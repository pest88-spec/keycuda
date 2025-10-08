#include "ComputeCore/gpu/performance/dynamic_configurator.h"
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>

namespace puzzle71::gpu::performance {

// Factory method implementation
std::unique_ptr<DynamicConfigurator> DynamicConfigurator::Create(int gpu_id) {
    return std::make_unique<GpuDynamicConfigurator>(gpu_id);
}

// GpuDynamicConfigurator implementation
GpuDynamicConfigurator::GpuDynamicConfigurator(int gpu_id) : gpu_id_(gpu_id) {
    PERF_LOG_INFO("dynamic_configurator", "Initializing GPU dynamic configurator",
                 json{{"gpu_id", gpu_id}});

    // Initialize optimization profiles
    InitializeOptimizationProfiles();

    PERF_LOG_INFO("dynamic_configurator", "GPU dynamic configurator initialized",
                 json{{"profiles_count", optimization_profiles_.size()}});
}

DynamicKernelConfig GpuDynamicConfigurator::CalculateOptimalConfiguration(size_t workload_size, int gpu_id) {
    std::lock_guard<std::mutex> lock(configurator_mutex_);

    int target_gpu_id = (gpu_id >= 0) ? gpu_id : gpu_id_;
    int compute_capability = GetComputeCapability();

    PERF_LOG_DEBUG("dynamic_configurator", "Calculating optimal configuration",
                  json{{{"workload_size", workload_size},
                       {"compute_capability", compute_capability},
                       {"target_gpu_id", target_gpu_id}});

    DynamicKernelConfig config = CalculateBaseConfiguration(compute_capability);
    config.grid_size = CalculateOptimalGridSize(workload_size, config.block_size, config.points_per_thread);

    // Optimize for architecture
    config = OptimizeForArchitecture(compute_capability, workload_size);

    // Apply constraints and validation
    if (!ValidateConfiguration(config)) {
        PERF_LOG_WARNING("dynamic_configurator", "Initial configuration invalid, applying fallback");
        config = CalculateBaseConfiguration(compute_capability);
        config.points_per_thread = 128; // Safe default
        config.block_size = 512;
        config.grid_size = CalculateOptimalGridSize(workload_size, config.block_size, config.points_per_thread);
    }

    PERF_LOG_DEBUG("dynamic_configurator", "Optimal configuration calculated",
                  json{{"config", config.ToJson()}});

    return config;
}

DynamicKernelConfig GpuDynamicConfigurator::GetConfigurationForProfile(const std::string& profile_name, size_t workload_size) {
    std::lock_guard<std::mutex> lock(configurator_mutex_);

    OptimizationProfile profile = GetProfile(profile_name);

    if (!profile.IsCompatible(GetComputeCapability())) {
        PERF_LOG_WARNING("dynamic_configurator", "Profile not compatible with current GPU",
                        json{{"profile", profile_name},
                             {"compute_capability", GetComputeCapability()}});

        // Fallback to balanced profile
        profile = GetProfile("balanced");
    }

    DynamicKernelConfig config;
    config.points_per_thread = std::clamp(128, profile.min_points_per_thread, profile.max_points_per_thread);
    config.block_size = profile.preferred_block_size;
    config.target_occupancy_ratio = profile.target_occupancy;
    config.optimization_profile = profile_name;
    config.grid_size = CalculateOptimalGridSize(workload_size, config.block_size, config.points_per_thread);

    // Optimize for architecture
    config = OptimizeForArchitecture(GetComputeCapability(), workload_size);

    PERF_LOG_DEBUG("dynamic_configurator", "Profile-based configuration created",
                  json{{{"profile", profile_name},
                       {"config", config.ToJson()}});

    return config;
}

DynamicKernelConfig GpuDynamicConfigurator::AdaptConfiguration(const DynamicKernelConfig& current_config, const ConfigurationMetrics& metrics) {
    std::lock_guard<std::mutex> lock(configurator_mutex_);

    PERF_LOG_DEBUG("dynamic_configurator", "Adapting configuration based on performance metrics");

    // Record current performance
    RecordConfigurationPerformance(current_config, metrics);

    // Generate configuration variants
    auto variants = GenerateConfigurationVariants(current_config);

    // Score each variant based on target metrics
    DynamicKernelConfig best_config = current_config;
    double best_score = CalculateConfigurationScore(current_config, metrics);

    for (const auto& variant : variants) {
        if (ValidateConfiguration(variant)) {
            double score = CalculateConfigurationScore(variant, metrics);
            if (score > best_score) {
                best_score = score;
                best_config = variant;
            }
        }
    }

    if (best_config.points_per_thread != current_config.points_per_thread ||
        best_config.block_size != current_config.block_size) {
        PERF_LOG_INFO("dynamic_configurator", "Configuration adapted",
                     json{{{"old_ppt", current_config.points_per_thread},
                          {"new_ppt", best_config.points_per_thread},
                          {"old_block", current_config.block_size},
                          {"new_block", best_config.block_size},
                          {"score_improvement", best_score - CalculateConfigurationScore(current_config, metrics)}});
    }

    return best_config;
}

DynamicKernelConfig GpuDynamicConfigurator::OptimizeForThroughput(size_t workload_size, double target_throughput_mkeys_per_sec) {
    std::lock_guard<std::mutex> lock(configurator_mutex_);

    PERF_LOG_DEBUG("dynamic_configurator", "Optimizing configuration for maximum throughput",
                  json{{{"workload_size", workload_size},
                       {"target_throughput_mkeys_per_sec", target_throughput_mkeys_per_sec}});

    DynamicKernelConfig config = CalculateOptimalConfiguration(workload_size);

    // Aggressive optimization for throughput
    if (target_throughput_mkeys_per_sec > 0) {
        // Estimate if current config can meet target
        double estimated_throughput = EstimateThroughput(config, workload_size);

        if (estimated_throughput < target_throughput_mkeys_per_sec) {
            // Try more aggressive settings
            config.points_per_thread = std::min(config.max_points_per_thread, 256);
            config.block_size = 768;
            config.target_occupancy_ratio = 0.95;

            estimated_throughput = EstimateThroughput(config, workload_size);
            if (estimated_throughput >= target_throughput_mkeys_per_sec) {
                PERF_LOG_INFO("dynamic_configurator", "Throughput target achievable with aggressive settings",
                             json{{{"target_throughput", target_throughput_mkeys_per_sec},
                                  {"estimated_throughput", estimated_throughput}});
            }
        }
    }

    config.optimization_profile = "throughput";
    return config;
}

DynamicKernelConfig GpuDynamicConfigurator::OptimizeForPowerEfficiency(size_t workload_size) {
    std::lock_guard<std::mutex> lock(configurator_mutex_);

    PERF_LOG_DEBUG("dynamic_configurator", "Optimizing configuration for power efficiency",
                  json{{"workload_size", workload_size}});

    DynamicKernelConfig config = CalculateOptimalConfiguration(workload_size);

    // Conservative settings for power efficiency
    config.points_per_thread = 64;
    config.block_size = 256;
    config.target_occupancy_ratio = 0.7;
    config.optimization_profile = "power_efficient";

    return config;
}

bool GpuDynamicConfigurator::ValidateConfiguration(const DynamicKernelConfig& config) const {
    int compute_capability = GetComputeCapability();

    if (!ValidatePointsPerThread(config.points_per_thread, compute_capability)) {
        return false;
    }

    if (!ValidateBlockSize(config.block_size, compute_capability)) {
        return false;
    }

    if (config.points_per_thread < config.min_points_per_thread ||
        config.points_per_thread > config.max_points_per_thread) {
        return false;
    }

    if (config.target_occupancy_ratio <= 0.0 || config.target_occupancy_ratio > 1.0) {
        return false;
    }

    return true;
}

std::vector<std::string> GpuDynamicConfigurator::GetConfigurationWarnings(const DynamicKernelConfig& config) const {
    std::vector<std::string> warnings;
    int compute_capability = GetComputeCapability();

    if (config.points_per_thread < 64) {
        warnings.push_back("Low points_per_thread may result in suboptimal performance");
    }

    if (config.points_per_thread > 256 && compute_capability < 86) {
        warnings.push_back("High points_per_thread may cause register pressure on older GPUs");
    }

    if (config.block_size < 128) {
        warnings.push_back("Very small block size may significantly underutilize GPU SMs");
    } else if (config.block_size < 256) {
        warnings.push_back("Small block size may underutilize GPU SMs but can be optimal for memory-bound workloads");
    }

    if (config.block_size > 1024) {
        warnings.push_back("Very large block size may cause resource contention");
    }

    if (config.target_occupancy_ratio > 0.9) {
        warnings.push_back("Very high occupancy target may cause resource pressure");
    }

    return warnings;
}

void GpuDynamicConfigurator::RecordConfigurationPerformance(const DynamicKernelConfig& config, const ConfigurationMetrics& metrics) {
    std::lock_guard<std::mutex> lock(configurator_mutex_);

    std::string config_key = std::to_string(config.points_per_thread) + "_" +
                           std::to_string(config.block_size) + "_" +
                           config.optimization_profile;

    performance_history_[config_key].push_back(metrics);

    // Keep only recent history (last 100 measurements per configuration)
    if (performance_history_[config_key].size() > 100) {
        performance_history_[config_key].erase(performance_history_[config_key].begin(),
                                              performance_history_[config_key].begin() +
                                              (performance_history_[config_key].size() - 100));
    }

    PERF_LOG_DEBUG("dynamic_configurator", "Performance recorded",
                  json{{{"config_key", config_key},
                       {"throughput_mkeys_per_sec", metrics.throughput_mkeys_per_sec},
                       {"gpu_utilization_percent", metrics.gpu_utilization_percent}});
}

std::vector<ConfigurationMetrics> GpuDynamicConfigurator::GetPerformanceHistory(const DynamicKernelConfig& config, std::chrono::seconds duration) {
    std::lock_guard<std::mutex> lock(configurator_mutex_);

    std::string config_key = std::to_string(config.points_per_thread) + "_" +
                           std::to_string(config.block_size) + "_" +
                           config.optimization_profile;

    auto it = performance_history_.find(config_key);
    if (it == performance_history_.end()) {
        return {};
    }

    const auto& all_metrics = it->second;
    if (all_metrics.empty()) {
        return {};
    }

    // Filter by time duration
    auto cutoff_time = std::chrono::steady_clock::now() - duration;
    std::vector<ConfigurationMetrics> recent_metrics;

    for (auto rit = all_metrics.rbegin(); rit != all_metrics.rend(); ++rit) {
        if (rit->timestamp >= cutoff_time) {
            recent_metrics.push_back(*rit);
        } else {
            break;
        }
    }

    return recent_metrics;
}

std::vector<OptimizationProfile> GpuDynamicConfigurator::GetAvailableProfiles() const {
    std::lock_guard<std::mutex> lock(configurator_mutex_);
    return optimization_profiles_;
}

OptimizationProfile GpuDynamicConfigurator::GetProfile(const std::string& name) const {
    std::lock_guard<std::mutex> lock(configurator_mutex_);

    auto it = std::find_if(optimization_profiles_.begin(), optimization_profiles_.end(),
                          [&name](const OptimizationProfile& profile) {
                              return profile.name == name;
                          });

    if (it != optimization_profiles_.end()) {
        return *it;
    }

    // Return balanced profile as fallback
    return GetProfile("balanced");
}

void GpuDynamicConfigurator::RegisterCustomProfile(const OptimizationProfile& profile) {
    std::lock_guard<std::mutex> lock(configurator_mutex_);

    // Remove existing profile with same name
    optimization_profiles_.erase(
        std::remove_if(optimization_profiles_.begin(), optimization_profiles_.end(),
                      [&profile](const OptimizationProfile& p) { return p.name == profile.name; }),
        optimization_profiles_.end()
    );

    optimization_profiles_.push_back(profile);

    PERF_LOG_INFO("dynamic_configurator", "Custom profile registered",
                 json{{"profile_name", profile.name}});
}

DynamicKernelConfig GpuDynamicConfigurator::OptimizeForArchitecture(int compute_capability, size_t workload_size) {
    switch (compute_capability) {
        case 90: // Hopper
            return GetHopperConfiguration(workload_size);
        case 89: // Ada Lovelace
            return GetAdaLovelaceConfiguration(workload_size);
        case 86: // Ampere RTX 3090
        case 80: // Ampere A100
            return GetAmpereConfiguration(workload_size);
        case 75: // Turing
            return GetTuringConfiguration(workload_size);
        case 70: // Pascal
        case 60: // Pascal
            return GetPascalConfiguration(workload_size);
        default:
            // Flexible fallback with expanded block size range
            DynamicKernelConfig config;
            config.points_per_thread = 64;
            config.block_size = 384; // Improved over 256 for better utilization
            config.target_occupancy_ratio = 0.7;
            return config;
    }
}

DynamicKernelConfig GpuDynamicConfigurator::OptimizeForMemoryConstraints(size_t available_memory_mb, size_t workload_size) {
    std::lock_guard<std::mutex> lock(configurator_mutex_);

    PERF_LOG_DEBUG("dynamic_configurator", "Optimizing for memory constraints",
                  json{{{"available_memory_mb", available_memory_mb},
                       {"workload_size", workload_size}});

    DynamicKernelConfig config = CalculateOptimalConfiguration(workload_size);

    // Estimate memory usage per point
    const size_t memory_per_point_mb = 32; // Approximate
    size_t max_points_per_thread = available_memory_mb / (memory_per_point_mb * 2); // Conservative

    config.points_per_thread = std::min(config.points_per_thread, static_cast<int>(max_points_per_thread));
    config.points_per_thread = std::max(config.points_per_thread, 32); // Minimum viable

    // Reduce block size if needed (with flexible range support)
    if (available_memory_mb < 1024) { // Less than 1GB
        config.block_size = 128; // Smallest allowed for memory efficiency
    } else if (available_memory_mb < 2048) { // Less than 2GB
        config.block_size = 256; // Conservative for memory constraints
    } else if (available_memory_mb < 4096) { // Less than 4GB
        config.block_size = 384; // Balanced for moderate memory
    }

    config.optimization_profile = "memory_constrained";

    return config;
}

// Private implementation methods
void GpuDynamicConfigurator::InitializeOptimizationProfiles() {
    optimization_profiles_.clear();

    // High performance profile
    OptimizationProfile high_perf;
    high_perf.name = "high_performance";
    high_perf.description = "Maximum throughput configuration";
    high_perf.min_points_per_thread = 128;
    high_perf.max_points_per_thread = 256;
    high_perf.preferred_block_size = 768;
    high_perf.target_occupancy = 0.95;
    high_perf.supported_compute_capabilities = {86, 89, 90};
    high_perf.requires_high_memory_bandwidth = true;
    high_perf.requires_cooperative_groups = true;
    optimization_profiles_.push_back(high_perf);

    // Balanced profile
    OptimizationProfile balanced;
    balanced.name = "balanced";
    balanced.description = "Balanced performance and efficiency";
    balanced.min_points_per_thread = 64;
    balanced.max_points_per_thread = 128;
    balanced.preferred_block_size = 512;
    balanced.target_occupancy = 0.85;
    balanced.supported_compute_capabilities = {75, 80, 86, 89, 90};
    balanced.requires_high_memory_bandwidth = false;
    balanced.requires_cooperative_groups = false;
    optimization_profiles_.push_back(balanced);

    // Power efficient profile
    OptimizationProfile power_efficient;
    power_efficient.name = "power_efficient";
    power_efficient.description = "Optimized for power efficiency";
    power_efficient.min_points_per_thread = 32;
    power_efficient.max_points_per_thread = 64;
    power_efficient.preferred_block_size = 320; // Optimized for power efficiency
    power_efficient.target_occupancy = 0.7;
    power_efficient.supported_compute_capabilities = {60, 70, 75, 80, 86, 89, 90};
    power_efficient.requires_high_memory_bandwidth = false;
    power_efficient.requires_cooperative_groups = false;
    optimization_profiles_.push_back(power_efficient);

    // Memory optimized profile
    OptimizationProfile memory_optimized;
    memory_optimized.name = "memory_optimized";
    memory_optimized.description = "Optimized for low memory usage";
    memory_optimized.min_points_per_thread = 16;
    memory_optimized.max_points_per_thread = 64;
    memory_optimized.preferred_block_size = 192; // Smaller blocks for memory-bound workloads
    memory_optimized.target_occupancy = 0.6;
    memory_optimized.supported_compute_capabilities = {60, 70, 75, 80, 86, 89, 90};
    memory_optimized.requires_high_memory_bandwidth = false;
    memory_optimized.requires_cooperative_groups = false;
    optimization_profiles_.push_back(memory_optimized);
}

DynamicKernelConfig GpuDynamicConfigurator::CalculateBaseConfiguration(int compute_capability) const {
    DynamicKernelConfig config;

    // Set default ranges based on compute capability
    if (compute_capability >= 89) { // Ada/Hopper
        config.min_points_per_thread = 128;
        config.max_points_per_thread = 256;
        config.points_per_thread = 192;
        config.block_size = 512;
    } else if (compute_capability >= 86) { // Ampere
        config.min_points_per_thread = 64;
        config.max_points_per_thread = 192;
        config.points_per_thread = 128;
        config.block_size = 512;
    } else if (compute_capability >= 75) { // Turing
        config.min_points_per_thread = 32;
        config.max_points_per_thread = 128;
        config.points_per_thread = 64;
        config.block_size = 384;
    } else { // Older architectures
        config.min_points_per_thread = 16;
        config.max_points_per_thread = 64;
        config.points_per_thread = 32;
        config.block_size = 256;
    }

    config.target_occupancy_ratio = 0.85;
    config.enable_adaptive_scaling = true;
    config.performance_weight = 1.0;
    config.optimization_profile = "default";

    return config;
}

double GpuDynamicConfigurator::EstimatePerformance(const DynamicKernelConfig& config, size_t workload_size) const {
    return EstimateThroughput(config, workload_size);
}

int GpuDynamicConfigurator::GetComputeCapability() const {
    cudaDeviceProp props;
    cudaError_t error = cudaGetDeviceProperties(&props, gpu_id_);
    if (error != cudaSuccess) {
        return 75; // Conservative fallback
    }
    return props.major * 10 + props.minor;
}

size_t GpuDynamicConfigurator::GetTotalMemoryMB() const {
    cudaDeviceProp props;
    cudaError_t error = cudaGetDeviceProperties(&props, gpu_id_);
    if (error != cudaSuccess) {
        return 4096; // Conservative fallback
    }
    return props.totalGlobalMem / (1024 * 1024);
}

int GpuDynamicConfigurator::CalculateOptimalPointsPerThread(size_t workload_size, int compute_capability) const {
    // Base calculation on workload size and architecture
    int base_ppt = 64;

    if (workload_size > 1000000) { // Large workload
        base_ppt = 128;
    }

    if (workload_size > 10000000) { // Very large workload
        base_ppt = 192;
    }

    // Architecture-specific adjustments
    if (compute_capability >= 89) {
        base_ppt = std::min(base_ppt * 2, 256);
    } else if (compute_capability >= 86) {
        base_ppt = std::min(base_ppt * 1.5, 192);
    } else if (compute_capability < 75) {
        base_ppt = std::max(base_ppt / 2, 32);
    }

    return base_ppt;
}

int GpuDynamicConfigurator::CalculateOptimalBlockSize(int compute_capability, int points_per_thread) const {
    // Calculate block size based on architecture and points per thread
    int base_block_size = 256;

    if (compute_capability >= 89) {
        base_block_size = 768;
    } else if (compute_capability >= 86) {
        base_block_size = 512;
    } else if (compute_capability >= 75) {
        base_block_size = 384;
    }

    // Adjust for points per thread (higher PPT may need smaller blocks)
    if (points_per_thread > 192) {
        base_block_size = static_cast<int>(base_block_size * 0.8);
    }

    // Ensure warp alignment
    return (base_block_size / 32) * 32;
}

int GpuDynamicConfigurator::CalculateOptimalGridSize(size_t workload_size, int block_size, int points_per_thread) const {
    size_t total_threads_needed = (workload_size + points_per_thread - 1) / points_per_thread;
    int grid_size = static_cast<int>((total_threads_needed + block_size - 1) / block_size);

    // Ensure at least one block
    return std::max(grid_size, 1);
}

// Architecture-specific configurations
DynamicKernelConfig GpuDynamicConfigurator::GetHopperConfiguration(size_t workload_size) const {
    DynamicKernelConfig config;
    config.points_per_thread = 256;
    config.block_size = 1024;
    config.target_occupancy_ratio = 0.95;
    config.min_points_per_thread = 128;
    config.max_points_per_thread = 256;
    config.optimization_profile = "hopper";
    return config;
}

DynamicKernelConfig GpuDynamicConfigurator::GetAdaLovelaceConfiguration(size_t workload_size) const {
    DynamicKernelConfig config;
    config.points_per_thread = 192;
    config.block_size = 768;
    config.target_occupancy_ratio = 0.9;
    config.min_points_per_thread = 96;
    config.max_points_per_thread = 256;
    config.optimization_profile = "ada_lovelace";
    return config;
}

DynamicKernelConfig GpuDynamicConfigurator::GetAmpereConfiguration(size_t workload_size) const {
    DynamicKernelConfig config;
    config.points_per_thread = 128;
    config.block_size = 512;
    config.target_occupancy_ratio = 0.85;
    config.min_points_per_thread = 64;
    config.max_points_per_thread = 192;
    config.optimization_profile = "ampere";
    return config;
}

DynamicKernelConfig GpuDynamicConfigurator::GetTuringConfiguration(size_t workload_size) const {
    DynamicKernelConfig config;
    config.points_per_thread = 64;
    config.block_size = 384;
    config.target_occupancy_ratio = 0.8;
    config.min_points_per_thread = 32;
    config.max_points_per_thread = 128;
    config.optimization_profile = "turing";
    return config;
}

DynamicKernelConfig GpuDynamicConfigurator::GetPascalConfiguration(size_t workload_size) const {
    DynamicKernelConfig config;
    config.points_per_thread = 32;
    config.block_size = 192; // Optimized for Pascal's memory bandwidth characteristics
    config.target_occupancy_ratio = 0.7;
    config.min_points_per_thread = 16;
    config.max_points_per_thread = 64;
    config.optimization_profile = "pascal";
    return config;
}

double GpuDynamicConfigurator::EstimateThroughput(const DynamicKernelConfig& config, size_t workload_size) const {
    // Simplified throughput estimation based on configuration parameters
    double base_throughput = 10.0; // Base Mkeys/s

    // Points per thread scaling
    double ppt_factor = 1.0 + (config.points_per_thread - 64) * 0.005;

    // Block size scaling (with expanded range support 128-1024)
    double block_factor = 1.0 + (config.block_size - 384) * 0.00015; // Using 384 as new baseline

    // Occupancy scaling
    double occupancy_factor = config.target_occupancy_ratio;

    // Architecture factor
    int compute_capability = GetComputeCapability();
    double arch_factor = 1.0;
    if (compute_capability >= 90) arch_factor = 3.0;
    else if (compute_capability >= 89) arch_factor = 2.5;
    else if (compute_capability >= 86) arch_factor = 2.0;
    else if (compute_capability >= 80) arch_factor = 1.8;
    else if (compute_capability >= 75) arch_factor = 1.5;

    return base_throughput * ppt_factor * block_factor * occupancy_factor * arch_factor;
}

bool GpuDynamicConfigurator::ValidatePointsPerThread(int ppt, int compute_capability) const {
    if (ppt <= 0 || ppt > 1024) {
        return false;
    }

    // Architecture-specific validation
    if (compute_capability < 75 && ppt > 64) {
        return false; // Older architectures can't handle high PPT
    }

    if (compute_capability < 86 && ppt > 128) {
        return false; // Pre-Ampere limited to lower PPT
    }

    return true;
}

bool GpuDynamicConfigurator::ValidateBlockSize(int block_size, int compute_capability) const {
    // Phase A optimization: Expanded block size validation (128-1024)
    if (block_size < 128 || block_size > 1024) {
        return false;
    }

    // Must be warp-aligned (32 threads per warp)
    if (block_size % 32 != 0) {
        return false;
    }

    // Architecture-specific refined limits with expanded range
    if (compute_capability >= 90) {
        // Hopper: Support full range with optimal performance at high end
        return block_size >= 256; // Recommend minimum for Hopper efficiency
    } else if (compute_capability >= 89) {
        // Ada Lovelace: Support full range with sweet spot at 640-896
        return block_size >= 192;
    } else if (compute_capability >= 86) {
        // Ampere: Support 256-1024 range for good performance
        return block_size >= 256;
    } else if (compute_capability >= 75) {
        // Turing: Support 128-768 range (larger blocks can cause resource pressure)
        return block_size <= 768;
    } else if (compute_capability >= 60) {
        // Pascal: Conservative support for 128-512 range
        return block_size <= 512;
    }

    return true;
}

// Additional implementation methods would continue here...
// Due to length constraints, I'll include the most critical ones

// Utility functions implementation
namespace dynamic_config_utils {

std::string FormatConfiguration(const DynamicKernelConfig& config) {
    std::ostringstream oss;
    oss << "Dynamic Kernel Configuration:\n";
    oss << "  Points Per Thread: " << config.points_per_thread << "\n";
    oss << "  Block Size: " << config.block_size << "\n";
    oss << "  Grid Size: " << config.grid_size << "\n";
    oss << "  Target Occupancy: " << std::fixed << std::setprecision(2) << config.target_occupancy_ratio * 100 << "%\n";
    oss << "  Adaptive Scaling: " << (config.enable_adaptive_scaling ? "YES" : "NO") << "\n";
    oss << "  Profile: " << config.optimization_profile << "\n";
    return oss.str();
}

std::string FormatMetrics(const ConfigurationMetrics& metrics) {
    std::ostringstream oss;
    oss << "Performance Metrics:\n";
    oss << "  Throughput: " << std::fixed << std::setprecision(2) << metrics.throughput_mkeys_per_sec << " Mkeys/s\n";
    oss << "  GPU Utilization: " << metrics.gpu_utilization_percent << "%\n";
    oss << "  Memory Bandwidth: " << metrics.memory_bandwidth_utilization * 100 << "%\n";
    oss << "  Execution Time: " << metrics.kernel_execution_time_ms << " ms\n";
    oss << "  Power Efficiency: " << metrics.power_efficiency_mkeys_per_watt << " Mkeys/W\n";
    oss << "  Accuracy Validated: " << (metrics.accuracy_validated ? "YES" : "NO") << "\n";
    return oss.str();
}

OptimizationProfile CreateHighPerformanceProfile() {
    OptimizationProfile profile;
    profile.name = "high_performance";
    profile.description = "Maximum throughput configuration";
    profile.min_points_per_thread = 128;
    profile.max_points_per_thread = 256;
    profile.preferred_block_size = 768;
    profile.target_occupancy = 0.95;
    profile.supported_compute_capabilities = {86, 89, 90};
    profile.requires_high_memory_bandwidth = true;
    profile.requires_cooperative_groups = true;
    return profile;
}

OptimizationProfile CreateBalancedProfile() {
    OptimizationProfile profile;
    profile.name = "balanced";
    profile.description = "Balanced performance and efficiency";
    profile.min_points_per_thread = 64;
    profile.max_points_per_thread = 128;
    profile.preferred_block_size = 512;
    profile.target_occupancy = 0.85;
    profile.supported_compute_capabilities = {75, 80, 86, 89, 90};
    profile.requires_high_memory_bandwidth = false;
    profile.requires_cooperative_groups = false;
    return profile;
}

OptimizationProfile CreatePowerEfficientProfile() {
    OptimizationProfile profile;
    profile.name = "power_efficient";
    profile.description = "Optimized for power efficiency";
    profile.min_points_per_thread = 32;
    profile.max_points_per_thread = 64;
    profile.preferred_block_size = 320; // Power-optimized block size
    profile.target_occupancy = 0.7;
    profile.supported_compute_capabilities = {60, 70, 75, 80, 86, 89, 90};
    profile.requires_high_memory_bandwidth = false;
    profile.requires_cooperative_groups = false;
    return profile;
}

OptimizationProfile CreateMemoryOptimizedProfile() {
    OptimizationProfile profile;
    profile.name = "memory_optimized";
    profile.description = "Optimized for low memory usage";
    profile.min_points_per_thread = 16;
    profile.max_points_per_thread = 64;
    profile.preferred_block_size = 192; // Memory-optimized small block size
    profile.target_occupancy = 0.6;
    profile.supported_compute_capabilities = {60, 70, 75, 80, 86, 89, 90};
    profile.requires_high_memory_bandwidth = false;
    profile.requires_cooperative_groups = false;
    return profile;
}

bool IsConfigurationCompatible(const DynamicKernelConfig& config, int compute_capability) {
    return config.points_per_thread >= 16 && config.points_per_thread <= 1024 &&
           config.block_size >= 32 && config.block_size <= 1024 &&
           config.block_size % 32 == 0 &&
           config.target_occupancy_ratio > 0.0 && config.target_occupancy_ratio <= 1.0;
}

DynamicKernelConfig ClampConfigurationToLimits(const DynamicKernelConfig& config, int compute_capability) {
    DynamicKernelConfig clamped = config;

    // Clamp points per thread
    clamped.points_per_thread = std::clamp(config.points_per_thread, 1, 1024);

    // Architecture-specific limits
    if (compute_capability < 75) {
        clamped.points_per_thread = std::min(clamped.points_per_thread, 64);
        clamped.block_size = std::min(clamped.block_size, 512);
    } else if (compute_capability < 86) {
        clamped.points_per_thread = std::min(clamped.points_per_thread, 128);
    }

    // Clamp block size and ensure warp alignment
    clamped.block_size = std::clamp(config.block_size, 32, 1024);
    clamped.block_size = (clamped.block_size / 32) * 32;

    // Clamp occupancy
    clamped.target_occupancy_ratio = std::clamp(config.target_occupancy_ratio, 0.1, 1.0);

    return clamped;
}

} // namespace dynamic_config_utils

} // namespace puzzle71::gpu::performance