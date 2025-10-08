#include "ComputeCore/gpu/performance/occupancy_calculator.h"
#include "ComputeCore/gpu/performance/resource_profiler.h"
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace keycuda {
namespace gpu {
namespace performance {

OccupancyCalculator::OccupancyCalculator() : device_id_(0) {
    UpdateCachedParameters();
}

OccupancyCalculator::OccupancyCalculator(int device_id) : device_id_(device_id) {
    UpdateCachedParameters();
}

OccupancyCalculator::~OccupancyCalculator() = default;

OccupancyMetrics OccupancyCalculator::CalculateOccupancy(
    int block_size,
    const OccupancyParameters& params
) const {
    OccupancyMetrics metrics;
    metrics.block_size = block_size;

    if (!IsValidBlockSize(block_size)) {
        metrics.efficiency_score = 0.0;
        metrics.limiting_factor = "Invalid block size";
        return metrics;
    }

    // Calculate blocks per SM limited by different factors
    int blocks_by_threads = CalculateBlocksLimitedByThreads(block_size, params);
    int blocks_by_registers = CalculateBlocksLimitedByRegisters(block_size, params);
    int blocks_by_shared_memory = CalculateBlocksLimitedBySharedMemory(block_size, params);
    int blocks_by_blocks = CalculateBlocksLimitedByBlocks(params);

    // Determine the actual limiting factor
    metrics.blocks_per_sm = std::min({blocks_by_threads, blocks_by_registers, blocks_by_shared_memory, blocks_by_blocks});

    // Calculate occupancy metrics
    metrics.occupancy_percentage = CalculateOccupancyPercentage(
        metrics.blocks_per_sm, block_size, params
    );

    metrics.active_warps_per_sm = (metrics.blocks_per_sm * block_size) / params.warp_size;
    metrics.active_threads_percentage = (static_cast<double>(metrics.blocks_per_sm * block_size) /
                                       static_cast<double>(params.max_threads_per_sm)) * 100.0;

    // Determine limiting factor
    if (metrics.blocks_per_sm == blocks_by_registers) {
        metrics.limited_by_registers = true;
        metrics.limiting_factor = "Registers";
    } else if (metrics.blocks_per_sm == blocks_by_shared_memory) {
        metrics.limited_by_shared_memory = true;
        metrics.limiting_factor = "Shared Memory";
    } else if (metrics.blocks_per_sm == blocks_by_threads) {
        metrics.limited_by_threads = true;
        metrics.limiting_factor = "Threads";
    } else {
        metrics.limited_by_blocks = true;
        metrics.limiting_factor = "Block Limit";
    }

    // Calculate efficiency score
    metrics.efficiency_score = CalculateEfficiencyScore(metrics, params);

    return metrics;
}

std::vector<OccupancyMetrics> OccupancyCalculator::CalculateOccupancyRange(
    const std::vector<int>& block_sizes,
    const OccupancyParameters& params
) const {
    std::vector<OccupancyMetrics> results;
    results.reserve(block_sizes.size());

    for (int block_size : block_sizes) {
        results.push_back(CalculateOccupancy(block_size, params));
    }

    return results;
}

BlockSizeRecommendation OccupancyCalculator::GetOptimalBlockSize(
    const OccupancyParameters& params,
    double target_occupancy
) const {
    BlockSizeRecommendation recommendation;
    recommendation.target_occupancy = target_occupancy;

    // Get common block sizes and calculate occupancy for each
    std::vector<int> block_sizes = GetCommonBlockSizes();
    std::vector<OccupancyMetrics> occupancy_results = CalculateOccupancyRange(block_sizes, params);

    // Find the best block size
    double best_score = -1.0;
    int best_block_size = 256;  // Default

    for (const auto& metrics : occupancy_results) {
        double score = metrics.efficiency_score;

        // Prefer block sizes that meet or exceed target occupancy
        if (metrics.occupancy_percentage >= target_occupancy * 100.0) {
            score += 0.2;  // Bonus for meeting target
        }

        // Penalty for being significantly over target (wasted resources)
        if (metrics.occupancy_percentage > target_occupancy * 120.0) {
            score -= 0.1;
        }

        if (score > best_score) {
            best_score = score;
            best_block_size = metrics.block_size;
        }
    }

    recommendation.optimal_block_size = best_block_size;

    // Get achieved occupancy for the optimal size
    auto optimal_metrics = CalculateOccupancy(best_block_size, params);
    recommendation.achieved_occupancy = optimal_metrics.occupancy_percentage / 100.0;

    // Generate rationale
    std::stringstream ss;
    ss << "Selected block size " << best_block_size << " achieves "
       << std::fixed << std::setprecision(1) << recommendation.achieved_occupancy * 100.0
       << "% occupancy";

    if (optimal_metrics.limiting_factor != "Block Limit") {
        ss << " (limited by " << optimal_metrics.limiting_factor << ")";
    }

    recommendation.recommendation_rationale = ss.str();

    // Add constraints
    if (optimal_metrics.limited_by_registers) {
        recommendation.constraints.push_back("Register limited");
    }
    if (optimal_metrics.limited_by_shared_memory) {
        recommendation.constraints.push_back("Shared memory limited");
    }
    if (optimal_metrics.limited_by_threads) {
        recommendation.constraints.push_back("Thread limited");
    }

    // Add alternative block sizes
    std::vector<std::pair<int, double>> size_scores;
    for (const auto& metrics : occupancy_results) {
        if (metrics.block_size != best_block_size && metrics.occupancy_percentage >= target_occupancy * 50.0) {
            size_scores.emplace_back(metrics.block_size, metrics.efficiency_score);
        }
    }

    // Sort by score and take top alternatives
    std::sort(size_scores.begin(), size_scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    for (size_t i = 0; i < std::min(size_t(3), size_scores.size()); ++i) {
        recommendation.alternative_sizes.push_back(size_scores[i].first);
    }

    return recommendation;
}

std::vector<int> OccupancyCalculator::GetOptimalBlockSizes(
    const OccupancyParameters& params,
    size_t workload_size
) const {
    std::vector<int> common_sizes = GetCommonBlockSizes();
    std::vector<OccupancyMetrics> metrics = CalculateOccupancyRange(common_sizes, params);

    // Sort by efficiency score
    std::sort(metrics.begin(), metrics.end(),
              [](const OccupancyMetrics& a, const OccupancyMetrics& b) {
                  return a.efficiency_score > b.efficiency_score;
              });

    std::vector<int> optimal_sizes;
    for (const auto& metric : metrics) {
        // Only include sizes with reasonable efficiency
        if (metric.efficiency_score >= 0.5 && metric.occupancy_percentage >= 25.0) {
            optimal_sizes.push_back(metric.block_size);
        }
    }

    // If no good sizes found, return a reasonable default
    if (optimal_sizes.empty()) {
        optimal_sizes.push_back(256);
    }

    return optimal_sizes;
}

std::map<int, double> OccupancyCalculator::CalculateOccupancyCurve(
    const OccupancyParameters& params
) const {
    std::map<int, double> curve;
    std::vector<int> block_sizes = GetCommonBlockSizes();

    for (int block_size : block_sizes) {
        auto metrics = CalculateOccupancy(block_size, params);
        curve[block_size] = metrics.occupancy_percentage;
    }

    return curve;
}

double OccupancyCalculator::EstimateOptimalOccupancy(
    const OccupancyParameters& params,
    bool is_memory_bound
) const {
    // Get device parameters for architecture-specific optimization
    auto profiler = ResourceProfiler::Create();
    int compute_cap = profiler->GetGpuCapabilities(device_id_).compute_capability;

    double sweet_spot = GetOccupancySweetSpot(compute_cap);

    if (is_memory_bound) {
        // Memory-bound kernels can often benefit from slightly lower occupancy
        // to reduce memory contention
        return std::min(sweet_spot, 0.65);
    } else {
        // Compute-bound kernels generally benefit from higher occupancy
        return std::max(sweet_spot, 0.75);
    }
}

std::string OccupancyCalculator::GetPrimaryLimitingFactor(
    int block_size,
    const OccupancyParameters& params
) const {
    auto metrics = CalculateOccupancy(block_size, params);
    return metrics.limiting_factor;
}

bool OccupancyCalculator::IsRegisterLimited(
    int block_size,
    const OccupancyParameters& params
) const {
    auto metrics = CalculateOccupancy(block_size, params);
    return metrics.limited_by_registers;
}

bool OccupancyCalculator::IsSharedMemoryLimited(
    int block_size,
    const OccupancyParameters& params
) const {
    auto metrics = CalculateOccupancy(block_size, params);
    return metrics.limited_by_shared_memory;
}

OccupancyParameters OccupancyCalculator::GetArchitectureOptimizedParameters(
    int compute_capability,
    const std::string& kernel_type
) const {
    OccupancyParameters params = GetDefaultParameters();

    // Architecture-specific optimizations
    if (compute_capability >= 90) {
        // Hopper optimizations
        params.max_threads_per_sm = 2048;
        params.total_registers = 65536;
        if (kernel_type == "tensor") {
            params.registers_per_thread = 64;  // Tensor cores use more registers
        }
    } else if (compute_capability >= 89) {
        // Ada Lovelace optimizations
        params.max_threads_per_sm = 1536;
        params.total_registers = 65536;
    } else if (compute_capability >= 86) {
        // Ampere optimizations
        params.max_threads_per_sm = 1536;
        params.total_registers = 65536;
        if (kernel_type == "matrix") {
            params.registers_per_thread = 48;
        }
    } else if (compute_capability >= 75) {
        // Turing optimizations
        params.max_threads_per_sm = 1024;
        params.total_registers = 65536;
    } else {
        // Conservative defaults for older architectures
        params.max_threads_per_sm = 2048;
        params.total_registers = 65536;
    }

    return params;
}

int OccupancyCalculator::GetOptimalBlockSizeForArchitecture(
    int compute_capability,
    const OccupancyParameters& params
) const {
    auto recommendation = GetOptimalBlockSize(params);

    // Architecture-specific adjustments
    switch (compute_capability) {
        case 90: // Hopper
            return std::max(recommendation.optimal_block_size, 512);
        case 89: // Ada Lovelace
            return std::max(recommendation.optimal_block_size, 384);
        case 86: // Ampere
            return std::max(recommendation.optimal_block_size, 256);
        case 75: // Turing
            return std::max(recommendation.optimal_block_size, 256);
        default:
            return recommendation.optimal_block_size;
    }
}

double OccupancyCalculator::PredictRelativePerformance(
    int block_size,
    const OccupancyParameters& params,
    bool is_memory_bound
) const {
    auto metrics = CalculateOccupancy(block_size, params);

    if (is_memory_bound) {
        return ModelMemoryPerformance(block_size, metrics.occupancy_percentage / 100.0);
    } else {
        return ModelComputePerformance(block_size, metrics.occupancy_percentage / 100.0);
    }
}

std::vector<int> OccupancyCalculator::GetPerformanceOrderedBlockSizes(
    const OccupancyParameters& params,
    bool is_memory_bound
) const {
    std::vector<int> block_sizes = GetCommonBlockSizes();

    // Create map of block size to performance prediction
    std::vector<std::pair<int, double>> size_performance;
    for (int block_size : block_sizes) {
        double performance = PredictRelativePerformance(block_size, params, is_memory_bound);
        size_performance.emplace_back(block_size, performance);
    }

    // Sort by performance (descending)
    std::sort(size_performance.begin(), size_performance.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Extract just the block sizes
    std::vector<int> ordered_sizes;
    ordered_sizes.reserve(size_performance.size());
    for (const auto& pair : size_performance) {
        ordered_sizes.push_back(pair.first);
    }

    return ordered_sizes;
}

bool OccupancyCalculator::ValidateBlockSize(
    int block_size,
    const OccupancyParameters& params
) const {
    return IsValidBlockSize(block_size) && block_size <= params.max_threads_per_block;
}

std::vector<std::string> OccupancyCalculator::GetConfigurationWarnings(
    int block_size,
    const OccupancyParameters& params
) const {
    std::vector<std::string> warnings;

    if (!IsValidBlockSize(block_size)) {
        warnings.push_back("Block size must be a multiple of warp size (32) and within limits");
        return warnings;
    }

    auto metrics = CalculateOccupancy(block_size, params);

    if (metrics.occupancy_percentage < 25.0) {
        warnings.push_back("Low occupancy (< 25%) - consider reducing resource usage per thread");
    }

    if (metrics.limited_by_registers) {
        warnings.push_back("Register limited - consider reducing register usage per thread");
    }

    if (metrics.limited_by_shared_memory) {
        warnings.push_back("Shared memory limited - consider reducing shared memory usage per block");
    }

    if (metrics.efficiency_score < 0.3) {
        warnings.push_back("Low efficiency score - consider different block size or resource allocation");
    }

    return warnings;
}

void OccupancyCalculator::SetDevice(int device_id) {
    device_id_ = device_id;
    UpdateCachedParameters();
}

OccupancyParameters OccupancyCalculator::GetDefaultParameters() const {
    OccupancyParameters params;

    // Get device-specific defaults
    auto profiler = ResourceProfiler::Create();
    auto caps = profiler->GetGpuCapabilities(device_id_);

    params.max_threads_per_sm = caps.max_threads_per_sm;
    params.max_threads_per_block = caps.max_threads_per_block;
    params.warp_size = caps.warp_size;
    params.shared_memory_per_block = caps.shared_memory_per_block;
    params.total_shared_memory = caps.total_shared_memory;

    return params;
}

// Static methods
int OccupancyCalculator::GetDefaultBlockSize(int compute_capability) {
    if (compute_capability >= 90) return 512;      // Hopper
    if (compute_capability >= 89) return 384;      // Ada Lovelace
    if (compute_capability >= 86) return 256;      // Ampere
    if (compute_capability >= 75) return 256;      // Turing
    return 256;                                     // Conservative default
}

std::vector<int> OccupancyCalculator::GetCommonBlockSizes() {
    return {32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480, 512, 544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896, 928, 960, 992, 1024};
}

double OccupancyCalculator::GetMinimumRecommendedOccupancy() {
    return 0.5;  // 50% minimum recommended occupancy
}

// Private methods
int OccupancyCalculator::CalculateBlocksLimitedByThreads(
    int block_size,
    const OccupancyParameters& params
) const {
    return params.max_threads_per_sm / block_size;
}

int OccupancyCalculator::CalculateBlocksLimitedByRegisters(
    int block_size,
    const OccupancyParameters& params
) const {
    int total_registers_needed = block_size * params.registers_per_thread;
    if (total_registers_needed == 0) return params.max_blocks_per_sm;

    return params.total_registers / total_registers_needed;
}

int OccupancyCalculator::CalculateBlocksLimitedBySharedMemory(
    int block_size,
    const OccupancyParameters& params
) const {
    size_t total_shared_needed = params.shared_memory_per_block + params.dynamic_shared_memory_bytes;
    if (total_shared_needed == 0) return params.max_blocks_per_sm;

    return params.total_shared_memory / total_shared_needed;
}

int OccupancyCalculator::CalculateBlocksLimitedByBlocks(
    const OccupancyParameters& params
) const {
    return params.max_blocks_per_sm;
}

double OccupancyCalculator::CalculateOccupancyPercentage(
    int blocks_per_sm,
    int block_size,
    const OccupancyParameters& params
) const {
    int active_threads = blocks_per_sm * block_size;
    return (static_cast<double>(active_threads) / static_cast<double>(params.max_threads_per_sm)) * 100.0;
}

double OccupancyCalculator::CalculateEfficiencyScore(
    const OccupancyMetrics& metrics,
    const OccupancyParameters& params
) const {
    double score = 0.0;

    // Base score from occupancy
    score += (metrics.occupancy_percentage / 100.0) * 0.4;

    // Bonus for good warp utilization
    double warp_utilization = (static_cast<double>(metrics.active_warps_per_sm) /
                              (static_cast<double>(params.max_threads_per_sm) / params.warp_size));
    score += warp_utilization * 0.3;

    // Penalty for limiting factors
    if (metrics.limited_by_registers) score -= 0.1;
    if (metrics.limited_by_shared_memory) score -= 0.1;
    if (metrics.limited_by_threads && metrics.occupancy_percentage < 50.0) score -= 0.2;

    // Bonus for block sizes that are powers of 2 (generally more efficient)
    if ((metrics.block_size & (metrics.block_size - 1)) == 0) {
        score += 0.1;
    }

    return std::max(0.0, std::min(1.0, score));
}

bool OccupancyCalculator::ShouldPreferHigherOccupancy(int compute_capability) const {
    // Newer architectures generally benefit from higher occupancy
    return compute_capability >= 80;  // Ampere and newer
}

double OccupancyCalculator::GetOccupancySweetSpot(int compute_capability) const {
    switch (compute_capability) {
        case 90: return 0.85;  // Hopper
        case 89: return 0.80;  // Ada Lovelace
        case 86: return 0.75;  // Ampere
        case 75: return 0.70;  // Turing
        default: return 0.65;  // Conservative
    }
}

int OccupancyCalculator::GetOptimalWarpCountPerSM(int compute_capability) const {
    switch (compute_capability) {
        case 90: return 64;   // Hopper
        case 89: return 48;   // Ada Lovelace
        case 86: return 48;   // Ampere
        case 75: return 32;   // Turing
        default: return 32;   // Conservative
    }
}

double OccupancyCalculator::ModelMemoryPerformance(int block_size, double occupancy) const {
    // Memory performance scales with occupancy but has diminishing returns
    double base_performance = occupancy;

    // Larger blocks can improve memory coalescing
    double block_size_factor = std::log2(static_cast<double>(block_size)) / 10.0;

    return base_performance * (1.0 + block_size_factor * 0.2);
}

double OccupancyCalculator::ModelComputePerformance(int block_size, double occupancy) const {
    // Compute performance scales more directly with occupancy
    double base_performance = occupancy;

    // Optimal block size for compute is typically in the middle range
    double optimal_block_size = 256.0;
    double block_size_efficiency = 1.0 - std::abs(block_size - optimal_block_size) / optimal_block_size * 0.3;

    return base_performance * block_size_efficiency;
}

OccupancyParameters OccupancyCalculator::GetDeviceParameters(int device_id) const {
    auto profiler = ResourceProfiler::Create();
    auto caps = profiler->GetGpuCapabilities(device_id);

    OccupancyParameters params;
    params.max_threads_per_sm = caps.max_threads_per_sm;
    params.max_threads_per_block = caps.max_threads_per_block;
    params.warp_size = caps.warp_size;
    params.shared_memory_per_block = caps.shared_memory_per_block;
    params.total_shared_memory = caps.total_shared_memory;

    return params;
}

void OccupancyCalculator::UpdateCachedParameters() const {
    cached_params_ = GetDeviceParameters(device_id_);
}

bool OccupancyCalculator::IsValidBlockSize(int block_size) const {
    // Block size must be positive, multiple of warp size, and within reasonable limits
    return block_size > 0 &&
           block_size % 32 == 0 &&
           block_size <= 1024 &&
           block_size <= cached_params_.max_threads_per_block;
}

std::unique_ptr<OccupancyCalculator> CreateOccupancyCalculator(int device_id) {
    return std::make_unique<OccupancyCalculator>(device_id);
}

} // namespace performance
} // namespace gpu
} // namespace keycuda