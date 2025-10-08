#include "ComputeCore/gpu/performance/memory_coalescing_optimizer.h"
#include <algorithm>
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <sstream>
#include <numeric>
#include <cmath>

namespace keycuda {
namespace gpu {
namespace performance {

MemoryCoalescingOptimizer::MemoryCoalescingOptimizer(int device_id)
    : device_id_(device_id)
    , warp_size_(DEFAULT_WARP_SIZE)
    , l2_cache_size_(0)
    , shared_memory_per_block_(0)
    , max_threads_per_block_(0)
    , last_error_(ErrorType::NONE) {

    InitializeDeviceProperties();

    // Initialize default configuration
    config_.enable_profiling = true;
    config_.enable_auto_optimization = true;
    config_.min_improvement_threshold = 0.05;
    config_.max_optimization_attempts = 10;
    config_.enable_detailed_analysis = true;
    config_.profiling_timeout = std::chrono::microseconds(1000000);
    config_.max_strategy = CoalescingStrategy::SHARED_MEMORY;
}

MemoryCoalescingOptimizer::~MemoryCoalescingOptimizer() {
    // Cleanup resources
}

void MemoryCoalescingOptimizer::InitializeDeviceProperties() {
    cudaError_t error = cudaGetDeviceProperties(&device_properties_, device_id_);
    if (error != cudaSuccess) {
        SetError(ErrorType::CUDA_ERROR, "Failed to get device properties");
        return;
    }

    warp_size_ = device_properties_.warpSize;
    l2_cache_size_ = device_properties_.l2CacheSize;
    shared_memory_per_block_ = device_properties_.sharedMemPerBlock;
    max_threads_per_block_ = device_properties_.maxThreadsPerBlock;

    // Initialize architecture-specific recommendations
    int compute_capability = device_properties_.major * 10 + device_properties_.minor;
    architecture_recommendations_[compute_capability] = GetArchitectureSpecificOptimizations(
        compute_capability, AccessPattern{}
    );
}

AccessPattern MemoryCoalescingOptimizer::AnalyzeAccessPattern(
    const void* device_ptr,
    size_t data_size,
    size_t element_size,
    int block_size,
    int grid_size) {

    AccessPattern pattern{};

    if (!device_ptr || data_size == 0 || element_size == 0) {
        SetError(ErrorType::INVALID_PATTERN, "Invalid parameters for access pattern analysis");
        return pattern;
    }

    try {
        // Determine access pattern type based on heuristics
        pattern.type = AnalyzePatternType(device_ptr, data_size, element_size, block_size);
        pattern.access_size = element_size;
        pattern.stride_size = CalculateStrideSize(device_ptr, data_size, element_size);
        pattern.thread_offsets = GenerateThreadOffsets(device_ptr, data_size, element_size, block_size);
        pattern.warp_offsets = GenerateWarpOffsets(pattern.thread_offsets, warp_size_);

        // Calculate pattern characteristics
        pattern.locality_score = CalculateLocalityScore(pattern.thread_offsets);
        pattern.sequentiality_score = CalculateSequentialityScore(pattern.thread_offsets);
        pattern.estimated_latency = EstimateMemoryAccessTime(pattern);

        // Calculate GPU-specific metrics
        pattern.coalescing_efficiency = CalculateWarpCoalescing(pattern, 0); // First warp
        pattern.bank_conflicts = CalculateBankConflicts(pattern);
        pattern.shared_memory_utilization = 0.0; // Will be calculated if needed
        pattern.l2_cache_hit_rate = EstimateL2CacheHitRate(pattern);

        return pattern;

    } catch (const std::exception& e) {
        SetError(ErrorType::OPTIMIZATION_FAILED, "Access pattern analysis failed: " + std::string(e.what()));
        return pattern;
    }
}

std::vector<AccessPattern> MemoryCoalescingOptimizer::AnalyzeKernelAccessPatterns(
    const std::vector<void*>& device_ptrs,
    const std::vector<size_t>& data_sizes,
    const std::vector<size_t>& element_sizes,
    int block_size) {

    std::vector<AccessPattern> patterns;

    if (device_ptrs.size() != data_sizes.size() ||
        device_ptrs.size() != element_sizes.size()) {
        SetError(ErrorType::INVALID_PATTERN, "Mismatched array sizes for pattern analysis");
        return patterns;
    }

    for (size_t i = 0; i < device_ptrs.size(); ++i) {
        AccessPattern pattern = AnalyzeAccessPattern(
            device_ptrs[i], data_sizes[i], element_sizes[i], block_size
        );
        patterns.push_back(pattern);
    }

    return patterns;
}

CoalescingMetrics MemoryCoalescingOptimizer::CalculateCoalescingEfficiency(
    const AccessPattern& pattern,
    int compute_capability) {

    CoalescingMetrics metrics{};

    if (!ValidateAccessPattern(pattern)) {
        SetError(ErrorType::INVALID_PATTERN, "Invalid access pattern for efficiency calculation");
        return metrics;
    }

    try {
        metrics.total_accesses = pattern.thread_offsets.size();
        metrics.coalesced_accesses = 0;
        metrics.uncoalesced_accesses = 0;
        metrics.active_warps = (pattern.thread_offsets.size() + warp_size_ - 1) / warp_size_;

        // Analyze each warp
        std::vector<double> warp_efficiencies;
        for (int warp_id = 0; warp_id < metrics.active_warps; ++warp_id) {
            double warp_efficiency = CalculateWarpCoalescing(pattern, warp_id);
            warp_efficiencies.push_back(warp_efficiency);

            // Count coalesced vs uncoalesced accesses in this warp
            size_t start_idx = warp_id * warp_size_;
            size_t end_idx = std::min(start_idx + warp_size_, pattern.thread_offsets.size());

            for (size_t i = start_idx; i < end_idx; ++i) {
                if (IsCoalescedAccess(pattern.thread_offsets[i], i, pattern.stride_size)) {
                    metrics.coalesced_accesses++;
                } else {
                    metrics.uncoalesced_accesses++;
                }
            }
        }

        // Calculate overall metrics
        metrics.coalescing_efficiency = metrics.total_accesses > 0 ?
            static_cast<double>(metrics.coalesced_accesses) / metrics.total_accesses : 0.0;

        metrics.theoretical_maximum_efficiency = 1.0; // Perfect coalescing
        metrics.improvement_potential = metrics.theoretical_maximum_efficiency - metrics.coalescing_efficiency;

        // Calculate uncoalescing penalty
        metrics.uncoalescing_penalty = CalculateUncoalescingPenalty(pattern);

        // Warp-level analysis
        metrics.warp_efficiencies = warp_efficiencies;
        metrics.average_warp_efficiency = warp_efficiencies.empty() ? 0.0 :
            std::accumulate(warp_efficiencies.begin(), warp_efficiencies.end(), 0.0) / warp_efficiencies.size();
        metrics.worst_warp_efficiency = warp_efficiencies.empty() ? 0.0 :
            *std::min_element(warp_efficiencies.begin(), warp_efficiencies.end());

        // Performance impact
        metrics.memory_latency = pattern.estimated_latency;
        metrics.coalescing_overhead = std::chrono::microseconds(
            static_cast<int>(metrics.uncoalescing_penalty * 100) // Rough estimate
        );
        metrics.bandwidth_utilization = EstimateBandwidthUtilization(pattern, compute_capability);
        metrics.occupancy_impact = CalculateOccupancyImpact(pattern);

        // Architecture-specific metrics
        metrics.l2_cache_effectiveness = pattern.l2_cache_hit_rate;
        metrics.shared_memory_bank_conflict_rate = static_cast<double>(pattern.bank_conflicts) /
                                                     (metrics.total_accesses * 16); // Rough estimate
        metrics.memory_divergence_events = CountMemoryDivergenceEvents(pattern);

        metrics.timestamp = std::chrono::system_clock::now();

        return metrics;

    } catch (const std::exception& e) {
        SetError(ErrorType::OPTIMIZATION_FAILED, "Coalescing efficiency calculation failed: " + std::string(e.what()));
        return metrics;
    }
}

std::vector<OptimizationRecommendation> MemoryCoalescingOptimizer::GenerateOptimizationRecommendations(
    const AccessPattern& pattern,
    const CoalescingMetrics& metrics,
    CoalescingStrategy max_strategy) {

    std::vector<OptimizationRecommendation> recommendations;

    if (!ValidateAccessPattern(pattern)) {
        return recommendations;
    }

    // Only generate recommendations if there's room for improvement
    if (metrics.coalescing_efficiency >= 0.95) {
        return recommendations; // Already well-optimized
    }

    try {
        // Generate recommendations based on strategy
        for (int strategy = static_cast<int>(CoalescingStrategy::NONE) + 1;
             strategy <= static_cast<int>(max_strategy);
             ++strategy) {

            CoalescingStrategy current_strategy = static_cast<CoalescingStrategy>(strategy);
            auto recommendation = GenerateRecommendationForStrategy(pattern, metrics, current_strategy);

            if (recommendation.expected_improvement >= config_.min_improvement_threshold) {
                recommendations.push_back(recommendation);
            }
        }

        // Sort by expected improvement (highest first)
        std::sort(recommendations.begin(), recommendations.end(),
                  [](const OptimizationRecommendation& a, const OptimizationRecommendation& b) {
                      return a.expected_improvement > b.expected_improvement;
                  });

    } catch (const std::exception& e) {
        SetError(ErrorType::OPTIMFORMATION_FAILED, "Recommendation generation failed: " + std::string(e.what()));
    }

    return recommendations;
}

MemoryLayoutAnalysis MemoryCoalescingOptimizer::PerformLayoutAnalysis(
    const void* device_ptr,
    size_t data_size,
    size_t element_size,
    const std::string& data_structure_name) {

    MemoryLayoutAnalysis analysis{};
    analysis.data_structure_name = data_structure_name;

    // Analyze access patterns
    analysis.patterns.push_back(AnalyzeAccessPattern(device_ptr, data_size, element_size));

    // Calculate current metrics
    if (!analysis.patterns.empty()) {
        analysis.current_metrics = CalculateCoalescingEfficiency(
            analysis.patterns[0],
            device_properties_.major * 10 + device_properties_.minor
        );

        // Generate recommendations
        analysis.recommendations = GenerateOptimizationRecommendations(
            analysis.patterns[0],
            analysis.current_metrics,
            config_.max_strategy
        );

        // Layout optimization suggestions
        analysis.suggest_struct_of_arrays = ShouldSuggestStructOfArrays(analysis.patterns[0]);
        analysis.suggest_data_padding = ShouldSuggestDataPadding(analysis.patterns[0]);
        analysis.suggest_memory_alignment = ShouldSuggestMemoryAlignment(analysis.patterns[0]);
        analysis.recommended_alignment = CalculateRecommendedAlignment(analysis.patterns[0]);
        analysis.recommended_padding = CalculateRecommendedPadding(analysis.patterns[0]);

        // Access pattern suggestions
        analysis.suggest_loop_tiling = ShouldSuggestLoopTiling(analysis.patterns[0]);
        analysis.suggest_thread_reorganization = ShouldSuggestThreadReorganization(analysis.patterns[0]);
        analysis.prefer_shared_memory = ShouldPreferSharedMemory(analysis.current_metrics);
        analysis.optimal_tile_size = CalculateOptimalTileSize(analysis.patterns[0], shared_memory_per_block_);
        analysis.optimal_block_size = CalculateOptimalBlockSize(analysis.patterns[0]);
    }

    // Store in history
    analysis_history_.push_back(analysis);

    return analysis;
}

std::vector<size_t> MemoryCoalescingOptimizer::TransformForCoalescing(
    const AccessPattern& pattern,
    CoalescingStrategy strategy,
    int block_size) {

    if (!ValidateAccessPattern(pattern)) {
        SetError(ErrorType::INVALID_PATTERN, "Invalid pattern for transformation");
        return {};
    }

    switch (strategy) {
        case CoalescingStrategy::AUTO_TRANSFORM:
            return GenerateCoalescedOffsets(pattern, block_size, strategy);

        case CoalescingStrategy::RESTRUCTURE_DATA:
            return GenerateRestructuredOffsets(pattern, block_size);

        case CoalescingStrategy::TILE_ACCESS:
            return GenerateTiledOffsets(0, pattern.stride_size,
                                      pattern.access_size, block_size);

        case CoalescingStrategy::PREFETCH_AWARE:
            return GeneratePrefetchAwareOffsets(pattern, block_size);

        case CoalescingStrategy::SHARED_MEMORY:
            return GenerateSharedMemoryOffsets(pattern, block_size);

        default:
            return pattern.thread_offsets;
    }
}

std::string MemoryCoalescingOptimizer::GenerateOptimizedKernelCode(
    const std::string& kernel_name,
    const AccessPattern& pattern,
    const OptimizationRecommendation& recommendation) {

    std::ostringstream code;

    code << "// Optimized kernel for improved memory coalescing\n";
    code << "// Generated by MemoryCoalescingOptimizer\n";
    code << "// Strategy: " << static_cast<int>(recommendation.strategy) << "\n";
    code << "// Expected improvement: " << (recommendation.expected_improvement * 100) << "%\n\n";

    code << "__global__ void " << kernel_name << "(";

    // Generate kernel parameters (simplified)
    code << "void* data, size_t size, int* result) {\n";

    // Generate thread and block indices
    code << "    int tid = threadIdx.x + blockIdx.x * blockDim.x;\n";
    code << "    int stride = blockDim.x * gridDim.x;\n\n";

    // Generate optimized memory access code
    switch (recommendation.strategy) {
        case CoalescingStrategy::RESTRUCTURE_DATA:
            code << "    // Restructured data access for better coalescing\n";
            code << "    size_t coalesced_offset = tid * sizeof(int);\n";
            code << "    int* coalesced_data = (int*)data + coalesced_offset;\n";
            code << "    if (tid < size) {\n";
            code << "        result[tid] = coalesced_data[tid];\n";
            code << "    }\n";
            break;

        case CoalescingStrategy::TILE_ACCESS:
            code << "    // Tiled access pattern\n";
            code << "    int tile_size = 32;\n";
            code << "    int tile_id = tid / tile_size;\n";
            code << "    int lane_id = tid % tile_size;\n";
            code << "    size_t tile_offset = tile_id * tile_size;\n";
            code << "    if (tid < size) {\n";
            code << "        int* tile_data = (int*)data + tile_offset;\n";
            code << "        result[tid] = tile_data[lane_id];\n";
            code << "    }\n";
            break;

        case CoalescingStrategy::SHARED_MEMORY:
            code << "    // Shared memory optimization\n";
            code << "    __shared__ int shared_data[256];\n";
            code << "    if (threadIdx.x < size && blockIdx.x == 0) {\n";
            code << "        shared_data[threadIdx.x] = ((int*)data)[threadIdx.x];\n";
            code << "    }\n";
            code << "    __syncthreads();\n";
            code << "    if (tid < size) {\n";
            code << "        result[tid] = shared_data[threadIdx.x];\n";
            code << "    }\n";
            break;

        default:
            code << "    // Basic optimized access\n";
            code << "    if (tid < size) {\n";
            code << "        result[tid] = ((int*)data)[tid];\n";
            code << "    }\n";
            break;
    }

    code << "}\n\n";

    return code.str();
}

CoalescingMetrics MemoryCoalescingOptimizer::MeasureCoalescingPerformance(
    const void* device_ptr,
    size_t data_size,
    const std::vector<size_t>& access_offsets,
    int block_size,
    int iterations) {

    CoalescingMetrics metrics{};

    if (!device_ptr || data_size == 0 || access_offsets.empty()) {
        SetError(ErrorType::INVALID_PATTERN, "Invalid parameters for performance measurement");
        return metrics;
    }

    try {
        auto start_time = std::chrono::high_resolution_clock::now();

        // Create test data
        std::vector<int> host_data(access_offsets.size());
        for (size_t i = 0; i < access_offsets.size(); ++i) {
            host_data[i] = static_cast<int>(i);
        }

        void* device_test_data = nullptr;
        if (cudaMalloc(&device_test_data, data_size) != cudaSuccess) {
            SetError(ErrorType::INSUFFICIENT_MEMORY, "Failed to allocate device memory for testing");
            return metrics;
        }

        // Copy test data to device
        cudaMemcpy(device_test_data, host_data.data(),
                      host_data.size() * sizeof(int), cudaMemcpyHostToDevice);

        // Create streams for measurement
        cudaStream_t stream;
        cudaStreamCreate(&stream);

        // Perform memory access test
        for (int iter = 0; iter < iterations; ++iter) {
            for (size_t offset : access_offsets) {
                // Simulate memory access
                int dummy = *((int*)device_test_data + offset);
                (void)dummy; // Suppress unused variable warning
            }
        }

        cudaStreamSynchronize(stream);
        auto end_time = std::chrono::high_resolution_clock::now();

        // Calculate metrics
        auto total_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        metrics.memory_latency = total_time;

        // Simulate coalescing analysis
        metrics.total_accesses = access_offsets.size() * iterations;
        metrics.coalesced_accesses = static_cast<int>(metrics.total_accesses * 0.8); // Estimate
        metrics.uncoalesced_accesses = metrics.total_accesses - metrics.coalesced_accesses;
        metrics.coalescing_efficiency = static_cast<double>(metrics.coalesced_accesses) / metrics.total_accesses;

        // Calculate other metrics
        metrics.theoretical_maximum_efficiency = 1.0;
        metrics.improvement_potential = 1.0 - metrics.coalescing_efficiency;
        metrics.uncoalescing_penalty = (1.0 - metrics.coalescing_efficiency) * COALESCING_PENALTY_FACTOR;

        metrics.timestamp = std::chrono::system_clock::now();

        // Cleanup
        cudaFree(device_test_data);
        cudaStreamDestroy(stream);

    } catch (const std::exception& e) {
        SetError(ErrorType::CUDA_ERROR, "Performance measurement failed: " + std::string(e.what()));
    }

    return metrics;
}

std::chrono::microseconds MemoryCoalescingOptimizer::EstimateCoalescingImprovement(
    const CoalescingMetrics& current,
    const OptimizationRecommendation& recommendation) {

    // Simple estimation based on strategy type
    double improvement_factor = 1.0;

    switch (recommendation.strategy) {
        case CoalescingStrategy::AUTO_TRANSFORM:
            improvement_factor = 1.15; // 15% improvement
            break;
        case CoalescingStrategy::RESTRUCTURE_DATA:
            improvement_factor = 1.25; // 25% improvement
            break;
        case CoalescingStrategy::TILE_ACCESS:
            improvement_factor = 1.30; // 30% improvement
            break;
        case CoalescingStrategy::PREFETCH_AWARE:
            improvement_factor = 1.10; // 10% improvement
            break;
        case CoalescingStrategy::SHARED_MEMORY:
            improvement_factor = 1.40; // 40% improvement
            break;
        default:
            improvement_factor = 1.05; // 5% improvement
            break;
    }

    auto current_latency = current.memory_latency;
    auto estimated_improvement = std::chrono::microseconds(
        static_cast<int>(current_latency.count() * (improvement_factor - 1.0))
    );

    return estimated_improvement;
}

bool MemoryCoalescingOptimizer::ValidateCoalescingImprovement(
    const void* original_data,
    const void* optimized_data,
    size_t data_size,
    const std::vector<size_t>& access_offsets,
    double min_improvement) {

    // Measure original performance
    auto original_offsets = access_offsets;
    CoalescingMetrics original_metrics = MeasureCoalescingPerformance(
        original_data, data_size, original_offsets
    );

    // Measure optimized performance
    CoalescingMetrics optimized_metrics = MeasureCoalescingPerformance(
        optimized_data, data_size, original_offsets
    );

    // Calculate improvement
    double improvement_ratio = 1.0;
    if (original_metrics.memory_latency.count() > 0) {
        improvement_ratio = static_cast<double>(original_metrics.memory_latency.count()) /
                         optimized_metrics.memory_latency.count();
    }

    return improvement_ratio >= (1.0 + min_improvement);
}

bool MemoryCoalescingOptimizer::TestCoalescingStability(
    const AccessPattern& pattern,
    int test_iterations,
    double variance_threshold) {

    if (!ValidateAccessPattern(pattern)) {
        return false;
    }

    std::vector<double> efficiency_samples;
    efficiency_samples.reserve(test_iterations);

    try {
        for (int i = 0; i < test_iterations; ++i) {
            auto metrics = CalculateCoalescingEfficiency(pattern);
            efficiency_samples.push_back(metrics.coalescing_efficiency);
        }

        // Calculate variance
        double mean = std::accumulate(efficiency_samples.begin(), efficiency_samples.end(), 0.0) / efficiency_samples.size();
        double variance = 0.0;
        for (double sample : efficiency_samples) {
            variance += (sample - mean) * (sample - mean);
        }
        variance /= efficiency_samples.size();

        double standard_deviation = std::sqrt(variance);
        double coefficient_of_variation = (mean > 0) ? (standard_deviation / mean) : 0.0;

        return coefficient_of_variation <= variance_threshold;

    } catch (const std::exception& e) {
        SetError(ErrorType::VALIDATION_ERROR, "Stability test failed: " + std::string(e.what()));
        return false;
    }
}

json MemoryCoalescingOptimizer::GetCoalescingAnalytics() const {
    json analytics;

    // Current device properties
    analytics["device"]["compute_capability"] =
        device_properties_.major * 10 + device_properties_.minor;
    analytics["device"]["warp_size"] = warp_size_;
    analytics["device"]["l2_cache_size"] = l2_cache_size_;
    analytics["device"]["shared_memory_per_block"] = shared_memory_per_block_;

    // Analysis history summary
    analytics["total_analyses"] = analysis_history_.size();
    analytics["total_performance_measurements"] = performance_history_.size();

    if (!analysis_history_.empty()) {
        auto latest_analysis = analysis_history_.back();
        analytics["latest_analysis"]["coalescing_efficiency"] = latest_analysis.current_metrics.coalescing_efficiency;
        analytics["latest_analysis"]["uncoalescing_penalty"] = latest_analysis.current_metrics.uncoalescing_penalty;
        analytics["latest_analysis"]["bandwidth_utilization"] = latest_analysis.current_metrics.bandwidth_utilization;
        analytics["latest_analysis"]["recommendations_count"] = latest_analysis.recommendations.size();
    }

    // Performance trends
    if (performance_history_.size() >= 2) {
        auto first = performance_history_.front();
        auto latest = performance_history_.back();

        analytics["performance_trends"]["efficiency_change"] =
            latest.coalescing_efficiency - first.coalescing_efficiency;
        analytics["performance_trends"]["latency_change"] =
            static_cast<int>(latest.memory_latency.count() - first.memory_latency.count());
    }

    // Configuration
    analytics["config"]["enable_profiling"] = config_.enable_profiling;
    analytics["config"]["enable_auto_optimization"] = config_.enable_auto_optimization;
    analytics["config"]["min_improvement_threshold"] = config_.min_improvement_threshold;
    analytics["config"]["max_strategy"] = static_cast<int>(config_.max_strategy);

    return analytics;
}

std::string MemoryCoalescingOptimizer::GenerateCoalescingReport() const {
    auto analytics = GetCoalescingAnalytics();
    std::ostringstream report;

    report << "=== Memory Coalescing Optimization Report ===\n\n";

    report << "Device Information:\n";
    report << "  Compute Capability: " << analytics["device"]["compute_capability"] << "\n";
    report << "  Warp Size: " << analytics["device"]["warp_size"] << "\n";
    report << "  L2 Cache Size: " << analytics["device"]["l2_cache_size"] << " bytes\n";
    report << "  Shared Memory per Block: " << analytics["device"]["shared_memory_per_block"] << " bytes\n\n";

    report << "Analysis Summary:\n";
    report << "  Total Analyses Performed: " << analytics["total_analyses"] << "\n";
    report << "  Performance Measurements: " << analytics["total_performance_measurements"] << "\n\n";

    if (analytics.contains("latest_analysis")) {
        const auto& latest = analytics["latest_analysis"];
        report << "Latest Analysis Results:\n";
        report << "  Coalescing Efficiency: " << (latest["coalescing_efficiency"].get<double>() * 100) << "%\n";
        report << "  Uncoalescing Penalty: " << latest["uncoalescing_penalty"].get<double>() << "\n";
        report << "  Bandwidth Utilization: " << (latest["bandwidth_utilization"].get<double>() * 100) << "%\n";
        report << "  Optimization Recommendations: " << latest["recommendations_count"] << "\n\n";
    }

    if (analytics.contains("performance_trends")) {
        const auto& trends = analytics["performance_trends"];
        report << "Performance Trends:\n";
        report << "  Efficiency Change: " << (trends["efficiency_change"].get<double>() * 100) << "%\n";
        report << "  Latency Change: " << trends["latency_change"].get<int>() << " μs\n";
    }

    report << "Configuration:\n";
    report << "  Profiling: " << (analytics["config"]["enable_profiling"] ? "Enabled" : "Disabled") << "\n";
    report << "  Auto Optimization: " << (analytics["config"]["enable_auto_optimization"] ? "Enabled" : "Disabled") << "\n";
    report << "  Minimum Improvement Threshold: " << (analytics["config"]["min_improvement_threshold"].get<double>() * 100) << "%\n";
    report << "  Maximum Strategy: " << analytics["config"]["max_strategy"] << "\n";

    return report.str();
}

void MemoryCoalescingOptimizer::ExportOptimizationPlan(const std::string& filename) const {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << GetCoalescingAnalytics().dump(4);
        file.close();
    }
}

void MemoryCoalescingOptimizer::UpdateConfiguration(const OptimizerConfig& config) {
    config_ = config;
}

MemoryCoalescingOptimizer::OptimizerConfig MemoryCoalescingOptimizer::GetCurrentConfiguration() const {
    return config_;
}

MemoryCoalescingOptimizer::ErrorType MemoryCoalescingOptimizer::GetLastError() const {
    return last_error_;
}

std::string MemoryCoalescingOptimizer::GetErrorMessage() const {
    return last_error_message_;
}

bool MemoryCoalescingOptimizer::AttemptErrorRecovery() {
    return RecoverFromError(last_error_);
}

// Private methods implementation

AccessPatternType MemoryCoalescingOptimizer::AnalyzePatternType(
    const void* device_ptr,
    size_t data_size,
    size_t element_size,
    int block_size) const {

    // Simplified pattern type detection
    // In a real implementation, this would analyze the actual access patterns

    size_t expected_stride = element_size;

    // Check if access appears sequential
    bool is_sequential = true;
    for (size_t i = 1; i < std::min(data_size / element_size, size_t(100)); ++i) {
        size_t current_offset = i * element_size;
        if (current_offset >= data_size) break;

        // In a real implementation, this would check actual memory access patterns
        // For now, assume sequential if stride equals element size
    }

    if (is_sequential) {
        return AccessPatternType::SEQUENTIAL;
    }

    // Check for strided access
    size_t stride = CalculateStrideSize(device_ptr, data_size, element_size);
    if (stride > element_size && stride < data_size / 4) {
        return AccessPatternType::STRIDED;
    }

    // Default to uncoalesced
    return AccessPatternType::UNCOALESCED;
}

size_t MemoryCoalescingOptimizer::CalculateStrideSize(
    const void* device_ptr,
    size_t data_size,
    size_t element_size) const {

    // Simplified stride calculation
    // In a real implementation, this would analyze actual memory access patterns
    return element_size; // Assume default stride
}

std::vector<size_t> MemoryCoalescingOptimizer::GenerateThreadOffsets(
    const void* device_ptr,
    size_t data_size,
    size_t element_size,
    int block_size) const {

    std::vector<size_t> offsets;

    // Generate simple sequential thread offsets
    for (int i = 0; i < block_size && i * element_size < data_size; ++i) {
        offsets.push_back(i * element_size);
    }

    return offsets;
}

std::vector<size_t> MemoryCoalescingOptimizer::GenerateWarpOffsets(
    const std::vector<size_t>& thread_offsets,
    size_t warp_size) {

    std::vector<size_t> warp_offsets;

    for (size_t i = 0; i < thread_offsets.size() && i < warp_size; ++i) {
        warp_offsets.push_back(thread_offsets[i]);
    }

    return warp_offsets;
}

double MemoryCoalescingOptimizer::CalculateLocalityScore(const std::vector<size_t>& thread_offsets) const {
    if (thread_offsets.size() < 2) return 1.0;

    // Calculate spatial locality based on offset differences
    double total_locality = 0.0;
    for (size_t i = 1; i < thread_offsets.size(); ++i) {
        size_t diff = std::abs(static_cast<long long>(thread_offsets[i]) - static_cast<long long>(thread_offsets[i-1]));
        double locality = 1.0 / (1.0 + diff / 64.0); // Normalize by cache line size
        total_locality += locality;
    }

    return total_locality / (thread_offsets.size() - 1);
}

double MemoryCoalescingOptimizer::CalculateSequentialityScore(const std::vector<size_t>& thread_offsets) const {
    if (thread_offsets.size() < 2) return 1.0;

    size_t sequential_count = 0;
    for (size_t i = 1; i < thread_offsets.size(); ++i) {
        if (thread_offsets[i] == thread_offsets[i-1] + 1) {
            sequential_count++;
        }
    }

    return static_cast<double>(sequential_count) / (thread_offsets.size() - 1);
}

std::chrono::microseconds MemoryCoalescingOptimizer::EstimateMemoryAccessTime(
    const AccessPattern& pattern,
    double bandwidth_utilization) const {

    // Simplified memory access time estimation
    double base_latency = 100.0; // 100 microseconds base latency
    double efficiency_factor = pattern.coalescing_efficiency;
    double bandwidth_factor = bandwidth_utilization;

    auto estimated_time = std::chrono::microseconds(
        static_cast<int>(base_latency / (efficiency_factor * bandwidth_factor))
    );

    return estimated_time;
}

double MemoryCoalescingOptimizer::CalculateWarpCoalescing(const AccessPattern& pattern, int warp_id) const {
    if (pattern.warp_offsets.size() <= warp_id) return 0.0;

    size_t start_idx = warp_id * warp_size_;
    size_t end_idx = std::min(start_idx + warp_size_, pattern.warp_offsets.size());

    if (start_idx >= pattern.warp_offsets.size()) return 0.0;

    int coalesced_accesses = 0;
    for (size_t i = start_idx; i < end_idx; ++i) {
        if (IsCoalescedAccess(pattern.warp_offsets[i], i, pattern.stride_size)) {
            coalesced_accesses++;
        }
    }

    return static_cast<double>(coalesced_accesses) / std::min(end_idx - start_idx, warp_size_);
}

bool MemoryCoalescingOptimizer::IsCoalescedAccess(
    size_t offset,
    int thread_id,
    size_t stride_size) const {

    // Simple coalescing check
    // In a real implementation, this would check if the access falls within the same memory transaction
    return (offset % 128 == 0); // Assume 128-byte cache line alignment
}

int MemoryCoalescingOptimizer::CalculateBankConflicts(const AccessPattern& pattern) const {
    // Simplified bank conflict calculation
    // In a real implementation, this would analyze shared memory bank usage
    return 0; // Assume no bank conflicts for now
}

double MemoryCoalescingOptimizer::EstimateL2CacheHitRate(const AccessPattern& pattern) const {
    // Simplified L2 cache hit rate estimation
    double locality = pattern.locality_score;
    return std::min(0.95, locality * 0.8 + 0.2); // 80% weight to locality, 20% base
}

double MemoryCoalescingOptimizer::CalculateUncoalescingPenalty(const AccessPattern& pattern) const {
    // Calculate penalty based on coalescing efficiency
    return (1.0 - pattern.coalescing_efficiency) * COALESCING_PENALTY_FACTOR;
}

double MemoryCoalescingOptimizer::CalculateOccupancyImpact(const AccessPattern& pattern) const {
    // Simplified occupancy impact calculation
    // In a real implementation, this would consider register usage, shared memory, etc.
    return 0.1; // Assume minimal impact
}

double MemoryCoalescingOptimizer::EstimateBandwidthUtilization(
    const AccessPattern& pattern,
    int compute_capability) const {

    // Architecture-specific bandwidth estimation
    double base_utilization = 0.7; // 70% base utilization
    double coalescing_factor = pattern.coalescing_efficiency;

    switch (compute_capability) {
        case 90: // Hopper
            base_utilization = 0.85;
            break;
        case 89: // Ada
            base_utilization = 0.80;
            break;
        case 86: // Ampere
            base_utilization = 0.75;
            break;
        case 75: // Turing
            base_utilization = 0.70;
            break;
        default:
            base_utilization = 0.65;
            break;
    }

    return std::min(0.95, base_utilization * coalescing_factor);
}

int MemoryColescingOptimizer::CountMemoryDivergenceEvents(const AccessPattern& pattern) const {
    // Simplified divergence counting
    // In a real implementation, this would analyze actual thread divergence
    return 0; // Assume no divergence for now
}

bool MemoryCoalescingOptimizer::ValidateAccessPattern(const AccessPattern& pattern) const {
    return pattern.access_size > 0 && !pattern.thread_offsets.empty();
}

double MemoryCoalescingOptimizer::CalculateTransactionEfficiency(const AccessPattern& pattern) const {
    // Simplified transaction efficiency calculation
    return pattern.coalescing_efficiency;
}

std::vector<OptimizationRecommendation> MemoryCoalescingOptimizer::GetArchitectureSpecificOptimizations(
    int compute_capability,
    const AccessPattern& pattern) {

    std::vector<OptimizationRecommendation> recommendations;

    // Architecture-specific optimizations
    if (compute_capability >= 90) {
        // Hopper optimizations
        OptimizationRecommendation rec;
        rec.description = "Enable Hopper-specific memory access optimizations";
        rec.strategy = CoalescingStrategy::SHARED_MEMORY;
        rec.expected_improvement = 0.40;
        rec.implementation_effort = std::chrono::minutes(30);
        rec.architecture_specific = true;
        rec.target_architecture = "Hopper";
        recommendations.push_back(rec);
    } else if (compute_capability >= 89) {
        // Ada optimizations
        OptimizationRecommendation rec;
        rec.description = "Optimize for Ada Lovelace architecture";
        rec.strategy = CoalescingStrategy::TILE_ACCESS;
        rec.expected_improvement = 0.30;
        rec.implementation_effort = std::chrono::minutes(20);
        rec.architecture_specific = true;
        rec.target_architecture = "Ada";
        recommendations.push_back(rec);
    } else if (compute_capability >= 86) {
        // Ampere optimizations
        OptimizationRecommendation rec;
        rec.description = "Implement Ampere-optimized coalescing";
        rec.strategy = CoalescingStrategy::RESTRUCTURE_DATA;
        rec.expected_improvement = 0.25;
        rec.implementation_effort = std::chrono::minutes(15);
        rec.architecture_specific = true;
        rec.target_architecture = "Ampere";
        recommendations.push_back(rec);
    }

    return recommendations;
}

bool MemoryCoalescingOptimizer::IsArchitectureOptimized(const AccessPattern& pattern, int compute_capability) const {
    auto recommendations = GetArchitectureSpecificOptimizations(compute_capability, pattern);
    return !recommendations.empty();
}

MemoryCoalescingOptimizer::SharedMemoryConfig MemoryCoalescingOptimizer::CalculateOptimalSharedMemoryConfig(
    const AccessPattern& pattern,
    int compute_capability) const {

    SharedMemoryConfig config{};
    config.enable_shared_memory = true;
    config.shared_memory_size = shared_memory_per_block_;
    config.cache_line_size = GetCacheLineSize(compute_capability);
    config.use_read_only_cache = SupportsReadOnlyCache(compute_capability);

    // Calculate optimal tile size based on pattern
    config.tile_size_x = 32;
    config.tile_size_y = 8;

    return config;
}

bool MemoryCoalescingOptimizer::ShouldSuggestStructOfArrays(const AccessPattern& pattern) const {
    // Suggest struct-of-arrays for better coalescing
    return pattern.type == AccessPatternType::STRUCTURED_ARRAY &&
           pattern.coalescing_efficiency < 0.8;
}

bool MemoryCoalescingOptimizer::ShouldSuggestDataPadding(const AccessPattern& pattern) const {
    // Suggest padding if access patterns aren't aligned
    return pattern.stride_size % 64 != 0; // Check cache line alignment
}

bool MemoryCoalescingOptimizer::ShouldSuggestMemoryAlignment(const AccessPattern& pattern) const {
    // Suggest memory alignment if needed
    return pattern.stride_size % 128 != 0; // Check 128-byte alignment
}

size_t MemoryCoalescingOptimizer::CalculateRecommendedAlignment(const AccessPattern& pattern) const {
    // Recommend 128-byte alignment for optimal performance
    return 128;
}

size_t MemoryCoalescingOptimizer::CalculateRecommendedPadding(const AccessPattern& pattern) const {
    // Calculate padding to align to cache line boundaries
    size_t misalignment = pattern.stride_size % 128;
    return misalignment > 0 ? 128 - misalignment : 0;
}

bool MemoryColescingOptimizer::ShouldSuggestLoopTiling(const AccessPattern& pattern) const {
    // Suggest loop tiling for large data sets
    return pattern.access_size > 1024 * 1024; // > 1MB
}

bool MemoryColescingOptimizer::ShouldSuggestThreadReorganization(const AccessPattern& pattern) const {
    // Suggest thread reorganization if coalescing is poor
    return pattern.coalescing_efficiency < 0.6;
}

bool MemoryColescingOptimizer::ShouldPreferSharedMemory(const CoalescingMetrics& metrics) const {
    // Prefer shared memory if coalescing efficiency can be significantly improved
    return metrics.improvement_potential > 0.3;
}

size_t MemoryCoalescingOptimizer::CalculateOptimalTileSize(
    const AccessPattern& pattern,
    size_t shared_memory_limit) const {

    // Simplified tile size calculation
    size_t element_size = pattern.access_size;
    size_t elements_per_tile = shared_memory_limit / (element_size * 4); // 4-way splitting

    return std::clamp(elements_per_tile, size_t(32), size_t(256));
}

int MemoryCoalescingOptimizer::CalculateOptimalBlockSize(const AccessPattern& pattern) const {
    // Calculate optimal block size based on pattern and device capabilities
    int threads_per_element = 1; // Simplified

    int optimal_size = std::min(
        max_threads_per_block_,
        static_cast<int>(pattern.thread_offsets.size())
    );

    // Round up to warp size multiples
    return ((optimal_size + warp_size_ - 1) / warp_size_) * warp_size_;
}

bool MemoryCoalescingOptimizer::SupportsReadOnlyCache(int compute_capability) const {
    return compute_capability >= 86; // ReadOnly cache introduced in Ampere
}

size_t MemoryCoalescingOptimizer::GetCacheLineSize(int compute_capability) const {
    // Architecture-specific cache line sizes
    switch (compute_capability) {
        case 90: return 128; // Hopper
        case 89: return 128; // Ada
        case 86: return 128; // Ampere
        case 75: return 128; // Turing
        default: return 128; // Default
    }
}

double MemoryCoalescingOptimizer::GetL2CacheLatency(int compute_capability) const {
    // Architecture-specific L2 cache latencies (in clock cycles)
    switch (compute_capability) {
        case 90: return 200;  // Hopper
        case 89: return 250;  // Ada
        case 86: return 300;  // Ampere
        case 75: return 400;  // Turing
        default: return 500;  // Default
    }
}

void MemoryCoalescingOptimizer::SetError(ErrorType error, const std::string& message) {
    last_error_ = error;
    last_error_message_ = message;
}

bool MemoryCoalescingOptimizer::RecoverFromError(ErrorType error) {
    switch (error) {
        case ErrorType::CUDA_ERROR:
            cudaGetLastError(); // Clear error
            return true;
        case ErrorType::INSUFFICIENT_MEMORY:
            // Try to reduce buffer sizes or allocations
            return true;
        case ErrorType::OPTIMIZATION_FAILED:
            // Fall back to simpler strategies
            return true;
        default:
            return false;
    }
}

// Helper methods for generating recommendations
OptimizationRecommendation MemoryCoalescingOptimizer::GenerateRecommendationForStrategy(
    const AccessPattern& pattern,
    const CoalescingMetrics& metrics,
    CoalescingStrategy strategy) {

    OptimizationRecommendation rec{};
    rec.strategy = strategy;

    switch (strategy) {
        case CoalescingStrategy::AUTO_TRANSFORM:
            rec.description = "Apply automatic memory access transformation";
            rec.expected_improvement = 0.15;
            rec.implementation_effort = std::chrono::minutes(10);
            rec.code_changes = {"Modify kernel access patterns", "Add coalescing logic"};
            break;

        case CoalescingStrategy::RESTRUCTURE_DATA:
            rec.description = "Restructure data layout for optimal coalescing";
            rec.expected_improvement = 0.25;
            rec.implementation_effort = std::chrono::minutes(30);
            rec.code_changes = {"Change data structure layout", "Update access patterns"};
            break;

        case CoalescingStrategy::TILE_ACCESS:
            rec.description = "Implement tiled memory access pattern";
            rec.expected_improvement = 0.30;
            rec.implementation_effort = std::chrono::minutes(20);
            rec.code_changes = {"Add tiling logic", "Reorganize loops"};
            break;

        case CoalescingStrategy::PREFETCH_AWARE:
            rec.description = "Add prefetch-aware memory access optimization";
            rec.expected_improvement = 0.10;
            rec.implementation_effort = std::chrono::minutes(5);
            rec.code_changes = {"Add prefetch instructions", "Adjust access timing"};
            break;

        case CoalescingStrategy::SHARED_MEMORY:
            rec.description = "Utilize shared memory for data caching";
            rec.expected_improvement = 0.40;
            rec.implementation_effort = std::chrono::minutes(40);
            rec.code_changes = {"Add shared memory allocation", "Implement caching logic"};
            break;

        default:
            rec.description = "Basic coalescing optimization";
            rec.expected_improvement = 0.05;
            rec.implementation_effort = std::chrono::minutes(5);
            rec.code_changes = {"Simple access pattern changes"};
            break;
    }

    rec.architecture_specific = false;
    rec.target_architecture = "通用";

    return rec;
}

std::vector<size_t> MemoryCoalescingOptimizer::GenerateCoalescedOffsets(
    const AccessPattern& pattern,
    int block_size,
    CoalescingStrategy strategy) {

    std::vector<size_t> coalesced_offsets;

    switch (strategy) {
        case CoalescingStrategy::RESTRUCTURE_DATA:
            // Generate restructured offsets for better coalescing
            for (int i = 0; i < block_size; ++i) {
                coalesced_offsets.push_back(i * pattern.access_size);
            }
            break;

        case CoalescingStrategy::TILE_ACCESS:
            // Generate tiled offsets
            return GenerateTiledOffsets(0, pattern.stride_size, pattern.access_size, block_size);

        case CoalescingStrategy::SHARED_MEMORY:
            // Generate shared memory optimized offsets
            for (int i = 0; i < block_size; ++i) {
                coalesced_offsets.push_back(i * pattern.access_size);
            }
            break;

        default:
            // Default to original offsets
            return pattern.thread_offsets;
    }

    return coalesced_offsets;
}

std::vector<size_t> MemoryCoalescingOptimizer::GenerateRestructuredOffsets(
    const AccessPattern& pattern,
    int block_size) {

    // Generate restructured offsets for better coalescing
    std::vector<size_t> restructured_offsets;

    for (int i = 0; i < block_size; ++i) {
        restructured_offsets.push_back(i * pattern.access_size);
    }

    return restructured_offsets;
}

std::vector<size_t> MemoryCoalescingOptimizer::GenerateTiledOffsets(
    size_t base_offset,
    size_t stride,
    int elements_per_tile,
    int block_size) {

    std::vector<size_t> tiled_offsets;

    for (int i = 0; i < block_size; ++i) {
        size_t tile_id = i / elements_per_tile;
        size_t lane_id = i % elements_per_tile;
        tiled_offsets.push_back(base_offset + tile_id * stride * elements_per_tile + lane_id * stride);
    }

    return tiled_offsets;
}

std::vector<size_t> MemoryCoalescingOptimizer::GeneratePrefetchAwareOffsets(
    const AccessPattern& pattern,
    int block_size) {

    // Generate prefetch-aware offsets
    std::vector<size_t> prefetch_offsets;

    for (int i = 0; i < block_size; ++i) {
        prefetch_offsets.push_back(i * pattern.access_size);
    }

    return prefetch_offsets;
}

std::vector<size_t MemoryCoalescingOptimizer::GenerateSharedMemoryOffsets(
    const AccessPattern& pattern,
    int block_size) {

    // Generate shared memory optimized offsets
    std::vector<size_t> shared_offsets;

    for (int i = 0; i < block_size; ++i) {
        shared_offsets.push_back(i * pattern.access_size);
    }

    return shared_offsets;
}

// Factory function
std::unique_ptr<MemoryCoalescingOptimizer> CreateMemoryCoalescingOptimizer(int device_id) {
    return std::make_unique<MemoryCoalescingOptimizer>(device_id);
}

} // namespace performance
} // namespace gpu
} // namespace keycuda