#pragma once

#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <cuda_runtime.h>
#include "core/uint256.h"
#include "ComputeCore/gpu/performance/performance_logger.h"

namespace puzzle71::gpu::performance {

/**
 * Dynamic configuration for GPU kernel parameters
 */
struct DynamicKernelConfig {
    int points_per_thread = 128;                    // Primary parallelism factor
    int block_size = 512;                          // Threads per block
    int grid_size = 0;                             // Blocks in grid (0 = auto)
    double target_occupancy_ratio = 0.85;          // Target SM occupancy
    bool enable_adaptive_scaling = true;            // Auto-adjust based on performance
    int min_points_per_thread = 64;                 // Minimum for this configuration
    int max_points_per_thread = 256;                // Maximum for this configuration
    double performance_weight = 1.0;                // Relative performance importance
    std::string optimization_profile;              // Performance profile name

    json ToJson() const {
        json config;
        config["points_per_thread"] = points_per_thread;
        config["block_size"] = block_size;
        config["grid_size"] = grid_size;
        config["target_occupancy_ratio"] = target_occupancy_ratio;
        config["enable_adaptive_scaling"] = enable_adaptive_scaling;
        config["min_points_per_thread"] = min_points_per_thread;
        config["max_points_per_thread"] = max_points_per_thread;
        config["performance_weight"] = performance_weight;
        config["optimization_profile"] = optimization_profile;
        return config;
    }
};

/**
 * Performance metrics for configuration evaluation
 */
struct ConfigurationMetrics {
    double throughput_mkeys_per_sec = 0.0;          // Measured throughput
    double gpu_utilization_percent = 0.0;           // GPU utilization
    double memory_bandwidth_utilization = 0.0;       // Memory bandwidth usage
    double kernel_execution_time_ms = 0.0;          // Kernel execution time
    double power_efficiency_mkeys_per_watt = 0.0;   // Power efficiency
    std::chrono::steady_clock::time_point timestamp; // Measurement time
    bool accuracy_validated = false;                 // Correctness validation
    double error_rate_percent = 0.0;                // Error rate
    int thermal_throttling_events = 0;              // Thermal issues

    json ToJson() const {
        json metrics;
        metrics["throughput_mkeys_per_sec"] = throughput_mkeys_per_sec;
        metrics["gpu_utilization_percent"] = gpu_utilization_percent;
        metrics["memory_bandwidth_utilization"] = memory_bandwidth_utilization;
        metrics["kernel_execution_time_ms"] = kernel_execution_time_ms;
        metrics["power_efficiency_mkeys_per_watt"] = power_efficiency_mkeys_per_watt;
        metrics["accuracy_validated"] = accuracy_validated;
        metrics["error_rate_percent"] = error_rate_percent;
        metrics["thermal_throttling_events"] = thermal_throttling_events;
        return metrics;
    }
};

/**
 * Optimization profile for different scenarios
 */
struct OptimizationProfile {
    std::string name;
    std::string description;
    int min_points_per_thread;
    int max_points_per_thread;
    int preferred_block_size;
    double target_occupancy;
    std::vector<int> supported_compute_capabilities;
    bool requires_high_memory_bandwidth;
    bool requires_cooperative_groups;

    bool IsCompatible(int compute_capability) const {
        return std::find(supported_compute_capabilities.begin(),
                       supported_compute_capabilities.end(),
                       compute_capability) != supported_compute_capabilities.end();
    }
};

/**
 * Dynamic Configurator Interface
 *
 * Provides intelligent, adaptive configuration of GPU kernel parameters
 * based on hardware capabilities, workload characteristics, and performance feedback.
 */
class DynamicConfigurator {
public:
    DynamicConfigurator() = default;
    virtual ~DynamicConfigurator() = default;

    // Primary configuration methods
    virtual DynamicKernelConfig CalculateOptimalConfiguration(
        size_t workload_size,
        int gpu_id = 0
    ) = 0;

    virtual DynamicKernelConfig GetConfigurationForProfile(
        const std::string& profile_name,
        size_t workload_size
    ) = 0;

    // Adaptive configuration based on performance feedback
    virtual DynamicKernelConfig AdaptConfiguration(
        const DynamicKernelConfig& current_config,
        const ConfigurationMetrics& metrics
    ) = 0;

    virtual DynamicKernelConfig OptimizeForThroughput(
        size_t workload_size,
        double target_throughput_mkeys_per_sec = 0.0
    ) = 0;

    virtual DynamicKernelConfig OptimizeForPowerEfficiency(
        size_t workload_size
    ) = 0;

    // Configuration validation
    virtual bool ValidateConfiguration(const DynamicKernelConfig& config) const = 0;
    virtual std::vector<std::string> GetConfigurationWarnings(const DynamicKernelConfig& config) const = 0;

    // Performance measurement and feedback
    virtual void RecordConfigurationPerformance(
        const DynamicKernelConfig& config,
        const ConfigurationMetrics& metrics
    ) = 0;

    virtual std::vector<ConfigurationMetrics> GetPerformanceHistory(
        const DynamicKernelConfig& config,
        std::chrono::seconds duration = std::chrono::minutes(5)
    ) = 0;

    // Profile management
    virtual std::vector<OptimizationProfile> GetAvailableProfiles() const = 0;
    virtual OptimizationProfile GetProfile(const std::string& name) const = 0;
    virtual void RegisterCustomProfile(const OptimizationProfile& profile) = 0;

    // Architecture-specific optimization
    virtual DynamicKernelConfig OptimizeForArchitecture(
        int compute_capability,
        size_t workload_size
    ) = 0;

    virtual DynamicKernelConfig OptimizeForMemoryConstraints(
        size_t available_memory_mb,
        size_t workload_size
    ) = 0;

    // Factory method
    static std::unique_ptr<DynamicConfigurator> Create(int gpu_id = 0);

protected:
    int gpu_id_ = 0;
    std::map<std::string, std::vector<ConfigurationMetrics>> performance_history_;
    std::vector<OptimizationProfile> optimization_profiles_;
    mutable std::mutex configurator_mutex_;

    // Internal methods
    virtual void InitializeOptimizationProfiles() = 0;
    virtual DynamicKernelConfig CalculateBaseConfiguration(int compute_capability) const = 0;
    virtual double EstimatePerformance(const DynamicKernelConfig& config, size_t workload_size) const = 0;
};

// Concrete implementation
class GpuDynamicConfigurator : public DynamicConfigurator {
public:
    explicit GpuDynamicConfigurator(int gpu_id = 0);
    ~GpuDynamicConfigurator() override = default;

    // DynamicConfigurator interface implementation
    DynamicKernelConfig CalculateOptimalConfiguration(size_t workload_size, int gpu_id = 0) override;
    DynamicKernelConfig GetConfigurationForProfile(const std::string& profile_name, size_t workload_size) override;
    DynamicKernelConfig AdaptConfiguration(const DynamicKernelConfig& current_config, const ConfigurationMetrics& metrics) override;
    DynamicKernelConfig OptimizeForThroughput(size_t workload_size, double target_throughput_mkeys_per_sec = 0.0) override;
    DynamicKernelConfig OptimizeForPowerEfficiency(size_t workload_size) override;

    bool ValidateConfiguration(const DynamicKernelConfig& config) const override;
    std::vector<std::string> GetConfigurationWarnings(const DynamicKernelConfig& config) const override;

    void RecordConfigurationPerformance(const DynamicKernelConfig& config, const ConfigurationMetrics& metrics) override;
    std::vector<ConfigurationMetrics> GetPerformanceHistory(const DynamicKernelConfig& config, std::chrono::seconds duration) override;

    std::vector<OptimizationProfile> GetAvailableProfiles() const override;
    OptimizationProfile GetProfile(const std::string& name) const override;
    void RegisterCustomProfile(const OptimizationProfile& profile) override;

    DynamicKernelConfig OptimizeForArchitecture(int compute_capability, size_t workload_size) override;
    DynamicKernelConfig OptimizeForMemoryConstraints(size_t available_memory_mb, size_t workload_size) override;

private:
    // GPU capability detection
    int GetComputeCapability() const;
    size_t GetTotalMemoryMB() const;
    int GetSMCount() const;
    double GetMemoryBandwidthGBps() const;

    // Configuration optimization algorithms
    int CalculateOptimalPointsPerThread(size_t workload_size, int compute_capability) const;
    int CalculateOptimalBlockSize(int compute_capability, int points_per_thread) const;
    int CalculateOptimalGridSize(size_t workload_size, int block_size, int points_per_thread) const;

    // Performance estimation
    double EstimateThroughput(const DynamicKernelConfig& config, size_t workload_size) const;
    double EstimateMemoryBandwidthUtilization(const DynamicKernelConfig& config) const;
    double EstimatePowerConsumption(const DynamicKernelConfig& config) const;

    // Adaptive optimization
    DynamicKernelConfig PerformAdaptiveOptimization(const DynamicKernelConfig& config, const ConfigurationMetrics& metrics);
    std::vector<DynamicKernelConfig> GenerateConfigurationVariants(const DynamicKernelConfig& base_config) const;
    double CalculateConfigurationScore(const DynamicKernelConfig& config, const ConfigurationMetrics& target_metrics) const;

    // Profile management
    void InitializeOptimizationProfiles() override;
    DynamicKernelConfig CalculateBaseConfiguration(int compute_capability) const override;
    double EstimatePerformance(const DynamicKernelConfig& config, size_t workload_size) const override;

    // Architecture-specific configurations
    DynamicKernelConfig GetHopperConfiguration(size_t workload_size) const;
    DynamicKernelConfig GetAdaLovelaceConfiguration(size_t workload_size) const;
    DynamicKernelConfig GetAmpereConfiguration(size_t workload_size) const;
    DynamicKernelConfig GetTuringConfiguration(size_t workload_size) const;
    DynamicKernelConfig GetPascalConfiguration(size_t workload_size) const;

    // Constraint validation
    bool ValidatePointsPerThread(int ppt, int compute_capability) const;
    bool ValidateBlockSize(int block_size, int compute_capability) const;
    bool ValidateMemoryRequirements(const DynamicKernelConfig& config, size_t workload_size) const;

    // Historical analysis
    ConfigurationMetrics CalculateAverageMetrics(const std::vector<ConfigurationMetrics>& metrics) const;
    double CalculatePerformanceTrend(const std::vector<ConfigurationMetrics>& metrics) const;
};

// Utility functions
namespace dynamic_config_utils {
    std::string FormatConfiguration(const DynamicKernelConfig& config);
    std::string FormatMetrics(const ConfigurationMetrics& metrics);

    OptimizationProfile CreateHighPerformanceProfile();
    OptimizationProfile CreateBalancedProfile();
    OptimizationProfile CreatePowerEfficientProfile();
    OptimizationProfile CreateMemoryOptimizedProfile();

    bool IsConfigurationCompatible(const DynamicKernelConfig& config, int compute_capability);
    DynamicKernelConfig ClampConfigurationToLimits(const DynamicKernelConfig& config, int compute_capability);

    std::vector<int> GetRecommendedPointsPerThreadValues(int compute_capability);
    std::vector<int> GetRecommendedBlockSizes(int compute_capability);
}

} // namespace puzzle71::gpu::performance