#pragma once

#include <vector>
#include <memory>
#include <map>
#include <string>
#include <cuda_runtime.h>

namespace keycuda {
namespace gpu {
namespace performance {

/**
 * @brief Occupancy calculation and block size optimization
 *
 * Provides sophisticated occupancy-based block size selection algorithms
 * for optimal GPU performance across different architectures:
 * - Real-time occupancy calculation for different block configurations
 * - Architecture-specific optimization considerations
 * - Memory-aware block size selection
 * - Register and shared memory constraint analysis
 * - Optimal block size recommendations for target occupancy levels
 */

struct OccupancyMetrics {
    int block_size = 0;
    int blocks_per_sm = 0;
    double occupancy_percentage = 0.0;
    int active_warps_per_sm = 0;
    double active_threads_percentage = 0.0;
    int registers_per_thread = 0;
    size_t shared_memory_per_block = 0;
    bool limited_by_registers = false;
    bool limited_by_shared_memory = false;
    bool limited_by_threads = false;
    bool limited_by_blocks = false;
    std::string limiting_factor;
    double efficiency_score = 0.0;  // 0.0-1.0
};

struct BlockSizeRecommendation {
    int optimal_block_size = 256;
    std::vector<int> alternative_sizes;
    double target_occupancy = 0.75;
    double achieved_occupancy = 0.0;
    std::string recommendation_rationale;
    std::vector<std::string> constraints;
    bool is_memory_bound = false;
    bool is_compute_bound = false;
    double performance_potential = 0.0;
};

struct OccupancyParameters {
    int registers_per_thread = 32;
    size_t shared_memory_per_block = 0;
    int dynamic_shared_memory_bytes = 0;
    bool uses_dynamic_shared_memory = false;
    int max_blocks_per_sm = 32;  // CUDA default
    int max_threads_per_sm = 2048;  // Architecture dependent
    int max_threads_per_block = 1024;
    int warp_size = 32;
    size_t total_shared_memory = 65536;  // Per SM
    int total_registers = 65536;  // Per SM
};

class OccupancyCalculator {
public:
    OccupancyCalculator();
    explicit OccupancyCalculator(int device_id);
    ~OccupancyCalculator();

    // Core occupancy calculation
    OccupancyMetrics CalculateOccupancy(
        int block_size,
        const OccupancyParameters& params
    ) const;

    std::vector<OccupancyMetrics> CalculateOccupancyRange(
        const std::vector<int>& block_sizes,
        const OccupancyParameters& params
    ) const;

    // Block size optimization
    BlockSizeRecommendation GetOptimalBlockSize(
        const OccupancyParameters& params,
        double target_occupancy = 0.75
    ) const;

    std::vector<int> GetOptimalBlockSizes(
        const OccupancyParameters& params,
        size_t workload_size = 0
    ) const;

    // Advanced analysis
    std::map<int, double> CalculateOccupancyCurve(
        const OccupancyParameters& params
    ) const;

    double EstimateOptimalOccupancy(
        const OccupancyParameters& params,
        bool is_memory_bound = false
    ) const;

    // Constraint analysis
    std::string GetPrimaryLimitingFactor(
        int block_size,
        const OccupancyParameters& params
    ) const;

    bool IsRegisterLimited(
        int block_size,
        const OccupancyParameters& params
    ) const;

    bool IsSharedMemoryLimited(
        int block_size,
        const OccupancyParameters& params
    ) const;

    // Architecture-specific optimization
    OccupancyParameters GetArchitectureOptimizedParameters(
        int compute_capability,
        const std::string& kernel_type = "general"
    ) const;

    int GetOptimalBlockSizeForArchitecture(
        int compute_capability,
        const OccupancyParameters& params
    ) const;

    // Performance prediction
    double PredictRelativePerformance(
        int block_size,
        const OccupancyParameters& params,
        bool is_memory_bound = false
    ) const;

    std::vector<int> GetPerformanceOrderedBlockSizes(
        const OccupancyParameters& params,
        bool is_memory_bound = false
    ) const;

    // Configuration validation
    bool ValidateBlockSize(
        int block_size,
        const OccupancyParameters& params
    ) const;

    std::vector<std::string> GetConfigurationWarnings(
        int block_size,
        const OccupancyParameters& params
    ) const;

    // Utility methods
    void SetDevice(int device_id);
    int GetCurrentDevice() const { return device_id_; }
    OccupancyParameters GetDefaultParameters() const;

    // Static utility methods
    static int GetDefaultBlockSize(int compute_capability);
    static std::vector<int> GetCommonBlockSizes();
    static double GetMinimumRecommendedOccupancy();

private:
    int device_id_;
    mutable OccupancyParameters cached_params_;

    // Internal calculation methods
    int CalculateBlocksLimitedByThreads(
        int block_size,
        const OccupancyParameters& params
    ) const;

    int CalculateBlocksLimitedByRegisters(
        int block_size,
        const OccupancyParameters& params
    ) const;

    int CalculateBlocksLimitedBySharedMemory(
        int block_size,
        const OccupancyParameters& params
    ) const;

    int CalculateBlocksLimitedByBlocks(
        const OccupancyParameters& params
    ) const;

    double CalculateOccupancyPercentage(
        int blocks_per_sm,
        int block_size,
        const OccupancyParameters& params
    ) const;

    double CalculateEfficiencyScore(
        const OccupancyMetrics& metrics,
        const OccupancyParameters& params
    ) const;

    // Architecture-specific heuristics
    bool ShouldPreferHigherOccupancy(int compute_capability) const;
    double GetOccupancySweetSpot(int compute_capability) const;
    int GetOptimalWarpCountPerSM(int compute_capability) const;

    // Performance modeling
    double ModelMemoryPerformance(int block_size, double occupancy) const;
    double ModelComputePerformance(int block_size, double occupancy) const;

    // Helper methods
    OccupancyParameters GetDeviceParameters(int device_id) const;
    void UpdateCachedParameters() const;
    bool IsValidBlockSize(int block_size) const;
};

/**
 * @brief Factory function to create occupancy calculator
 */
std::unique_ptr<OccupancyCalculator> CreateOccupancyCalculator(int device_id = 0);

} // namespace performance
} // namespace gpu
} // namespace keycuda