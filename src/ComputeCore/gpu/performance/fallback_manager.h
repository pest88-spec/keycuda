#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include <atomic>
#include <string>
#include <cuda_runtime.h>
#include "core/uint256.h"
#include "ComputeCore/gpu/performance/error_handler.h"
#include "ComputeCore/gpu/performance/resource_profiler.h"
#include "ComputeCore/gpu/performance/metrics_collector.h"

namespace puzzle71::gpu::performance {

/**
 * Fallback strategy levels - from most aggressive to most conservative
 */
enum class FallbackLevel {
    None,           // No fallback - use optimized configuration
    Conservative,   // Slightly reduced performance parameters
    Safe,           // Reduced parameters with increased error checking
    Minimal,        // Basic configuration with minimal optimization
    Reference       // Fallback to reference CPU implementation
};

/**
 * Types of failures that can trigger fallbacks
 */
enum class FailureType {
    CudaDriverFailure,     // CUDA driver errors
    OutOfMemory,          // GPU memory exhaustion
    ThermalThrottling,    // GPU overheating
    HardwareTimeout,      // GPU unresponsive
    ComputeError,         // Computation errors/incorrect results
    SynchronizationError, // CUDA synchronization failures
    StreamError,          // CUDA stream errors
    KernelLaunchFailure,  // Kernel launch failures
    ValidationFailure,    // Accuracy validation failures
    Unknown               // Unclassified failure
};

/**
 * Fallback configuration parameters
 */
struct FallbackConfig {
    FallbackLevel level = FallbackLevel::None;
    int points_per_thread = 256;              // Default aggressive setting
    int block_size = 1024;                    // Default aggressive setting
    int max_concurrent_streams = 8;           // Default aggressive setting
    size_t memory_pool_size_mb = 4096;        // Default memory pool
    bool enable_async_operations = true;      // Enable async optimizations
    bool enable_memory_optimization = true;  // Enable memory optimizations
    bool enable_compute_optimization = true;  // Enable compute optimizations
    double max_occupancy_ratio = 0.9;         // Target GPU occupancy
    int retry_attempts = 3;                   // Number of retry attempts
    std::chrono::milliseconds retry_delay{100}; // Delay between retries

    // Performance targets for fallback levels
    double target_throughput_mkeys_per_sec = 100.0; // Optimized target
    double min_acceptable_throughput_mkeys_per_sec = 10.0; // Minimum acceptable

    // Resource limits for fallback levels
    double max_memory_utilization_percent = 85.0; // Memory usage limit
    double max_gpu_utilization_percent = 95.0;    // GPU utilization limit
    double max_temperature_celsius = 85.0;        // Temperature limit

    json ToJson() const {
        json config;
        config["level"] = static_cast<int>(level);
        config["points_per_thread"] = points_per_thread;
        config["block_size"] = block_size;
        config["max_concurrent_streams"] = max_concurrent_streams;
        config["memory_pool_size_mb"] = memory_pool_size_mb;
        config["enable_async_operations"] = enable_async_operations;
        config["enable_memory_optimization"] = enable_memory_optimization;
        config["enable_compute_optimization"] = enable_compute_optimization;
        config["max_occupancy_ratio"] = max_occupancy_ratio;
        config["retry_attempts"] = retry_attempts;
        config["retry_delay_ms"] = retry_delay.count();
        config["target_throughput_mkeys_per_sec"] = target_throughput_mkeys_per_sec;
        config["min_acceptable_throughput_mkeys_per_sec"] = min_acceptable_throughput_mkeys_per_sec;
        config["max_memory_utilization_percent"] = max_memory_utilization_percent;
        config["max_gpu_utilization_percent"] = max_gpu_utilization_percent;
        config["max_temperature_celsius"] = max_temperature_celsius;
        return config;
    }
};

/**
 * Fallback strategy definition
 */
struct FallbackStrategy {
    FailureType failure_type;
    FallbackLevel target_level;
    std::string description;
    std::function<bool(const FallbackConfig&)> condition;
    std::function<FallbackConfig(const FallbackConfig&, const std::string&)> action;
    bool is_automatic = true;
    std::chrono::seconds cooldown_period{30};
    double success_threshold = 0.8;
};

/**
 * Fallback execution result
 */
struct FallbackResult {
    bool fallback_triggered = false;
    FailureType failure_type = FailureType::Unknown;
    FallbackLevel from_level = FallbackLevel::None;
    FallbackLevel to_level = FallbackLevel::None;
    std::string failure_reason;
    std::chrono::steady_clock::time_point timestamp;
    std::string strategy_used;
    bool recovery_successful = false;
    std::chrono::microseconds recovery_time;
    json additional_info;

    FallbackResult() : timestamp(std::chrono::steady_clock::now()),
                       recovery_time(std::chrono::microseconds(0)) {}
};

/**
 * Fallback Manager Interface
 *
 * Provides intelligent fallback mechanisms for GPU operations during
 * aggressive optimization scenarios, ensuring system stability and
 * graceful degradation when hardware limits are reached.
 */
class FallbackManager {
public:
    FallbackManager() = default;
    virtual ~FallbackManager() = default;

    // Core fallback management
    virtual FallbackResult HandleFailure(FailureType failure_type,
                                       const std::string& context,
                                       const FallbackConfig& current_config) = 0;
    virtual FallbackConfig GetOptimalConfig(FallbackLevel level) const = 0;
    virtual bool CanRecoverToHigherLevel(FallbackLevel current_level,
                                       FailureType last_failure_type) const = 0;

    // Configuration management
    virtual void SetBaseConfig(const FallbackConfig& config) = 0;
    virtual FallbackConfig GetCurrentConfig() const = 0;
    virtual FallbackLevel GetCurrentLevel() const = 0;
    virtual void SetFallbackLevel(FallbackLevel level) = 0;

    // Strategy management
    virtual void RegisterFallbackStrategy(const FallbackStrategy& strategy) = 0;
    virtual void UnregisterFallbackStrategy(FailureType failure_type) = 0;
    virtual std::vector<FallbackStrategy> GetActiveStrategies() const = 0;

    // Monitoring and validation
    virtual bool ShouldTriggerFallback(const FallbackConfig& config) const = 0;
    virtual bool ValidateConfig(const FallbackConfig& config) const = 0;
    virtual std::vector<std::string> GetValidationWarnings(const FallbackConfig& config) const = 0;

    // Recovery and health monitoring
    virtual bool AttemptRecovery() = 0;
    virtual bool IsSystemHealthy() const = 0;
    virtual std::vector<FallbackResult> GetFallbackHistory(size_t count = 10) const = 0;
    virtual FallbackResult GetLastFallbackResult() const = 0;

    // Statistics
    virtual double GetFallbackSuccessRate() const = 0;
    virtual std::map<FailureType, int> GetFailureCounts() const = 0;
    virtual std::chrono::steady_clock::time_point GetLastFailureTime() const = 0;

    // Integration with other performance components
    virtual void SetErrorHandler(ErrorHandler* error_handler) = 0;
    virtual void SetResourceProfiler(ResourceProfiler* profiler) = 0;
    virtual void SetMetricsCollector(MetricsCollector* collector) = 0;

    // Configuration presets
    virtual FallbackConfig GetAggressiveConfig() const = 0;
    virtual FallbackConfig GetBalancedConfig() const = 0;
    virtual FallbackConfig GetConservativeConfig() const = 0;
    virtual FallbackConfig GetSafeConfig() const = 0;
    virtual FallbackConfig GetReferenceConfig() const = 0;

    // Factory method
    static std::unique_ptr<FallbackManager> Create(int gpu_id = 0);

protected:
    FallbackConfig base_config_;
    FallbackLevel current_level_ = FallbackLevel::None;
    std::vector<FallbackStrategy> strategies_;
    std::vector<FallbackResult> fallback_history_;
    std::map<FailureType, int> failure_counts_;
    std::chrono::steady_clock::time_point last_failure_time_;
    mutable std::mutex fallback_mutex_;

    ErrorHandler* error_handler_ = nullptr;
    ResourceProfiler* resource_profiler_ = nullptr;
    MetricsCollector* metrics_collector_ = nullptr;
    int gpu_id_ = 0;
};

/**
 * GPU Fallback Manager Implementation
 *
 * Intelligent fallback system that monitors GPU health and performance,
 automatically degrading to safer configurations when issues are detected.
 */
class GpuFallbackManager : public FallbackManager {
public:
    explicit GpuFallbackManager(int gpu_id = 0);
    ~GpuFallbackManager() override = default;

    // FallbackManager interface implementation
    FallbackResult HandleFailure(FailureType failure_type,
                               const std::string& context,
                               const FallbackConfig& current_config) override;
    FallbackConfig GetOptimalConfig(FallbackLevel level) const override;
    bool CanRecoverToHigherLevel(FallbackLevel current_level,
                               FailureType last_failure_type) const override;

    void SetBaseConfig(const FallbackConfig& config) override;
    FallbackConfig GetCurrentConfig() const override;
    FallbackLevel GetCurrentLevel() const override;
    void SetFallbackLevel(FallbackLevel level) override;

    void RegisterFallbackStrategy(const FallbackStrategy& strategy) override;
    void UnregisterFallbackStrategy(FailureType failure_type) override;
    std::vector<FallbackStrategy> GetActiveStrategies() const override;

    bool ShouldTriggerFallback(const FallbackConfig& config) const override;
    bool ValidateConfig(const FallbackConfig& config) const override;
    std::vector<std::string> GetValidationWarnings(const FallbackConfig& config) const override;

    bool AttemptRecovery() override;
    bool IsSystemHealthy() const override;
    std::vector<FallbackResult> GetFallbackHistory(size_t count = 10) const override;
    FallbackResult GetLastFallbackResult() const override;

    double GetFallbackSuccessRate() const override;
    std::map<FailureType, int> GetFailureCounts() const override;
    std::chrono::steady_clock::time_point GetLastFailureTime() const override;

    void SetErrorHandler(ErrorHandler* error_handler) override;
    void SetResourceProfiler(ResourceProfiler* profiler) override;
    void SetMetricsCollector(MetricsCollector* collector) override;

    FallbackConfig GetAggressiveConfig() const override;
    FallbackConfig GetBalancedConfig() const override;
    FallbackConfig GetConservativeConfig() const override;
    FallbackConfig GetSafeConfig() const override;
    FallbackConfig GetReferenceConfig() const override;

private:
    // Internal methods
    void InitializeDefaultStrategies();
    void InitializeBaseConfigs();
    FallbackStrategy CreateStrategyForFailure(FailureType failure_type);
    FallbackConfig ApplyFallbackLevel(FallbackLevel level) const;
    bool IsInCooldownPeriod(FailureType failure_type) const;
    void UpdateFallbackHistory(const FallbackResult& result);
    void RecordFailure(FailureType failure_type);

    // Failure detection methods
    bool DetectCudaDriverFailure() const;
    bool DetectMemoryExhaustion() const;
    bool DetectThermalThrottling() const;
    bool DetectHardwareTimeout() const;
    bool DetectComputeErrors() const;
    bool DetectSynchronizationErrors() const;
    bool DetectStreamErrors() const;
    bool DetectKernelLaunchFailures() const;

    // Health monitoring
    bool CheckCudaHealth() const;
    bool CheckMemoryHealth() const;
    bool CheckThermalHealth() const;
    bool CheckPerformanceHealth() const;

    // Configuration validation
    bool ValidateBlockSize(int block_size) const;
    bool ValidatePointsPerThread(int points_per_thread) const;
    bool ValidateMemoryUsage(size_t memory_mb) const;
    bool ValidateOccupancyRatio(double ratio) const;

    // Fallback level configuration presets
    std::map<FallbackLevel, FallbackConfig> level_configs_;

    // Cooldown tracking
    std::map<FailureType, std::chrono::steady_clock::time_point> last_failure_time_by_type_;

    // Statistics tracking
    std::atomic<int> total_fallbacks_{0};
    std::atomic<int> successful_recoveries_{0};
};

/**
 * RAII Fallback Guard for automatic fallback handling
 */
class FallbackGuard {
public:
    FallbackGuard(GpuFallbackManager* manager, const FallbackConfig& config);
    ~FallbackGuard();

    bool IsHealthy() const;
    FallbackResult GetLastResult() const;
    FallbackConfig GetCurrentConfig() const;

    // Disable copying
    FallbackGuard(const FallbackGuard&) = delete;
    FallbackGuard& operator=(const FallbackGuard&) = delete;

private:
    GpuFallbackManager* manager_;
    FallbackConfig initial_config_;
    FallbackResult last_result_;
    bool monitoring_active_;
};

// Utility functions
namespace fallback_utils {
    std::string FailureTypeToString(FailureType type);
    FailureType StringToFailureType(const std::string& str);
    std::string FallbackLevelToString(FallbackLevel level);
    FallbackLevel StringToFallbackLevel(const std::string& str);

    FallbackConfig CreateConfigFromJson(const json& j);
    json FallbackResultToJson(const FallbackResult& result);
    std::string FormatFallbackReport(const FallbackResult& result);

    bool IsCudaErrorRecoverable(cudaError_t error);
    FailureType CudaErrorToFailureType(cudaError_t error);
    std::vector<FallbackLevel> GetRecoveryPath(FallbackLevel from, FallbackLevel to);
}

} // namespace puzzle71::gpu::performance