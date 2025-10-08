#include "ComputeCore/gpu/performance/fallback_manager.h"
#include "ComputeCore/gpu/performance/performance_logger.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <thread>
#include <condition_variable>

namespace puzzle71::gpu::performance {

// Factory method implementation
std::unique_ptr<FallbackManager> FallbackManager::Create(int gpu_id) {
    return std::make_unique<GpuFallbackManager>(gpu_id);
}

// GpuFallbackManager implementation
GpuFallbackManager::GpuFallbackManager(int gpu_id) {
    PERF_LOG_INFO("fallback_manager", "Initializing GPU fallback manager",
                 json{{"gpu_id", gpu_id}});

    InitializeBaseConfigs();
    InitializeDefaultStrategies();

    PERF_LOG_INFO("fallback_manager", "GPU fallback manager initialized",
                 json{{"base_config", base_config_.ToJson()},
                      {"strategies_count", strategies_.size()}});
}

FallbackResult GpuFallbackManager::HandleFailure(FailureType failure_type,
                                                const std::string& context,
                                                const FallbackConfig& current_config) {
    std::lock_guard<std::mutex> lock(fallback_mutex_);

    FallbackResult result;
    result.failure_type = failure_type;
    result.from_level = current_level_;
    result.failure_reason = context;

    auto start_time = std::chrono::steady_clock::now();

    PERF_LOG_WARNING("fallback_manager", "Handling failure",
                    json{{"failure_type", fallback_utils::FailureTypeToString(failure_type)},
                         {"context", context},
                         {"current_level", fallback_utils::FallbackLevelToString(current_level_)}});

    // Record the failure
    RecordFailure(failure_type);

    // Check if we're in cooldown period for this failure type
    if (IsInCooldownPeriod(failure_type)) {
        result.recovery_successful = false;
        result.additional_info = {"reason", "In cooldown period"};
        PERF_LOG_DEBUG("fallback_manager", "Failure rejected - cooldown period active");
        return result;
    }

    // Find appropriate fallback strategy
    FallbackStrategy* strategy = nullptr;
    for (auto& s : strategies_) {
        if (s.failure_type == failure_type && s.condition(current_config)) {
            strategy = &s;
            break;
        }
    }

    if (!strategy) {
        // Create a default strategy for this failure type
        strategy = &strategies_.emplace_back(CreateStrategyForFailure(failure_type));
    }

    // Apply fallback strategy
    FallbackLevel target_level = strategy->target_level;
    if (target_level <= current_level_) {
        // Need to fallback to a more conservative level
        if (target_level == current_level_) {
            target_level = static_cast<FallbackLevel>(static_cast<int>(target_level) + 1);
        }
    }

    result.to_level = target_level;
    result.strategy_used = strategy->description;

    // Apply the new configuration
    FallbackConfig new_config = ApplyFallbackLevel(target_level);
    SetFallbackLevel(target_level);

    // Update the base config to use the new parameters
    base_config_ = new_config;

    // Test the new configuration
    bool config_valid = ValidateConfig(new_config);
    bool system_healthy = IsSystemHealthy();

    result.recovery_successful = config_valid && system_healthy;

    auto end_time = std::chrono::steady_clock::now();
    result.recovery_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    // Update cooldown tracking
    last_failure_time_by_type_[failure_type] = std::chrono::steady_clock::now();

    UpdateFallbackHistory(result);

    if (result.recovery_successful) {
        PERF_LOG_INFO("fallback_manager", "Fallback successful",
                     json{{"from_level", fallback_utils::FallbackLevelToString(result.from_level)},
                          {"to_level", fallback_utils::FallbackLevelToString(result.to_level)},
                          {"strategy", result.strategy_used},
                          {"recovery_time_ms", result.recovery_time.count() / 1000.0}});
    } else {
        PERF_LOG_ERROR("fallback_manager", "Fallback failed",
                      json{{"failure_type", fallback_utils::FailureTypeToString(failure_type)},
                           {"target_level", fallback_utils::FallbackLevelToString(target_level)},
                           {"config_valid", config_valid},
                           {"system_healthy", system_healthy}});
    }

    return result;
}

FallbackConfig GpuFallbackManager::GetOptimalConfig(FallbackLevel level) const {
    auto it = level_configs_.find(level);
    if (it != level_configs_.end()) {
        return it->second;
    }

    // Return safe default if level not found
    return GetSafeConfig();
}

bool GpuFallbackManager::CanRecoverToHigherLevel(FallbackLevel current_level,
                                                FailureType last_failure_type) const {
    // Only allow recovery if we haven't had the same failure type recently
    auto it = last_failure_time_by_type_.find(last_failure_type);
    if (it != last_failure_time_by_type_.end()) {
        auto time_since_failure = std::chrono::steady_clock::now() - it->second;
        // Require at least 5 minutes since last failure of this type
        if (time_since_failure < std::chrono::minutes(5)) {
            return false;
        }
    }

    // Can only recover if we're not at the most conservative level
    return current_level > FallbackLevel::Conservative;
}

void GpuFallbackManager::SetBaseConfig(const FallbackConfig& config) {
    std::lock_guard<std::mutex> lock(fallback_mutex_);
    base_config_ = config;
    PERF_LOG_DEBUG("fallback_manager", "Base configuration updated");
}

FallbackConfig GpuFallbackManager::GetCurrentConfig() const {
    std::lock_guard<std::mutex> lock(fallback_mutex_);
    return base_config_;
}

FallbackLevel GpuFallbackManager::GetCurrentLevel() const {
    std::lock_guard<std::mutex> lock(fallback_mutex_);
    return current_level_;
}

void GpuFallbackManager::SetFallbackLevel(FallbackLevel level) {
    std::lock_guard<std::mutex> lock(fallback_mutex_);
    current_level_ = level;
    base_config_ = GetOptimalConfig(level);

    PERF_LOG_INFO("fallback_manager", "Fallback level changed",
                 json{{"new_level", fallback_utils::FallbackLevelToString(level)},
                      {"config", base_config_.ToJson()}});
}

void GpuFallbackManager::RegisterFallbackStrategy(const FallbackStrategy& strategy) {
    std::lock_guard<std::mutex> lock(fallback_mutex_);

    // Remove existing strategy for the same failure type
    strategies_.erase(
        std::remove_if(strategies_.begin(), strategies_.end(),
                      [&](const FallbackStrategy& s) { return s.failure_type == strategy.failure_type; }),
        strategies_.end()
    );

    strategies_.push_back(strategy);

    PERF_LOG_INFO("fallback_manager", "Fallback strategy registered",
                 json{{"failure_type", fallback_utils::FailureTypeToString(strategy.failure_type)},
                      {"target_level", fallback_utils::FallbackLevelToString(strategy.target_level)},
                      {"description", strategy.description}});
}

void GpuFallbackManager::UnregisterFallbackStrategy(FailureType failure_type) {
    std::lock_guard<std::mutex> lock(fallback_mutex_);

    size_t before = strategies_.size();
    strategies_.erase(
        std::remove_if(strategies_.begin(), strategies_.end(),
                      [&](const FallbackStrategy& s) { return s.failure_type == failure_type; }),
        strategies_.end()
    );

    PERF_LOG_INFO("fallback_manager", "Fallback strategy unregistered",
                 json{{"failure_type", fallback_utils::FailureTypeToString(failure_type)},
                      {"strategies_removed", before - strategies_.size()}});
}

std::vector<FallbackStrategy> GpuFallbackManager::GetActiveStrategies() const {
    std::lock_guard<std::mutex> lock(fallback_mutex_);
    return strategies_;
}

bool GpuFallbackManager::ShouldTriggerFallback(const FallbackConfig& config) const {
    // Check system health indicators
    if (!IsSystemHealthy()) {
        return true;
    }

    // Check configuration validity
    if (!ValidateConfig(config)) {
        return true;
    }

    // Check for resource constraints
    if (resource_profiler_) {
        auto memory_usage = resource_profiler_->GetMemoryUsage(gpu_id_);
        auto thermal_state = resource_profiler_->GetThermalState(gpu_id_);

        if (memory_usage.utilization_percentage > config.max_memory_utilization_percent) {
            return true;
        }

        if (thermal_state.temperature_celsius > config.max_temperature_celsius) {
            return true;
        }

        if (thermal_state.thermal_throttling) {
            return true;
        }
    }

    // Check performance metrics
    if (metrics_collector_) {
        auto stats = metrics_collector_->GetStatistics(std::chrono::minutes(1));
        if (stats.error_rate_percent > 1.0) { // More than 1% error rate
            return true;
        }
    }

    return false;
}

bool GpuFallbackManager::ValidateConfig(const FallbackConfig& config) const {
    std::vector<std::string> warnings = GetValidationWarnings(config);
    return warnings.empty();
}

std::vector<std::string> GpuFallbackManager::GetValidationWarnings(const FallbackConfig& config) const {
    std::vector<std::string> warnings;

    if (!ValidateBlockSize(config.block_size)) {
        warnings.push_back("Invalid block size: " + std::to_string(config.block_size));
    }

    if (!ValidatePointsPerThread(config.points_per_thread)) {
        warnings.push_back("Invalid points_per_thread: " + std::to_string(config.points_per_thread));
    }

    if (!ValidateMemoryUsage(config.memory_pool_size_mb)) {
        warnings.push_back("Invalid memory pool size: " + std::to_string(config.memory_pool_size_mb) + "MB");
    }

    if (!ValidateOccupancyRatio(config.max_occupancy_ratio)) {
        warnings.push_back("Invalid occupancy ratio: " + std::to_string(config.max_occupancy_ratio));
    }

    if (config.target_throughput_mkeys_per_sec < config.min_acceptable_throughput_mkeys_per_sec) {
        warnings.push_back("Target throughput below minimum acceptable");
    }

    return warnings;
}

bool GpuFallbackManager::AttemptRecovery() {
    std::lock_guard<std::mutex> lock(fallback_mutex_);

    PERF_LOG_INFO("fallback_manager", "Attempting recovery",
                 json{{"current_level", fallback_utils::FallbackLevelToString(current_level_)}});

    // First, check if we can recover to a higher level
    if (current_level_ > FallbackLevel::Conservative) {
        FallbackLevel higher_level = static_cast<FallbackLevel>(static_cast<int>(current_level_) - 1);

        FallbackConfig test_config = GetOptimalConfig(higher_level);
        if (ValidateConfig(test_config) && IsSystemHealthy()) {
            SetFallbackLevel(higher_level);
            total_fallbacks_++;
            successful_recoveries_++;

            PERF_LOG_INFO("fallback_manager", "Recovery successful - upgraded level",
                         json{{"new_level", fallback_utils::FallbackLevelToString(higher_level)}});
            return true;
        }
    }

    // If we can't recover to a higher level, at least ensure current level works
    if (ValidateConfig(base_config_) && IsSystemHealthy()) {
        total_fallbacks_++;
        successful_recoveries_++;

        PERF_LOG_INFO("fallback_manager", "Recovery successful - current level stable");
        return true;
    }

    // If current level still doesn't work, fall back further
    if (current_level_ < FallbackLevel::Reference) {
        FallbackLevel lower_level = static_cast<FallbackLevel>(static_cast<int>(current_level_) + 1);
        SetFallbackLevel(lower_level);

        total_fallbacks_++;
        // Don't count this as successful recovery yet - need to test

        PERF_LOG_INFO("fallback_manager", "Recovery in progress - downgraded level",
                     json{{"new_level", fallback_utils::FallbackLevelToString(lower_level)}});
        return true;
    }

    PERF_LOG_WARNING("fallback_manager", "Recovery failed - already at reference level");
    return false;
}

bool GpuFallbackManager::IsSystemHealthy() const {
    // Check CUDA health
    if (!CheckCudaHealth()) {
        return false;
    }

    // Check memory health
    if (!CheckMemoryHealth()) {
        return false;
    }

    // Check thermal health
    if (!CheckThermalHealth()) {
        return false;
    }

    // Check performance health
    if (!CheckPerformanceHealth()) {
        return false;
    }

    return true;
}

std::vector<FallbackResult> GpuFallbackManager::GetFallbackHistory(size_t count) const {
    std::lock_guard<std::mutex> lock(fallback_mutex_);

    std::vector<FallbackResult> recent;
    size_t start_idx = fallback_history_.size() > count ?
                      fallback_history_.size() - count : 0;

    for (size_t i = start_idx; i < fallback_history_.size(); ++i) {
        recent.push_back(fallback_history_[i]);
    }

    return recent;
}

FallbackResult GpuFallbackManager::GetLastFallbackResult() const {
    std::lock_guard<std::mutex> lock(fallback_mutex_);

    if (fallback_history_.empty()) {
        return FallbackResult{};
    }

    return fallback_history_.back();
}

double GpuFallbackManager::GetFallbackSuccessRate() const {
    int total = total_fallbacks_.load();
    int successful = successful_recoveries_.load();

    if (total == 0) {
        return 0.0;
    }

    return (static_cast<double>(successful) / static_cast<double>(total)) * 100.0;
}

std::map<FailureType, int> GpuFallbackManager::GetFailureCounts() const {
    std::lock_guard<std::mutex> lock(fallback_mutex_);
    return failure_counts_;
}

std::chrono::steady_clock::time_point GpuFallbackManager::GetLastFailureTime() const {
    std::lock_guard<std::mutex> lock(fallback_mutex_);
    return last_failure_time_;
}

void GpuFallbackManager::SetErrorHandler(ErrorHandler* error_handler) {
    error_handler_ = error_handler;
    PERF_LOG_DEBUG("fallback_manager", "Error handler set");
}

void GpuFallbackManager::SetResourceProfiler(ResourceProfiler* profiler) {
    resource_profiler_ = profiler;
    PERF_LOG_DEBUG("fallback_manager", "Resource profiler set");
}

void GpuFallbackManager::SetMetricsCollector(MetricsCollector* collector) {
    metrics_collector_ = collector;
    PERF_LOG_DEBUG("fallback_manager", "Metrics collector set");
}

FallbackConfig GpuFallbackManager::GetAggressiveConfig() const {
    FallbackConfig config;
    config.level = FallbackLevel::None;
    config.points_per_thread = 256;
    config.block_size = 1024;
    config.max_concurrent_streams = 8;
    config.memory_pool_size_mb = 4096;
    config.enable_async_operations = true;
    config.enable_memory_optimization = true;
    config.enable_compute_optimization = true;
    config.max_occupancy_ratio = 0.9;
    config.target_throughput_mkeys_per_sec = 100.0;
    config.min_acceptable_throughput_mkeys_per_sec = 50.0;
    config.max_memory_utilization_percent = 85.0;
    config.max_gpu_utilization_percent = 95.0;
    config.max_temperature_celsius = 85.0;
    return config;
}

FallbackConfig GpuFallbackManager::GetBalancedConfig() const {
    FallbackConfig config;
    config.level = FallbackLevel::Conservative;
    config.points_per_thread = 128;
    config.block_size = 768;
    config.max_concurrent_streams = 6;
    config.memory_pool_size_mb = 3072;
    config.enable_async_operations = true;
    config.enable_memory_optimization = true;
    config.enable_compute_optimization = true;
    config.max_occupancy_ratio = 0.8;
    config.target_throughput_mkeys_per_sec = 75.0;
    config.min_acceptable_throughput_mkeys_per_sec = 30.0;
    config.max_memory_utilization_percent = 75.0;
    config.max_gpu_utilization_percent = 90.0;
    config.max_temperature_celsius = 80.0;
    return config;
}

FallbackConfig GpuFallbackManager::GetConservativeConfig() const {
    FallbackConfig config;
    config.level = FallbackLevel::Safe;
    config.points_per_thread = 64;
    config.block_size = 512;
    config.max_concurrent_streams = 4;
    config.memory_pool_size_mb = 2048;
    config.enable_async_operations = true;
    config.enable_memory_optimization = false;
    config.enable_compute_optimization = false;
    config.max_occupancy_ratio = 0.7;
    config.target_throughput_mkeys_per_sec = 40.0;
    config.min_acceptable_throughput_mkeys_per_sec = 15.0;
    config.max_memory_utilization_percent = 60.0;
    config.max_gpu_utilization_percent = 80.0;
    config.max_temperature_celsius = 75.0;
    return config;
}

FallbackConfig GpuFallbackManager::GetSafeConfig() const {
    FallbackConfig config;
    config.level = FallbackLevel::Minimal;
    config.points_per_thread = 32;
    config.block_size = 192; // Conservative but supports expanded range
    config.max_concurrent_streams = 2;
    config.memory_pool_size_mb = 1024;
    config.enable_async_operations = false;
    config.enable_memory_optimization = false;
    config.enable_compute_optimization = false;
    config.max_occupancy_ratio = 0.6;
    config.target_throughput_mkeys_per_sec = 20.0;
    config.min_acceptable_throughput_mkeys_per_sec = 5.0;
    config.max_memory_utilization_percent = 50.0;
    config.max_gpu_utilization_percent = 70.0;
    config.max_temperature_celsius = 70.0;
    return config;
}

FallbackConfig GpuFallbackManager::GetReferenceConfig() const {
    FallbackConfig config;
    config.level = FallbackLevel::Reference;
    config.points_per_thread = 1;
    config.block_size = 128;
    config.max_concurrent_streams = 1;
    config.memory_pool_size_mb = 256;
    config.enable_async_operations = false;
    config.enable_memory_optimization = false;
    config.enable_compute_optimization = false;
    config.max_occupancy_ratio = 0.4;
    config.target_throughput_mkeys_per_sec = 1.0;
    config.min_acceptable_throughput_mkeys_per_sec = 0.5;
    config.max_memory_utilization_percent = 30.0;
    config.max_gpu_utilization_percent = 50.0;
    config.max_temperature_celsius = 65.0;
    return config;
}

// Private methods
void GpuFallbackManager::InitializeDefaultStrategies() {
    // CUDA Driver Failure Strategy
    RegisterFallbackStrategy({
        FailureType::CudaDriverFailure,
        FallbackLevel::Reference,
        "CUDA driver failure - fallback to reference implementation",
        [](const FallbackConfig& config) { (void)config; return true; },
        [this](const FallbackConfig& config, const std::string& context) {
            (void)config; (void)context;
            return GetReferenceConfig();
        },
        true,
        std::chrono::minutes(5),
        0.9
    });

    // Out of Memory Strategy
    RegisterFallbackStrategy({
        FailureType::OutOfMemory,
        FallbackLevel::Conservative,
        "Out of memory - reduce memory usage",
        [](const FallbackConfig& config) { (void)config; return true; },
        [this](const FallbackConfig& config, const std::string& context) {
            (void)config; (void)context;
            return GetConservativeConfig();
        },
        true,
        std::chrono::minutes(2),
        0.8
    });

    // Thermal Throttling Strategy
    RegisterFallbackStrategy({
        FailureType::ThermalThrottling,
        FallbackLevel::Safe,
        "Thermal throttling - reduce computational intensity",
        [](const FallbackConfig& config) { (void)config; return true; },
        [this](const FallbackConfig& config, const std::string& context) {
            (void)config; (void)context;
            return GetSafeConfig();
        },
        true,
        std::chrono::minutes(3),
        0.7
    });

    // Compute Error Strategy
    RegisterFallbackStrategy({
        FailureType::ComputeError,
        FallbackLevel::Minimal,
        "Compute errors - use minimal configuration",
        [](const FallbackConfig& config) { return true; },
        [this](const FallbackConfig& config, const std::string& context) {
            (void)config; (void)context;
            return GetSafeConfig();
        },
        true,
        std::chrono::minutes(1),
        0.6
    });

    PERF_LOG_INFO("fallback_manager", "Default fallback strategies initialized",
                 json{{"strategy_count", strategies_.size()}});
}

void GpuFallbackManager::InitializeBaseConfigs() {
    base_config_ = GetAggressiveConfig(); // Start with aggressive configuration
    current_level_ = FallbackLevel::None;

    // Initialize level configurations
    level_configs_[FallbackLevel::None] = GetAggressiveConfig();
    level_configs_[FallbackLevel::Conservative] = GetBalancedConfig();
    level_configs_[FallbackLevel::Safe] = GetConservativeConfig();
    level_configs_[FallbackLevel::Minimal] = GetSafeConfig();
    level_configs_[FallbackLevel::Reference] = GetReferenceConfig();

    PERF_LOG_DEBUG("fallback_manager", "Base configurations initialized for all fallback levels");
}

FallbackStrategy GpuFallbackManager::CreateStrategyForFailure(FailureType failure_type) {
    std::string description = "Auto-generated strategy for " +
                             fallback_utils::FailureTypeToString(failure_type);

    FallbackLevel target_level = FallbackLevel::Conservative;
    switch (failure_type) {
        case FailureType::CudaDriverFailure:
        case FailureType::HardwareTimeout:
            target_level = FallbackLevel::Reference;
            break;
        case FailureType::OutOfMemory:
        case FailureType::ThermalThrottling:
            target_level = FallbackLevel::Safe;
            break;
        case FailureType::ComputeError:
        case FailureType::SynchronizationError:
        case FailureType::StreamError:
        case FailureType::KernelLaunchFailure:
            target_level = FallbackLevel::Minimal;
            break;
        default:
            target_level = FallbackLevel::Conservative;
            break;
    }

    return {
        failure_type,
        target_level,
        description,
        [](const FallbackConfig& config) { (void)config; return true; },
        [this, target_level](const FallbackConfig& config, const std::string& context) {
            (void)config; (void)context;
            return GetOptimalConfig(target_level);
        },
        true,
        std::chrono::minutes(2),
        0.7
    };
}

FallbackConfig GpuFallbackManager::ApplyFallbackLevel(FallbackLevel level) const {
    return GetOptimalConfig(level);
}

bool GpuFallbackManager::IsInCooldownPeriod(FailureType failure_type) const {
    auto it = last_failure_time_by_type_.find(failure_type);
    if (it == last_failure_time_by_type_.end()) {
        return false;
    }

    auto time_since_failure = std::chrono::steady_clock::now() - it->second;

    // Find the strategy for this failure type to get cooldown period
    for (const auto& strategy : strategies_) {
        if (strategy.failure_type == failure_type) {
            return time_since_failure < strategy.cooldown_period;
        }
    }

    // Default cooldown period of 2 minutes if no strategy found
    return time_since_failure < std::chrono::minutes(2);
}

void GpuFallbackManager::UpdateFallbackHistory(const FallbackResult& result) {
    fallback_history_.push_back(result);
    last_failure_time_ = result.timestamp;

    // Keep only the last 1000 fallback results
    if (fallback_history_.size() > 1000) {
        fallback_history_.erase(fallback_history_.begin(),
                               fallback_history_.begin() + (fallback_history_.size() - 1000));
    }
}

void GpuFallbackManager::RecordFailure(FailureType failure_type) {
    failure_counts_[failure_type]++;
    last_failure_time_ = std::chrono::steady_clock::now();
    last_failure_time_by_type_[failure_type] = std::chrono::steady_clock::now();
}

// Health checking methods
bool GpuFallbackManager::CheckCudaHealth() const {
    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);

    if (error != cudaSuccess) {
        PERF_LOG_WARNING("fallback_manager", "CUDA health check failed",
                        json{{"error", cudaGetErrorString(error)}});
        return false;
    }

    if (gpu_id_ >= device_count) {
        PERF_LOG_WARNING("fallback_manager", "GPU ID out of range",
                        json{{"gpu_id", gpu_id_},
                             {"device_count", device_count}});
        return false;
    }

    // Try to set the device
    error = cudaSetDevice(gpu_id_);
    if (error != cudaSuccess) {
        PERF_LOG_WARNING("fallback_manager", "Failed to set GPU device",
                        json{{"gpu_id", gpu_id_},
                             {"error", cudaGetErrorString(error)}});
        return false;
    }

    return true;
}

bool GpuFallbackManager::CheckMemoryHealth() const {
    if (!resource_profiler_) {
        return true; // Assume healthy if no profiler available
    }

    auto memory_usage = resource_profiler_->GetMemoryUsage(gpu_id_);

    // Check if we have enough free memory
    if (memory_usage.utilization_percentage > 95.0) {
        PERF_LOG_WARNING("fallback_manager", "Memory utilization too high",
                        json{{"utilization_percent", memory_usage.utilization_percentage}});
        return false;
    }

    return true;
}

bool GpuFallbackManager::CheckThermalHealth() const {
    if (!resource_profiler_) {
        return true; // Assume healthy if no profiler available
    }

    auto thermal_state = resource_profiler_->GetThermalState(gpu_id_);

    // Check temperature
    if (thermal_state.temperature_celsius > 90.0) {
        PERF_LOG_WARNING("fallback_manager", "GPU temperature too high",
                        json{{"temperature_celsius", thermal_state.temperature_celsius}});
        return false;
    }

    // Check for thermal throttling
    if (thermal_state.thermal_throttling) {
        PERF_LOG_WARNING("fallback_manager", "GPU thermal throttling detected");
        return false;
    }

    return true;
}

bool GpuFallbackManager::CheckPerformanceHealth() const {
    if (!metrics_collector_) {
        return true; // Assume healthy if no metrics collector available
    }

    auto stats = metrics_collector_->GetStatistics(std::chrono::minutes(1));

    // Check error rate
    if (stats.error_rate_percent > 5.0) {
        PERF_LOG_WARNING("fallback_manager", "High error rate detected",
                        json{{"error_rate_percent", stats.error_rate_percent}});
        return false;
    }

    // Check for extremely low throughput (indicating issues)
    if (stats.sustained_throughput_mkeys_per_sec < base_config_.min_acceptable_throughput_mkeys_per_sec) {
        PERF_LOG_WARNING("fallback_manager", "Throughput below minimum acceptable",
                        json{{"throughput_mkeys_per_sec", stats.sustained_throughput_mkeys_per_sec},
                             {"minimum_acceptable", base_config_.min_acceptable_throughput_mkeys_per_sec}});
        return false;
    }

    return true;
}

// Configuration validation methods
bool GpuFallbackManager::ValidateBlockSize(int block_size) const {
    return block_size >= 32 && block_size <= 1024 && (block_size % 32 == 0);
}

bool GpuFallbackManager::ValidatePointsPerThread(int points_per_thread) const {
    return points_per_thread >= 1 && points_per_thread <= 256;
}

bool GpuFallbackManager::ValidateMemoryUsage(size_t memory_mb) const {
    return memory_mb >= 64 && memory_mb <= 16384; // 64MB to 16GB
}

bool GpuFallbackManager::ValidateOccupancyRatio(double ratio) const {
    return ratio >= 0.1 && ratio <= 1.0;
}

// FallbackGuard implementation
FallbackGuard::FallbackGuard(GpuFallbackManager* manager, const FallbackConfig& config)
    : manager_(manager), initial_config_(config), monitoring_active_(true) {

    if (manager_) {
        manager_->SetBaseConfig(config);
        last_result_ = manager_->GetLastFallbackResult();
    }
}

FallbackGuard::~FallbackGuard() {
    if (monitoring_active_ && manager_) {
        // Check if we need to trigger a fallback
        if (manager_->ShouldTriggerFallback(manager_->GetCurrentConfig())) {
            // Try to auto-recover
            if (!manager_->AttemptRecovery()) {
                PERF_LOG_WARNING("fallback_manager", "Auto-recovery failed in fallback guard");
            }
        }
    }
}

bool FallbackGuard::IsHealthy() const {
    return manager_ && manager_->IsSystemHealthy();
}

FallbackResult FallbackGuard::GetLastResult() const {
    return last_result_;
}

FallbackConfig FallbackGuard::GetCurrentConfig() const {
    return manager_ ? manager_->GetCurrentConfig() : initial_config_;
}

// Utility functions implementation
namespace fallback_utils {

std::string FailureTypeToString(FailureType type) {
    switch (type) {
        case FailureType::CudaDriverFailure: return "CudaDriverFailure";
        case FailureType::OutOfMemory: return "OutOfMemory";
        case FailureType::ThermalThrottling: return "ThermalThrottling";
        case FailureType::HardwareTimeout: return "HardwareTimeout";
        case FailureType::ComputeError: return "ComputeError";
        case FailureType::SynchronizationError: return "SynchronizationError";
        case FailureType::StreamError: return "StreamError";
        case FailureType::KernelLaunchFailure: return "KernelLaunchFailure";
        case FailureType::ValidationFailure: return "ValidationFailure";
        case FailureType::Unknown: return "Unknown";
        default: return "Invalid";
    }
}

FailureType StringToFailureType(const std::string& str) {
    if (str == "CudaDriverFailure") return FailureType::CudaDriverFailure;
    if (str == "OutOfMemory") return FailureType::OutOfMemory;
    if (str == "ThermalThrottling") return FailureType::ThermalThrottling;
    if (str == "HardwareTimeout") return FailureType::HardwareTimeout;
    if (str == "ComputeError") return FailureType::ComputeError;
    if (str == "SynchronizationError") return FailureType::SynchronizationError;
    if (str == "StreamError") return FailureType::StreamError;
    if (str == "KernelLaunchFailure") return FailureType::KernelLaunchFailure;
    if (str == "ValidationFailure") return FailureType::ValidationFailure;
    return FailureType::Unknown;
}

std::string FallbackLevelToString(FallbackLevel level) {
    switch (level) {
        case FallbackLevel::None: return "None";
        case FallbackLevel::Conservative: return "Conservative";
        case FallbackLevel::Safe: return "Safe";
        case FallbackLevel::Minimal: return "Minimal";
        case FallbackLevel::Reference: return "Reference";
        default: return "Invalid";
    }
}

FallbackLevel StringToFallbackLevel(const std::string& str) {
    if (str == "None") return FallbackLevel::None;
    if (str == "Conservative") return FallbackLevel::Conservative;
    if (str == "Safe") return FallbackLevel::Safe;
    if (str == "Minimal") return FallbackLevel::Minimal;
    if (str == "Reference") return FallbackLevel::Reference;
    return FallbackLevel::None;
}

FallbackConfig CreateConfigFromJson(const json& j) {
    FallbackConfig config;
    config.level = StringToFallbackLevel(j.value("level", "None"));
    config.points_per_thread = j.value("points_per_thread", 256);
    config.block_size = j.value("block_size", 1024);
    config.max_concurrent_streams = j.value("max_concurrent_streams", 8);
    config.memory_pool_size_mb = j.value("memory_pool_size_mb", 4096);
    config.enable_async_operations = j.value("enable_async_operations", true);
    config.enable_memory_optimization = j.value("enable_memory_optimization", true);
    config.enable_compute_optimization = j.value("enable_compute_optimization", true);
    config.max_occupancy_ratio = j.value("max_occupancy_ratio", 0.9);
    config.retry_attempts = j.value("retry_attempts", 3);
    config.retry_delay = std::chrono::milliseconds(j.value("retry_delay_ms", 100));
    config.target_throughput_mkeys_per_sec = j.value("target_throughput_mkeys_per_sec", 100.0);
    config.min_acceptable_throughput_mkeys_per_sec = j.value("min_acceptable_throughput_mkeys_per_sec", 10.0);
    config.max_memory_utilization_percent = j.value("max_memory_utilization_percent", 85.0);
    config.max_gpu_utilization_percent = j.value("max_gpu_utilization_percent", 95.0);
    config.max_temperature_celsius = j.value("max_temperature_celsius", 85.0);
    return config;
}

json FallbackResultToJson(const FallbackResult& result) {
    json j;
    j["fallback_triggered"] = result.fallback_triggered;
    j["failure_type"] = FailureTypeToString(result.failure_type);
    j["from_level"] = FallbackLevelToString(result.from_level);
    j["to_level"] = FallbackLevelToString(result.to_level);
    j["failure_reason"] = result.failure_reason;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        result.timestamp.time_since_epoch()).count();
    j["strategy_used"] = result.strategy_used;
    j["recovery_successful"] = result.recovery_successful;
    j["recovery_time_us"] = result.recovery_time.count();
    j["additional_info"] = result.additional_info;
    return j;
}

std::string FormatFallbackReport(const FallbackResult& result) {
    std::ostringstream oss;
    oss << "Fallback Report:\n";
    oss << "  Failure Type: " << FailureTypeToString(result.failure_type) << "\n";
    oss << "  From Level: " << FallbackLevelToString(result.from_level) << "\n";
    oss << "  To Level: " << FallbackLevelToString(result.to_level) << "\n";
    oss << "  Reason: " << result.failure_reason << "\n";
    oss << "  Strategy: " << result.strategy_used << "\n";
    oss << "  Recovery Successful: " << (result.recovery_successful ? "YES" : "NO") << "\n";
    oss << "  Recovery Time: " << result.recovery_time.count() / 1000.0 << " ms\n";

    if (!result.additional_info.empty()) {
        oss << "  Additional Info: " << result.additional_info.dump() << "\n";
    }

    return oss.str();
}

bool IsCudaErrorRecoverable(cudaError_t error) {
    switch (error) {
        case cudaErrorMemoryAllocation:
        case cudaErrorLaunchTimeout:
        case cudaErrorLaunchFailure:
        case cudaErrorLaunchOutOfResources:
        case cudaErrorUnknown:
            return false; // These are typically not recoverable without full reset

        case cudaErrorLaunchIncompatibleTexturing:
        case cudaErrorPeerAccessAlreadyEnabled:
        case cudaErrorInvalidDevice:
        case cudaErrorInvalidValue:
        case cudaErrorInvalidMemcpyDirection:
            return true; // These can often be recovered by adjusting parameters

        default:
            return true; // Assume recoverable by default
    }
}

FailureType CudaErrorToFailureType(cudaError_t error) {
    switch (error) {
        case cudaErrorMemoryAllocation:
            return FailureType::OutOfMemory;
        case cudaErrorLaunchTimeout:
            return FailureType::HardwareTimeout;
        case cudaErrorLaunchFailure:
        case cudaErrorLaunchOutOfResources:
            return FailureType::KernelLaunchFailure;
        case cudaErrorInvalidDevice:
        case cudaErrorNoDevice:
            return FailureType::CudaDriverFailure;
        default:
            return FailureType::ComputeError;
    }
}

std::vector<FallbackLevel> GetRecoveryPath(FallbackLevel from, FallbackLevel to) {
    std::vector<FallbackLevel> path;

    if (from <= to) {
        // Fallback path (going more conservative)
        for (int level = static_cast<int>(from); level <= static_cast<int>(to); ++level) {
            path.push_back(static_cast<FallbackLevel>(level));
        }
    } else {
        // Recovery path (going less conservative)
        for (int level = static_cast<int>(from); level >= static_cast<int>(to); --level) {
            path.push_back(static_cast<FallbackLevel>(level));
        }
    }

    return path;
}

} // namespace fallback_utils

} // namespace puzzle71::gpu::performance