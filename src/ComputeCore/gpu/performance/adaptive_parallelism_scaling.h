#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include <chrono>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include <cuda_runtime.h>

// Forward declarations
namespace keycuda {
namespace gpu {
namespace performance {
class OccupancyCalculator;
class ConfigurationLogger;
}
}
}

namespace keycuda {
namespace gpu {
namespace performance {

/**
 * @brief Adaptive parallelism scaling system for GPU optimization
 *
 * Automatically adjusts kernel parallelism parameters based on available GPU resources
 * and workload characteristics to achieve optimal performance:
 * - GPU architecture detection and capability profiling
 * - Occupancy-based block size selection (256-1024 threads)
 * - Memory-aware parallelism scaling
 * - Dynamic points_per_thread adjustment (64-256 range)
 * - Real-time performance feedback and optimization
 * - Automatic fallback for resource constraints
 */

struct GpuCapabilities {
    int device_id;
    std::string device_name;
    int compute_capability;  // e.g., 86, 89, 90
    size_t total_memory_mb;
    size_t free_memory_mb;
    int sm_count;             // Streaming multiprocessors
    int max_threads_per_sm;
    int max_threads_per_block;
    size_t shared_memory_per_block;
    size_t total_shared_memory;
    size_t l2_cache_size_kb;
    double memory_bandwidth_gb_per_sec;
    double clock_rate_mhz;
    int warp_size;
    int max_blocks_per_sm;
    int max_registers_per_thread;
    bool supports_managed_memory;
    bool supports_cooperative_groups;
    bool supports_async_copy;
};

struct ParallelismConfiguration {
    int points_per_thread;              // 64-256 range
    int block_size;                    // 256-1024 threads
    int grid_size;                     // Number of blocks
    size_t shared_memory_size;          // Per block
    int registers_per_thread;           // Estimated
    double expected_occupancy;          // 0.0-1.0
    double memory_utilization_estimate; // 0.0-1.0
    std::chrono::microseconds estimated_execution_time;
    std::string configuration_rationale;
    json optimization_metrics;
};

struct ScalingDecision {
    ParallelismConfiguration selected_config;
    std::vector<ParallelismConfiguration> alternatives;
    std::string decision_logic;
    double confidence_score;           // 0.0-1.0
    std::chrono::system_clock::time_point decision_time;
    GpuCapabilities gpu_capabilities;
    std::vector<std::string> constraints_applied;
    bool automatic_scaling_enabled;
};

struct PerformanceFeedback {
    double actual_throughput_mkeys_per_sec;
    double actual_occupancy;
    double actual_memory_utilization;
    std::chrono::microseconds actual_execution_time;
    bool accuracy_maintained;
    std::vector<std::string> performance_issues;
    std::vector<std::string> optimization_suggestions;
    double performance_score;           // 0.0-1.0
};

class AdaptiveParallelismScaling {
public:
    explicit AdaptiveParallelismScaling(int device_id);
    ~AdaptiveParallelismScaling();

    // Core scaling operations
    ScalingDecision CalculateOptimalConfiguration(
        size_t workload_size,
        const std::string& operation_type = "key_search"
    );

    ParallelismConfiguration GetRecommendedConfiguration(
        const GpuCapabilities& gpu_caps,
        size_t workload_size,
        const std::map<std::string, std::string>& constraints = {}
    );

    // Performance feedback integration
    void UpdatePerformanceFeedback(
        const ParallelismConfiguration& config,
        const PerformanceFeedback& feedback
    );

    void RecordPerformanceResult(
        const ParallelismConfiguration& config,
        double throughput_mkeys_per_sec,
        std::chrono::microseconds execution_time,
        bool accuracy_maintained = true
    );

    // Architecture detection
    GpuCapabilities DetectGpuCapabilities(int device_id) const;
    bool IsArchitectureSupported(const GpuCapabilities& caps) const;
    std::vector<GpuCapabilities> DetectAllGpus() const;

    // Dynamic scaling methods
    ScalingDecision AdaptConfiguration(
        const ParallelismConfiguration& current_config,
        const PerformanceFeedback& recent_feedback
    );

    ParallelismConfiguration ScaleForMemoryConstraints(
        const ParallelismConfiguration& base_config,
        size_t available_memory_mb
    );

    ParallelismConfiguration ScaleForThroughputTarget(
        const ParallelismConfiguration& base_config,
        double target_throughput_mkeys_per_sec
    );

    // Validation and testing
    bool ValidateConfiguration(const ParallelismConfiguration& config) const;
    std::vector<std::string> GetConfigurationWarnings(const ParallelismConfiguration& config) const;
    bool TestConfiguration(const ParallelismConfiguration& config, size_t test_size = 1000);

    // Occupancy-based optimization methods
    ParallelismConfiguration OptimizeForOccupancy(
        const ParallelismConfiguration& base_config,
        double target_occupancy = 0.75
    ) const;

    std::vector<int> GetOptimalBlockSizesOccupancyBased(
        const ParallelismConfiguration& base_config
    ) const;

    double CalculateConfigurationOccupancy(
        const ParallelismConfiguration& config
    ) const;

    ParallelismConfiguration GetOccupancyOptimizedConfiguration(
        size_t workload_size,
        const std::string& operation_type = "key_search"
    ) const;

    // Learning and optimization
    void EnableLearningMode(bool enabled);
    void SetOptimizationStrategy(const std::string& strategy);
    std::map<std::string, double> GetPerformanceHistory() const;
    std::vector<ParallelismConfiguration> GetOptimalConfigurations() const;

    // Configuration management
    void SetConfigurationConstraints(const std::map<std::string, std::string>& constraints);
    void SetPerformanceTargets(double min_throughput, double max_memory_utilization);
    void EnableAutomaticScaling(bool enabled);
    void SetScalingAggressiveness(double aggressiveness); // 0.0-1.0

    // Analytics and reporting
    json GetPerformanceAnalytics() const;
    std::string GenerateConfigurationReport() const;
    std::vector<std::string> GetOptimizationRecommendations() const;
    double GetScalingEffectiveness() const;

    // Configuration decision logging
    void EnableConfigurationLogging(bool enabled);
    std::string GetConfigurationLogPath() const;
    std::string ExportConfigurationDecisions(std::chrono::hours time_window = std::chrono::hours(24)) const;
    void SetConfigurationLogLevel(int log_level);  // 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR, 4=CRITICAL
    std::string GetConfigurationDecisionReport() const;

    // Fallback and safety
    ParallelismConfiguration GetSafeConfiguration() const;
    bool HasConfigurationFailed(const ParallelismConfiguration& config) const;
    void MarkConfigurationAsFailed(const ParallelismConfiguration& config);
    std::vector<ParallelismConfiguration> GetFallbackConfigurations() const;

    // Advanced fallback management
    ParallelismConfiguration ActivateAutomaticFallback(
        const std::string& failure_reason,
        const ParallelismConfiguration& failed_config,
        size_t workload_size = 1000000);

    bool DetectResourceConstraints(
        const ParallelismConfiguration& config,
        size_t workload_size,
        std::string& constraint_type) const;

    ParallelismConfiguration GetResourceConstraintAwareFallback(
        const std::string& constraint_type,
        size_t workload_size = 1000000) const;

private:
    int target_device_id_;
    GpuCapabilities current_gpu_capabilities_;
    bool learning_mode_enabled_;
    bool automatic_scaling_enabled_;
    double scaling_aggressiveness_;

    // Components for adaptive scaling
    std::unique_ptr<OccupancyCalculator> occupancy_calculator_;
    std::unique_ptr<ConfigurationLogger> configuration_logger_;

    mutable std::mutex scaling_mutex_;

    // Performance tracking
    std::map<std::string, std::vector<std::pair<ParallelismConfiguration, PerformanceFeedback>>> performance_history_;
    std::set<ParallelismConfiguration> failed_configurations_;
    std::map<std::string, double> optimization_weights_;

    // Configuration constraints
    std::map<std::string, std::string> current_constraints_;
    double min_throughput_target_;
    double max_memory_utilization_target_;
    std::string optimization_strategy_;

    // Learning data
    std::map<std::string, double> architecture_performance_cache_;
    std::vector<ScalingDecision> decision_history_;

    // Performance monitoring
    std::chrono::steady_clock::time_point start_time_;
    std::vector<std::chrono::microseconds> logging_latencies_;
    double total_logging_overhead_;

    enum class IntegrityStatus { VALID, CORRUPTED, RECOVERING };
    mutable IntegrityStatus last_integrity_status_;

    std::thread verification_thread_;
    std::atomic<bool> stop_verification_;

    // Internal methods
    std::vector<ParallelismConfiguration> GenerateCandidateConfigurations(
        const GpuCapabilities& gpu_caps,
        size_t workload_size
    ) const;

    double EstimateConfigurationPerformance(
        const ParallelismConfiguration& config,
        const GpuCapabilities& gpu_caps,
        size_t workload_size
    ) const;

    double CalculateOccupancy(
        const ParallelismConfiguration& config,
        const GpuCapabilities& gpu_caps
    ) const;

    double EstimateMemoryUsage(
        const ParallelismConfiguration& config,
        size_t workload_size
    ) const;

    double EstimateExecutionTime(
        const ParallelismConfiguration& config,
        const GpuCapabilities& gpu_caps,
        size_t workload_size
    ) const;

    bool ApplyConstraints(
        ParallelismConfiguration& config,
        const std::map<std::string, std::string>& constraints
    ) const;

    void UpdateLearningModel(
        const ParallelismConfiguration& config,
        const PerformanceFeedback& feedback
    );

    std::string SelectOptimalBlockSizes(const GpuCapabilities& gpu_caps) const;
    std::vector<int> CalculateOptimalBlockSizesOccupancyBased(const GpuCapabilities& gpu_caps) const;
    double EstimateOccupancyForBlockSize(int block_size, const GpuCapabilities& gpu_caps) const;
    int SelectOptimalBlockSizeForWorkload(
        const GpuCapabilities& gpu_caps,
        size_t workload_size,
        size_t available_memory_mb
    ) const;
    std::vector<int> SelectOptimalPointsPerThread(
        const GpuCapabilities& gpu_caps,
        size_t workload_size
    ) const;

    // Occupancy calculation helpers
    OccupancyParameters ConvertConfigToOccupancyParams(
        const ParallelismConfiguration& config
    ) const;
    ParallelismConfiguration ConvertOccupancyToConfig(
        const OccupancyMetrics& metrics,
        const ParallelismConfiguration& base_config
    ) const;
    double EstimateConfigOccupancy(
        const ParallelismConfiguration& config,
        const OccupancyParameters& params
    ) const;
    bool IsConfigurationOccupancyOptimal(
        const ParallelismConfiguration& config,
        double target_occupancy = 0.75
    ) const;

    bool IsWorkloadMemoryBound(
        size_t workload_size,
        const std::string& operation_type
    ) const;

    // Architecture-specific optimizations
    ParallelismConfiguration OptimizeForHopper(const GpuCapabilities& caps, size_t workload_size) const;
    ParallelismConfiguration OptimizeForAda(const GpuCapabilities& caps, size_t workload_size) const;
    ParallelismConfiguration OptimizeForAmpere(const GpuCapabilities& caps, size_t workload_size) const;
    ParallelismConfiguration OptimizeForTuring(const GpuCapabilities& caps, size_t workload_size) const;
    ParallelismConfiguration OptimizeForVolta(const GpuCapabilities& caps, size_t workload_size) const;

    // Memory-aware optimization methods
    ParallelismConfiguration ApplyMemoryAwareScaling(
        const ParallelismConfiguration& config,
        const GpuCapabilities& gpu_caps,
        size_t workload_size) const;
    size_t EstimateDetailedMemoryUsage(
        const ParallelismConfiguration& config,
        size_t workload_size) const;
    ParallelismConfiguration ApplyCriticalMemoryOptimization(
        const ParallelismConfiguration& config,
        const GpuCapabilities& gpu_caps,
        size_t workload_size) const;
    ParallelismConfiguration ApplyHighMemoryOptimization(
        const ParallelismConfiguration& config,
        const GpuCapabilities& gpu_caps,
        size_t workload_size) const;
    ParallelismConfiguration ApplyModerateMemoryOptimization(
        const ParallelismConfiguration& config,
        const GpuCapabilities& gpu_caps,
        size_t workload_size) const;
    ParallelismConfiguration ApplyPerformanceOptimization(
        const ParallelismConfiguration& config,
        const GpuCapabilities& gpu_caps,
        size_t workload_size) const;
    std::vector<ParallelismConfiguration> GenerateMemoryAwareConfigurations(
        const GpuCapabilities& gpu_caps,
        size_t workload_size,
        const std::map<std::string, std::string>& constraints) const;

    // Advanced memory-aware scaling methods
    ParallelismConfiguration DynamicMemoryScaling(
        const ParallelismConfiguration& base_config,
        size_t current_memory_usage_mb,
        size_t available_memory_mb,
        double performance_target
    ) const;

    std::vector<ParallelismConfiguration> GetMemoryConstrainedAlternatives(
        const ParallelismConfiguration& preferred_config,
        size_t max_memory_mb,
        size_t workload_size
    ) const;

    bool PredictMemoryExhaustion(
        const ParallelismConfiguration& config,
        size_t workload_size,
        double safety_margin = 0.1
    ) const;

    ParallelismConfiguration OptimizeForMemoryBandwidth(
        const ParallelismConfiguration& config,
        const GpuCapabilities& caps
    ) const;

    double GetMemoryEfficiencyScore(
        const ParallelismConfiguration& config,
        size_t workload_size
    ) const;

    // Validation helpers
    bool ValidateBlockSize(int block_size, const GpuCapabilities& caps) const;
    bool ValidatePointsPerThread(int ppt, const GpuCapabilities& caps) const;
    bool ValidateMemoryUsage(const ParallelismConfiguration& config, const GpuCapabilities& caps) const;
    bool ValidateRegisterUsage(const ParallelismConfiguration& config, const GpuCapabilities& caps) const;

    // Performance monitoring helpers
    void UpdatePerformanceMetrics(std::chrono::microseconds logging_time);
    double EstimateMemoryBandwidth(const cudaDeviceProp& prop) const;
    std::string GenerateConfigurationKey(const ParallelismConfiguration& config) const;
    std::string SelectOptimalBlockSizes(const GpuCapabilities& gpu_caps) const;
    std::vector<int> CalculateOptimalBlockSizesOccupancyBased(const GpuCapabilities& gpu_caps) const;
    double EstimateOccupancyForBlockSize(int block_size, const GpuCapabilities& gpu_caps) const;
    int SelectOptimalBlockSizeForWorkload(
        const GpuCapabilities& gpu_caps,
        size_t workload_size,
        size_t available_memory_mb) const;
    std::vector<int> SelectOptimalPointsPerThread(
        const GpuCapabilities& gpu_caps,
        size_t workload_size) const;

    // Performance estimation
    double GetArchitecturePerformanceFactor(const GpuCapabilities& caps) const;
    double GetMemoryBandwidthFactor(const ParallelismConfiguration& config) const;
    double GetOccupancyFactor(double occupancy) const;

    // Serialization
    json GpuCapabilitiesToJson(const GpuCapabilities& caps) const;
    json ParallelismConfigurationToJson(const ParallelismConfiguration& config) const;
    json ScalingDecisionToJson(const ScalingDecision& decision) const;
    json PerformanceFeedbackToJson(const PerformanceFeedback& feedback) const;
};

/**
 * @brief Factory function to create adaptive parallelism scaling instance
 */
std::unique_ptr<AdaptiveParallelismScaling> CreateAdaptiveParallelismScaling(
    int device_id,
    bool enable_learning = true,
    bool enable_automatic_scaling = true);

} // namespace performance
} // namespace gpu
} // namespace keycuda