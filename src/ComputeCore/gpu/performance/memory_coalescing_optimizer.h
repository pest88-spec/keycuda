#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

namespace keycuda {
namespace gpu {
namespace performance {

/**
 * @brief CUDA kernel memory access pattern optimization for better coalescing
 *
 * Provides comprehensive memory coalescing optimization for GPU kernels:
 * - Automatic memory access pattern analysis
 * - Coalescing efficiency calculation and improvement
 * - Memory layout optimization suggestions
 * - Access pattern transformation utilities
 * - Performance impact measurement
 * - Architecture-specific optimization recommendations
 */

enum class AccessPatternType {
    SEQUENTIAL,
    STRIDED,
    RANDOM,
    IRREGULAR,
    COALESCED,
    UNCOALESCED,
    STRUCTURED_ARRAY,
    STRUCT_OF_ARRAYS
};

enum class CoalescingStrategy {
    NONE,           // No optimization
    AUTO_TRANSFORM, // Automatic transformation
    RESTRUCTURE_DATA, // Restructure data layout
    TILE_ACCESS,    // Tile-based access patterns
    PREFETCH_AWARE,  // Prefetch-aware access
    SHARED_MEMORY   // Shared memory optimization
};

struct AccessPattern {
    AccessPatternType type;
    size_t stride_size;
    size_t access_size;
    std::vector<size_t> thread_offsets;
    std::vector<size_t> warp_offsets;
    double locality_score;
    double sequentiality_score;
    std::chrono::microseconds estimated_latency;

    // GPU-specific metrics
    double coalescing_efficiency;
    int bank_conflicts;
    double shared_memory_utilization;
    double l2_cache_hit_rate;
};

struct CoalescingMetrics {
    // Overall efficiency
    double coalescing_efficiency;
    double theoretical_maximum_efficiency;
    double improvement_potential;

    // Memory access statistics
    int total_accesses;
    int coalesced_accesses;
    int uncoalesced_accesses;
    double uncoalescing_penalty;

    // Warp-level analysis
    std::vector<double> warp_efficiencies;
    double average_warp_efficiency;
    double worst_warp_efficiency;
    int active_warps;

    // Performance impact
    std::chrono::microseconds memory_latency;
    std::chrono::microseconds coalescing_overhead;
    double bandwidth_utilization;
    double occupancy_impact;

    // Architecture-specific
    double l2_cache_effectiveness;
    double shared_memory_bank_conflict_rate;
    int memory_divergence_events;

    std::chrono::system_clock::time_point timestamp;
};

struct OptimizationRecommendation {
    std::string description;
    CoalescingStrategy strategy;
    double expected_improvement;
    std::chrono::microseconds implementation_effort;
    std::vector<std::string> code_changes;
    bool architecture_specific;
    std::string target_architecture;
};

struct MemoryLayoutAnalysis {
    std::vector<AccessPattern> patterns;
    CoalescingMetrics current_metrics;
    std::vector<OptimizationRecommendation> recommendations;

    // Layout optimization suggestions
    bool suggest_struct_of_arrays;
    bool suggest_data_padding;
    bool suggest_memory_alignment;
    size_t recommended_alignment;
    size_t recommended_padding;

    // Access pattern suggestions
    bool suggest_loop_tiling;
    bool suggest_thread_reorganization;
    bool prefer_shared_memory;
    size_t optimal_tile_size;
    int optimal_block_size;
};

class MemoryCoalescingOptimizer {
public:
    explicit MemoryCoalescingOptimizer(int device_id = 0);
    ~MemoryCoalescingOptimizer();

    // Access pattern analysis
    AccessPattern AnalyzeAccessPattern(
        const void* device_ptr,
        size_t data_size,
        size_t element_size,
        int block_size = 256,
        int grid_size = 1
    );

    std::vector<AccessPattern> AnalyzeKernelAccessPatterns(
        const std::vector<void*>& device_ptrs,
        const std::vector<size_t>& data_sizes,
        const std::vector<size_t>& element_sizes,
        int block_size = 256
    );

    CoalescingMetrics CalculateCoalescingEfficiency(
        const AccessPattern& pattern,
        int compute_capability = 86
    );

    // Optimization recommendations
    std::vector<OptimizationRecommendation> GenerateOptimizationRecommendations(
        const AccessPattern& pattern,
        const CoalescingMetrics& metrics,
        CoalescingStrategy max_strategy = CoalescingStrategy::SHARED_MEMORY
    );

    MemoryLayoutAnalysis PerformLayoutAnalysis(
        const void* device_ptr,
        size_t data_size,
        size_t element_size,
        const std::string& data_structure_name = "unknown"
    );

    // Access pattern transformation
    std::vector<size_t> TransformForCoalescing(
        const AccessPattern& pattern,
        CoalescingStrategy strategy,
        int block_size = 256
    );

    std::string GenerateOptimizedKernelCode(
        const std::string& kernel_name,
        const AccessPattern& pattern,
        const OptimizationRecommendation& recommendation
    );

    // Performance measurement
    CoalescingMetrics MeasureCoalescingPerformance(
        const void* device_ptr,
        size_t data_size,
        const std::vector<size_t>& access_offsets,
        int block_size = 256,
        int iterations = 100
    );

    std::chrono::microseconds EstimateCoalescingImprovement(
        const CoalescingMetrics& current,
        const OptimizationRecommendation& recommendation
    );

    // Memory layout optimization
    bool OptimizeDataLayout(
        void* host_data,
        void* device_data,
        size_t element_count,
        size_t element_size,
        const MemoryLayoutAnalysis& analysis
    );

    void* CreateCoalescedLayout(
        const void* original_data,
        size_t element_count,
        size_t element_size,
        const std::vector<size_t>& access_pattern
    );

    // Architecture-specific optimization
    std::vector<OptimizationRecommendation> GetArchitectureSpecificOptimizations(
        int compute_capability,
        const AccessPattern& pattern
    );

    bool IsArchitectureOptimized(const AccessPattern& pattern, int compute_capability) const;

    // Shared memory optimization
    struct SharedMemoryConfig {
        bool enable_shared_memory = true;
        size_t shared_memory_size = 48 * 1024; // 48KB default
        int tile_size_x = 32;
        int tile_size_y = 8;
        bool use_read_only_cache = true;
        size_t cache_line_size = 128;
    };

    SharedMemoryConfig CalculateOptimalSharedMemoryConfig(
        const AccessPattern& pattern,
        int compute_capability = 86
    );

    std::string GenerateSharedMemoryCode(
        const std::string& kernel_name,
        const SharedMemoryConfig& config,
        const AccessPattern& pattern
    );

    // Validation and testing
    bool ValidateCoalescingImprovement(
        const void* original_data,
        const void* optimized_data,
        size_t data_size,
        const std::vector<size_t>& access_offsets,
        double min_improvement = 0.1
    );

    bool TestCoalescingStability(
        const AccessPattern& pattern,
        int test_iterations = 1000,
        double variance_threshold = 0.05
    );

    // Analytics and reporting
    json GetCoalescingAnalytics() const;
    std::string GenerateCoalescingReport() const;
    void ExportOptimizationPlan(const std::string& filename) const;

    // Configuration
    struct OptimizerConfig {
        bool enable_profiling = true;
        bool enable_auto_optimization = true;
        double min_improvement_threshold = 0.05;
        int max_optimization_attempts = 10;
        bool enable_detailed_analysis = true;
        std::chrono::microseconds profiling_timeout{1000000}; // 1 second
        CoalescingStrategy max_strategy = CoalescingStrategy::SHARED_MEMORY;
    };

    void UpdateConfiguration(const OptimizerConfig& config);
    OptimizerConfig GetCurrentConfiguration() const;

    // Error handling
    enum class ErrorType {
        NONE = 0,
        CUDA_ERROR,
        INVALID_PATTERN,
        INSUFFICIENT_MEMORY,
        OPTIMIZATION_FAILED,
        VALIDATION_ERROR,
        ARCHITECTURE_NOT_SUPPORTED
    };

    ErrorType GetLastError() const;
    std::string GetErrorMessage() const;
    bool AttemptErrorRecovery();

private:
    int device_id_;
    OptimizerConfig config_;
    ErrorType last_error_;
    std::string last_error_message_;

    // Device properties
    cudaDeviceProp device_properties_;
    size_t warp_size_;
    size_t l2_cache_size_;
    size_t shared_memory_per_block_;
    int max_threads_per_block_;

    // Performance cache
    std::map<std::string, CoalescingMetrics> pattern_cache_;
    std::map<int, std::vector<OptimizationRecommendation>> architecture_recommendations_;

    // Analysis history
    std::vector<MemoryLayoutAnalysis> analysis_history_;
    std::vector<CoalescingMetrics> performance_history_;

    // Internal methods
    void InitializeDeviceProperties();
    bool ValidateAccessPattern(const AccessPattern& pattern) const;

    // Coalescing calculation
    double CalculateWarpCoalescing(const AccessPattern& pattern, int warp_id) const;
    double CalculateTransactionEfficiency(const AccessPattern& pattern) const;
    int CalculateBankConflicts(const AccessPattern& pattern) const;

    // Pattern transformation
    std::vector<size_t> GenerateCoalescedOffsets(
        const AccessPattern& pattern,
        int block_size,
        CoalescingStrategy strategy
    );

    std::vector<size_t> GenerateStridedOffsets(
        size_t base_offset,
        size_t stride,
        int count,
        int block_size
    );

    std::vector<size_t> GenerateTiledOffsets(
        size_t base_offset,
        size_t tile_size,
        int elements_per_tile,
        int block_size
    );

    // Shared memory optimization
    size_t CalculateOptimalTileSize(
        const AccessPattern& pattern,
        size_t shared_memory_limit
    ) const;

    bool CanFitInSharedMemory(
        const AccessPattern& pattern,
        const SharedMemoryConfig& config
    ) const;

    // Performance estimation
    std::chrono::microseconds EstimateMemoryAccessTime(
        const AccessPattern& pattern,
        double bandwidth_utilization = 0.8
    ) const;

    double EstimateBandwidthUtilization(
        const AccessPattern& pattern,
        int compute_capability
    ) const;

    // Architecture-specific logic
    bool SupportsReadOnlyCache(int compute_capability) const;
    size_t GetCacheLineSize(int compute_capability) const;
    double GetL2CacheLatency(int compute_capability) const;

    // Error handling
    void SetError(ErrorType error, const std::string& message);
    bool RecoverFromError(ErrorType error);

    // Constants
    static constexpr size_t DEFAULT_WARP_SIZE = 32;
    static constexpr size_t DEFAULT_CACHE_LINE_SIZE = 128;
    static constexpr double COALESCING_PENALTY_FACTOR = 0.75;
    static constexpr int MAX_TILES_PER_BLOCK = 16;
};

/**
 * @brief Factory function to create memory coalescing optimizer instance
 */
std::unique_ptr<MemoryCoalescingOptimizer> CreateMemoryCoalescingOptimizer(
    int device_id = 0
);

} // namespace performance
} // namespace gpu
} // namespace keycuda