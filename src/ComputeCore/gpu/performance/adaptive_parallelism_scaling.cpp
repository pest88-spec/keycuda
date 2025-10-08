#include "ComputeCore/gpu/performance/adaptive_parallelism_scaling.h"
#include "ComputeCore/gpu/performance/occupancy_calculator.h"
#include "ComputeCore/gpu/performance/configuration_logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <stdexcept>
#include <cmath>
#include <thread>

namespace keycuda {
namespace gpu {
namespace performance {

AdaptiveParallelismScaling::AdaptiveParallelismScaling(int device_id)
    : target_device_id_(device_id)
    , learning_mode_enabled_(true)
    , automatic_scaling_enabled_(true)
    , scaling_aggressiveness_(0.7)
    , min_throughput_target_(1000.0) // 1K Mkeys/sec minimum
    , max_memory_utilization_target_(0.85) // 85% max memory usage
    , optimization_strategy_("balanced")
    , total_logging_overhead_(0.0)
    , last_integrity_status_(IntegrityStatus::VALID) {

    current_gpu_capabilities_ = DetectGpuCapabilities(device_id);
    start_time_ = std::chrono::steady_clock::now();

    // Initialize occupancy calculator
    occupancy_calculator_ = CreateOccupancyCalculator(device_id);

    // Initialize configuration logger
    configuration_logger_ = std::make_unique<ConfigurationLogger>();

    // Initialize optimization weights for different factors
    optimization_weights_ = {
        {"occupancy", 0.3},
        {"memory_bandwidth", 0.25},
        {"compute_throughput", 0.25},
        {"power_efficiency", 0.1},
        {"thermal_headroom", 0.1}
    };

    // Initialize architecture performance cache with default factors
    architecture_performance_cache_ = {
        {"hopper_90", 1.2},
        {"ada_89", 1.1},
        {"ada_87", 1.05},
        {"ampere_86", 1.0},
        {"ampere_80", 0.95},
        {"turing_75", 0.85},
        {"volta_70", 0.8}
    };
}

AdaptiveParallelismScaling::~AdaptiveParallelismScaling() {
    // Stop verification thread if running
    if (verification_thread_.joinable()) {
        stop_verification_ = true;
        verification_thread_.join();
    }
}

ScalingDecision AdaptiveParallelismScaling::CalculateOptimalConfiguration(
    size_t workload_size,
    const std::string& operation_type) {

    std::lock_guard<std::mutex> lock(scaling_mutex_);

    auto start_time = std::chrono::high_resolution_clock::now();

    // Generate candidate configurations
    auto candidates = GenerateCandidateConfigurations(current_gpu_capabilities_, workload_size);

    // Evaluate each candidate
    std::vector<std::pair<ParallelismConfiguration, double>> evaluated_candidates;
    for (const auto& config : candidates) {
        if (ValidateConfiguration(config)) {
            double performance_score = EstimateConfigurationPerformance(
                config, current_gpu_capabilities_, workload_size);
            evaluated_candidates.emplace_back(config, performance_score);
        }
    }

    if (evaluated_candidates.empty()) {
        // Fallback to safe configuration
        ParallelismConfiguration safe_config = GetSafeConfiguration();
        ScalingDecision fallback_decision;
        fallback_decision.selected_config = safe_config;
        fallback_decision.decision_logic = "fallback_no_valid_candidates";
        fallback_decision.confidence_score = 0.5;
        fallback_decision.decision_time = std::chrono::system_clock::now();
        fallback_decision.gpu_capabilities = current_gpu_capabilities_;
        fallback_decision.automatic_scaling_enabled = automatic_scaling_enabled_;
        return fallback_decision;
    }

    // Sort by performance score
    std::sort(evaluated_candidates.begin(), evaluated_candidates.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Select best configuration
    ParallelismConfiguration best_config = evaluated_candidates[0].first;
    double best_score = evaluated_candidates[0].second;

    // Prepare alternatives
    std::vector<ParallelismConfiguration> alternatives;
    for (size_t i = 1; i < std::min(size_t(5), evaluated_candidates.size()); ++i) {
        alternatives.push_back(evaluated_candidates[i].first);
    }

    // Create scaling decision
    ScalingDecision decision;
    decision.selected_config = best_config;
    decision.alternatives = alternatives;
    decision.decision_logic = "performance_optimization";
    decision.confidence_score = std::min(0.95, best_score);
    decision.decision_time = std::chrono::system_clock::now();
    decision.gpu_capabilities = current_gpu_capabilities_;
    decision.automatic_scaling_enabled = automatic_scaling_enabled_;

    // Add decision to history
    decision_history_.push_back(decision);

    // Log the decision
    if (configuration_logger_) {
        ConfigurationDecision log_decision;
        log_decision.decision_type = DecisionType::INITIAL_CONFIGURATION;
        log_decision.input_config = ParallelismConfiguration(); // Empty input for initial config
        log_decision.output_config = best_config;
        log_decision.gpu_name = current_gpu_capabilities_.device_name;
        log_decision.compute_capability = current_gpu_capabilities_.compute_capability;
        log_decision.total_memory_mb = current_gpu_capabilities_.total_memory_mb;
        log_decision.free_memory_mb = current_gpu_capabilities_.free_memory_mb;
        log_decision.memory_bandwidth_gb_per_sec = current_gpu_capabilities_.memory_bandwidth_gb_per_sec;
        log_decision.sm_count = current_gpu_capabilities_.sm_count;
        log_decision.workload_size = workload_size;
        log_decision.operation_type = operation_type;
        log_decision.decision_logic = "performance_optimization";
        log_decision.rationale = "Selected optimal configuration based on " + std::to_string(evaluated_candidates.size()) + " candidates";
        log_decision.confidence_score = decision.confidence_score;
        log_decision.estimated_throughput_mkeys_per_sec = best_score * 1000; // Convert to Mkeys/sec
        log_decision.estimated_execution_time_ms = best_config.estimated_execution_time.count() / 1000.0;
        log_decision.memory_utilization_estimate = best_config.memory_utilization_estimate;
        log_decision.expected_occupancy = best_config.expected_occupancy;

        configuration_logger_->LogConfigurationDecision(log_decision);
    }

    std::chrono::microseconds decision_time =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start_time);
    UpdatePerformanceMetrics(decision_time);

    return decision;
}

ParallelismConfiguration AdaptiveParallelismScaling::GetRecommendedConfiguration(
    const GpuCapabilities& gpu_caps,
    size_t workload_size,
    const std::map<std::string, std::string>& constraints) {

    ParallelismConfiguration config;

    // Architecture-specific optimization
    if (gpu_caps.compute_capability >= 90) {
        config = OptimizeForHopper(gpu_caps, workload_size);
    } else if (gpu_caps.compute_capability >= 89) {
        config = OptimizeForAda(gpu_caps, workload_size);
    } else if (gpu_caps.compute_capability >= 80) {
        config = OptimizeForAmpere(gpu_caps, workload_size);
    } else if (gpu_caps.compute_capability >= 75) {
        config = OptimizeForTuring(gpu_caps, workload_size);
    } else if (gpu_caps.compute_capability >= 70) {
        config = OptimizeForVolta(gpu_caps, workload_size);
    } else {
        // Generic fallback
        config.block_size = 512;
        config.points_per_thread = 128;
        config.grid_size = gpu_caps.sm_count * 2;
    }

    // Apply constraints
    ApplyConstraints(config, constraints);

    // Set derived values
    config.expected_occupancy = CalculateOccupancy(config, gpu_caps);
    config.memory_utilization_estimate = EstimateMemoryUsage(config, workload_size) /
                                         static_cast<double>(gpu_caps.total_memory_mb);
    config.estimated_execution_time = EstimateExecutionTime(config, gpu_caps, workload_size);

    // Add optimization metrics
    config.optimization_metrics = {
        {"architecture", std::to_string(gpu_caps.compute_capability)},
        {"occupancy", std::to_string(config.expected_occupancy)},
        {"memory_utilization", std::to_string(config.memory_utilization_estimate)},
        {"workload_size", std::to_string(workload_size)}
    };

    return config;
}

void AdaptiveParallelismScaling::UpdatePerformanceFeedback(
    const ParallelismConfiguration& config,
    const PerformanceFeedback& feedback) {

    std::lock_guard<std::mutex> lock(scaling_mutex_);

    // Store feedback in performance history
    std::string key = GenerateConfigurationKey(config);
    performance_history_[key].emplace_back(config, feedback);

    // Update learning model
    if (learning_mode_enabled_) {
        UpdateLearningModel(config, feedback);
    }

    // Track failed configurations
    if (feedback.performance_score < 0.3 || !feedback.accuracy_maintained) {
        failed_configurations_.insert(config);
    }

    // Log the feedback
    std::chrono::microseconds update_time{100}; // Estimate
    UpdatePerformanceMetrics(update_time);
}

void AdaptiveParallelismScaling::RecordPerformanceResult(
    const ParallelismConfiguration& config,
    double throughput_mkeys_per_sec,
    std::chrono::microseconds execution_time,
    bool accuracy_maintained) {

    PerformanceFeedback feedback;
    feedback.actual_throughput_mkeys_per_sec = throughput_mkeys_per_sec;
    feedback.actual_execution_time = execution_time;
    feedback.accuracy_maintained = accuracy_maintained;

    // Calculate derived metrics
    double theoretical_throughput = config.points_per_thread * config.block_size * config.grid_size;
    feedback.actual_occupancy = throughput_mkeys_per_sec / theoretical_throughput;
    feedback.performance_score = std::min(1.0, throughput_mkeys_per_sec / min_throughput_target_);

    UpdatePerformanceFeedback(config, feedback);
}

GpuCapabilities AdaptiveParallelismScaling::DetectGpuCapabilities(int device_id) const {
    GpuCapabilities caps;
    caps.device_id = device_id;

    cudaDeviceProp prop;
    cudaError_t error = cudaGetDeviceProperties(&prop, device_id);
    if (error != cudaSuccess) {
        throw std::runtime_error("Failed to get GPU properties: " +
                                std::string(cudaGetErrorString(error)));
    }

    caps.device_name = prop.name;
    caps.compute_capability = prop.major * 10 + prop.minor;
    caps.total_memory_mb = prop.totalGlobalMem / (1024 * 1024);
    caps.sm_count = prop.multiProcessorCount;
    caps.max_threads_per_sm = prop.maxThreadsPerMultiProcessor;
    caps.max_threads_per_block = prop.maxThreadsPerBlock;
    caps.shared_memory_per_block = prop.sharedMemPerBlock;
    caps.total_shared_memory = prop.sharedMemPerMultiprocessor * prop.multiProcessorCount;
    caps.warp_size = prop.warpSize;
    caps.max_blocks_per_sm = prop.maxBlocksPerMultiProcessor;
    caps.max_registers_per_thread = prop.regsPerBlock / prop.maxThreadsPerBlock;
    caps.supports_managed_memory = prop.managedMemory;
    caps.supports_cooperative_groups = prop.cooperativeLaunch;
    caps.supports_async_copy = prop.asyncEngineCount > 0;

    // Calculate derived capabilities
    caps.memory_bandwidth_gb_per_sec = EstimateMemoryBandwidth(prop);
    caps.clock_rate_mhz = prop.clockRate / 1000.0;
    caps.l2_cache_size_kb = prop.l2CacheSize / 1024;

    // Get current memory usage
    size_t free_memory = 0;
    size_t total_memory = 0;
    cudaMemGetInfo(&free_memory, &total_memory);
    caps.free_memory_mb = free_memory / (1024 * 1024);

    return caps;
}

bool AdaptiveParallelismScaling::IsArchitectureSupported(const GpuCapabilities& caps) const {
    // Minimum requirement: Compute capability 7.0 (Volta) or higher
    return caps.compute_capability >= 70;
}

std::vector<GpuCapabilities> AdaptiveParallelismScaling::DetectAllGpus() const {
    std::vector<GpuCapabilities> all_gpus;

    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);
    if (error != cudaSuccess) {
        return all_gpus;
    }

    for (int i = 0; i < device_count; ++i) {
        try {
            GpuCapabilities caps = DetectGpuCapabilities(i);
            if (IsArchitectureSupported(caps)) {
                all_gpus.push_back(caps);
            }
        } catch (const std::exception&) {
            // Skip devices that can't be probed
            continue;
        }
    }

    return all_gpus;
}

ScalingDecision AdaptiveParallelismScaling::AdaptConfiguration(
    const ParallelismConfiguration& current_config,
    const PerformanceFeedback& recent_feedback) {

    std::lock_guard<std::mutex> lock(scaling_mutex_);

    ScalingDecision decision;
    decision.gpu_capabilities = current_gpu_capabilities_;
    decision.automatic_scaling_enabled = automatic_scaling_enabled_;
    decision.decision_time = std::chrono::system_clock::now();

    // Analyze performance issues
    std::vector<std::string> constraints;

    if (recent_feedback.actual_throughput_mkeys_per_sec < min_throughput_target_) {
        constraints.push_back("increase_throughput");
    }

    if (recent_feedback.actual_memory_utilization > max_memory_utilization_target_) {
        constraints.push_back("reduce_memory_usage");
    }

    if (recent_feedback.actual_occupancy < 0.5) {
        constraints.push_back("increase_occupancy");
    }

    if (!recent_feedback.accuracy_maintained) {
        constraints.push_back("ensure_accuracy");
    }

    // Generate adapted configuration
    if (constraints.empty()) {
        // Performance is good, return current config
        decision.selected_config = current_config;
        decision.decision_logic = "no_adaptation_needed";
        decision.confidence_score = 0.9;
    } else {
        // Need to adapt
        ParallelismConfiguration adapted_config = current_config;

        // Apply adaptations based on constraints
        for (const auto& constraint : constraints) {
            if (constraint == "increase_throughput") {
                adapted_config.points_per_thread = std::min(256, adapted_config.points_per_thread * 2);
                adapted_config.block_size = std::min(1024, adapted_config.block_size + 128);
            } else if (constraint == "reduce_memory_usage") {
                adapted_config.points_per_thread = std::max(64, adapted_config.points_per_thread / 2);
            } else if (constraint == "increase_occupancy") {
                adapted_config.block_size = std::max(256, adapted_config.block_size - 128);
                adapted_config.grid_size = std::min(current_gpu_capabilities_.sm_count * 4,
                                                  adapted_config.grid_size + 1);
            } else if (constraint == "ensure_accuracy") {
                adapted_config = GetSafeConfiguration();
            }
        }

        // Validate adapted configuration
        if (ValidateConfiguration(adapted_config)) {
            decision.selected_config = adapted_config;
            decision.decision_logic = "performance_adaptation";
            decision.confidence_score = 0.7;
            constraints_applied_ = constraints;
        } else {
            decision.selected_config = GetSafeConfiguration();
            decision.decision_logic = "adaptation_failed_fallback";
            decision.confidence_score = 0.5;
        }
    }

    return decision;
}

ParallelismConfiguration AdaptiveParallelismScaling::ScaleForMemoryConstraints(
    const ParallelismConfiguration& base_config,
    size_t available_memory_mb) {

    ParallelismConfiguration scaled_config = base_config;

    // Advanced memory-aware scaling algorithm
    size_t current_usage = EstimateMemoryUsage(base_config, 1000000); // 1M points estimate

    if (current_usage > available_memory_mb) {
        // Calculate precise scaling factor
        double reduction_factor = static_cast<double>(available_memory_mb) / current_usage;

        // Apply intelligent scaling strategy based on memory pressure
        if (reduction_factor > 0.7) {
            // Mild memory constraint - primarily reduce points_per_thread
            scaled_config.points_per_thread = std::max(64,
                static_cast<int>(base_config.points_per_thread * std::sqrt(reduction_factor)));
        } else if (reduction_factor > 0.4) {
            // Moderate memory constraint - reduce both PPT and block size
            scaled_config.points_per_thread = std::max(64,
                static_cast<int>(base_config.points_per_thread * reduction_factor));
            scaled_config.block_size = std::max(256,
                static_cast<int>(base_config.block_size * std::sqrt(reduction_factor)));
        } else {
            // Severe memory constraint - aggressive scaling
            scaled_config.points_per_thread = 64; // Minimum viable
            scaled_config.block_size = 256;       // Conservative block size
            scaled_config.grid_size = std::max(1,
                static_cast<int>(base_config.grid_size * reduction_factor * 0.8));
        }

        // Recalculate memory usage after scaling
        size_t scaled_usage = EstimateMemoryUsage(scaled_config, 1000000);
        scaled_config.memory_utilization_estimate = static_cast<double>(scaled_usage) / available_memory_mb;

        // Add optimization rationale
        scaled_config.configuration_rationale = "memory_constrained_scaling_" +
                                              std::to_string(static_cast<int>(reduction_factor * 100)) + "percent";
    } else {
        // Memory available - consider scaling up if beneficial
        double memory_utilization = static_cast<double>(current_usage) / available_memory_mb;
        if (memory_utilization < 0.5 && available_memory_mb > 2000) { // Plenty of memory available
            // Can potentially increase parallelism
            scaled_config.points_per_thread = std::min(256, base_config.points_per_thread + 32);
            scaled_config.memory_utilization_estimate = memory_utilization;
        }
    }

    // Log the memory constraint adjustment
    if (configuration_logger_ && scaled_config.configuration_rationale != base_config.configuration_rationale) {
        ConfigurationDecision log_decision;
        log_decision.decision_type = DecisionType::MEMORY_CONSTRAINT_ADJUSTMENT;
        log_decision.input_config = base_config;
        log_decision.output_config = scaled_config;
        log_decision.gpu_name = current_gpu_capabilities_.device_name;
        log_decision.compute_capability = current_gpu_capabilities_.compute_capability;
        log_decision.total_memory_mb = current_gpu_capabilities_.total_memory_mb;
        log_decision.free_memory_mb = current_gpu_capabilities_.free_memory_mb;
        log_decision.memory_bandwidth_gb_per_sec = current_gpu_capabilities_.memory_bandwidth_gb_per_sec;
        log_decision.sm_count = current_gpu_capabilities_.sm_count;
        log_decision.workload_size = 1000000; // Estimated workload size
        log_decision.operation_type = "memory_scaling";
        log_decision.decision_logic = "memory_aware_scaling";
        log_decision.rationale = scaled_config.configuration_rationale;
        log_decision.confidence_score = 0.85;
        log_decision.memory_utilization_estimate = scaled_config.memory_utilization_estimate;

        configuration_logger_->LogMemoryConstraintAdjustment(
            base_config, scaled_config, current_gpu_capabilities_,
            scaled_config.memory_utilization_estimate, "memory_constraint");
    }

    return scaled_config;
}

ParallelismConfiguration AdaptiveParallelismScaling::ApplyMemoryAwareScaling(
    const ParallelismConfiguration& config,
    const GpuCapabilities& gpu_caps,
    size_t workload_size) const {

    ParallelismConfiguration memory_aware_config = config;

    // Calculate detailed memory requirements
    size_t base_memory_usage = EstimateDetailedMemoryUsage(config, workload_size);
    size_t available_memory_mb = gpu_caps.free_memory_mb;
    double memory_utilization = static_cast<double>(base_memory_usage) / (available_memory_mb * 1024 * 1024);

    // Apply memory-aware optimizations based on utilization levels
    if (memory_utilization > 0.9) {
        // Critical memory pressure - aggressive optimization
        memory_aware_config = ApplyCriticalMemoryOptimization(config, gpu_caps, workload_size);
    } else if (memory_utilization > 0.75) {
        // High memory pressure - moderate optimization
        memory_aware_config = ApplyHighMemoryOptimization(config, gpu_caps, workload_size);
    } else if (memory_utilization > 0.6) {
        // Moderate memory pressure - light optimization
        memory_aware_config = ApplyModerateMemoryOptimization(config, gpu_caps, workload_size);
    } else {
        // Low memory pressure - can optimize for performance
        memory_aware_config = ApplyPerformanceOptimization(config, gpu_caps, workload_size);
    }

    // Validate and apply final adjustments
    memory_aware_config.memory_utilization_estimate = memory_utilization;

    return memory_aware_config;
}

size_t AdaptiveParallelismScaling::EstimateDetailedMemoryUsage(
    const ParallelismConfiguration& config,
    size_t workload_size) const {

    // Detailed memory usage estimation in bytes

    // Base kernel memory (code, constants, etc.)
    size_t base_kernel_memory = 50 * 1024 * 1024; // 50MB

    // Working set memory for point calculations
    size_t working_set_memory = config.block_size * config.grid_size * config.points_per_thread * 32;

    // Shared memory per block * number of blocks
    size_t shared_memory_total = config.shared_memory_size * config.grid_size;

    // Global memory buffers and intermediate storage
    size_t global_buffers = (workload_size * 8) + (config.block_size * config.grid_size * 16);

    // CUDA context and driver overhead
    size_t system_overhead = 100 * 1024 * 1024; // 100MB

    // Memory fragmentation and safety margin
    size_t safety_margin = (base_kernel_memory + working_set_memory + shared_memory_total + global_buffers + system_overhead) * 0.1;

    return base_kernel_memory + working_set_memory + shared_memory_total + global_buffers + system_overhead + safety_margin;
}

ParallelismConfiguration AdaptiveParallelismScaling::ApplyCriticalMemoryOptimization(
    const ParallelismConfiguration& config,
    const GpuCapabilities& gpu_caps,
    size_t workload_size) const {

    ParallelismConfiguration optimized = config;

    // Maximum memory conservation mode
    optimized.points_per_thread = 64;  // Minimum viable
    optimized.block_size = 256;          // Smallest safe block size
    optimized.grid_size = std::max(1, gpu_caps.sm_count / 2); // Reduce grid size

    // Minimize shared memory usage
    optimized.shared_memory_size = std::min(size_t(8192), gpu_caps.shared_memory_per_block / 4);

    // Conservative register usage
    optimized.registers_per_thread = 32;

    optimized.configuration_rationale = "critical_memory_optimization";

    return optimized;
}

ParallelismConfiguration AdaptiveParallelismScaling::ApplyHighMemoryOptimization(
    const ParallelismConfiguration& config,
    const GpuCapabilities& gpu_caps,
    size_t workload_size) const {

    ParallelismConfiguration optimized = config;

    // Reduce memory usage while maintaining reasonable performance
    optimized.points_per_thread = std::max(64, config.points_per_thread * 3 / 4);
    optimized.block_size = std::max(256, config.block_size * 4 / 5);

    // Adjust grid size based on available SMs
    optimized.grid_size = std::max(1, gpu_caps.sm_count * 2 / 3);

    // Reduce shared memory usage
    optimized.shared_memory_size = std::min(size_t(16384), config.shared_memory_size * 2 / 3);

    optimized.configuration_rationale = "high_memory_optimization";

    return optimized;
}

ParallelismConfiguration AdaptiveParallelismScaling::ApplyModerateMemoryOptimization(
    const ParallelismConfiguration& config,
    const GpuCapabilities& gpu_caps,
    size_t workload_size) const {

    ParallelismConfiguration optimized = config;

    // Light memory optimization
    if (config.points_per_thread > 128) {
        optimized.points_per_thread = config.points_per_thread * 5 / 6;
    }

    // Small block size adjustment if needed
    if (config.block_size > 512) {
        optimized.block_size = config.block_size * 9 / 10;
    }

    optimized.configuration_rationale = "moderate_memory_optimization";

    return optimized;
}

ParallelismConfiguration AdaptiveParallelismScaling::ApplyPerformanceOptimization(
    const ParallelismConfiguration& config,
    const GpuCapabilities& gpu_caps,
    size_t workload_size) const {

    ParallelismConfiguration optimized = config;

    // Memory is available - can optimize for performance
    if (gpu_caps.free_memory_mb > 4000) { // Plenty of memory
        // Increase points per thread if beneficial
        if (config.points_per_thread < 192) {
            optimized.points_per_thread = std::min(256, config.points_per_thread + 32);
        }

        // Optimize block size for better occupancy
        int optimal_block_size = SelectOptimalBlockSizeForWorkload(
            gpu_caps, workload_size, gpu_caps.free_memory_mb);
        optimized.block_size = optimal_block_size;
    }

    optimized.configuration_rationale = "performance_optimization";

    return optimized;
}

std::vector<ParallelismConfiguration> AdaptiveParallelismScaling::GenerateMemoryAwareConfigurations(
    const GpuCapabilities& gpu_caps,
    size_t workload_size,
    const std::map<std::string, std::string>& constraints) const {

    std::vector<ParallelismConfiguration> configurations;

    // Generate base configurations for different memory usage levels
    std::vector<std::string> memory_levels = {"conservative", "balanced", "aggressive"};

    for (const auto& level : memory_levels) {
        ParallelismConfiguration base_config;

        if (level == "conservative") {
            base_config.block_size = 256;
            base_config.points_per_thread = 64;
            base_config.grid_size = gpu_caps.sm_count;
        } else if (level == "balanced") {
            base_config.block_size = 512;
            base_config.points_per_thread = 128;
            base_config.grid_size = gpu_caps.sm_count * 2;
        } else { // aggressive
            base_config.block_size = 768;
            base_config.points_per_thread = 192;
            base_config.grid_size = gpu_caps.sm_count * 3;
        }

        // Apply memory-aware scaling
        ParallelismConfiguration memory_aware_config = ApplyMemoryAwareScaling(
            base_config, gpu_caps, workload_size);

        // Apply constraints
        ApplyConstraints(memory_aware_config, constraints);

        // Set derived values
        memory_aware_config.expected_occupancy = CalculateOccupancy(memory_aware_config, gpu_caps);
        memory_aware_config.estimated_execution_time = EstimateExecutionTime(
            memory_aware_config, gpu_caps, workload_size);

        configurations.push_back(memory_aware_config);
    }

    return configurations;
}

ParallelismConfiguration AdaptiveParallelismScaling::ScaleForThroughputTarget(
    const ParallelismConfiguration& base_config,
    double target_throughput_mkeys_per_sec) {

    ParallelismConfiguration scaled_config = base_config;

    // Estimate current throughput
    double estimated_throughput = base_config.points_per_thread * base_config.block_size *
                                 base_config.grid_size * GetArchitecturePerformanceFactor(current_gpu_capabilities_);

    if (estimated_throughput < target_throughput_mkeys_per_sec) {
        double scaling_factor = target_throughput_mkeys_per_sec / estimated_throughput;

        // Scale up parallelism
        scaled_config.points_per_thread = std::min(256,
            static_cast<int>(base_config.points_per_thread * std::sqrt(scaling_factor)));

        scaled_config.block_size = std::min(1024,
            static_cast<int>(base_config.block_size * std::sqrt(scaling_factor)));

        scaled_config.grid_size = std::min(current_gpu_capabilities_.sm_count * 4,
            static_cast<int>(base_config.grid_size * scaling_factor));
    }

    return scaled_config;
}

bool AdaptiveParallelismScaling::ValidateConfiguration(const ParallelismConfiguration& config) const {
    return ValidateBlockSize(config.block_size, current_gpu_capabilities_) &&
           ValidatePointsPerThread(config.points_per_thread, current_gpu_capabilities_) &&
           ValidateMemoryUsage(config, current_gpu_capabilities_) &&
           ValidateRegisterUsage(config, current_gpu_capabilities_);
}

std::vector<std::string> AdaptiveParallelismScaling::GetConfigurationWarnings(
    const ParallelismConfiguration& config) const {

    std::vector<std::string> warnings;

    if (config.points_per_thread < 64 || config.points_per_thread > 256) {
        warnings.push_back("points_per_thread outside optimal range (64-256)");
    }

    if (config.block_size < 256 || config.block_size > 1024) {
        warnings.push_back("block_size outside optimal range (256-1024)");
    }

    if (config.expected_occupancy < 0.3) {
        warnings.push_back("low expected occupancy (< 30%)");
    }

    if (config.memory_utilization_estimate > 0.9) {
        warnings.push_back("high memory utilization estimate (> 90%)");
    }

    if (config.estimated_execution_time.count() > 60000000) { // > 1 minute
        warnings.push_back("long estimated execution time (> 1 minute)");
    }

    return warnings;
}

bool AdaptiveParallelismScaling::TestConfiguration(const ParallelismConfiguration& config, size_t test_size) {
    // Implementation would run a small test with the configuration
    // For now, just validate the configuration
    return ValidateConfiguration(config);
}

void AdaptiveParallelismScaling::EnableLearningMode(bool enabled) {
    std::lock_guard<std::mutex> lock(scaling_mutex_);
    learning_mode_enabled_ = enabled;
}

void AdaptiveParallelismScaling::SetOptimizationStrategy(const std::string& strategy) {
    std::lock_guard<std::mutex> lock(scaling_mutex_);
    optimization_strategy_ = strategy;

    // Adjust weights based on strategy
    if (strategy == "throughput") {
        optimization_weights_["compute_throughput"] = 0.5;
        optimization_weights_["memory_bandwidth"] = 0.3;
        optimization_weights_["occupancy"] = 0.2;
    } else if (strategy == "efficiency") {
        optimization_weights_["power_efficiency"] = 0.4;
        optimization_weights_["thermal_headroom"] = 0.3;
        optimization_weights_["occupancy"] = 0.3;
    } else if (strategy == "balanced") {
        // Use default balanced weights
    }
}

std::map<std::string, double> AdaptiveParallelismScaling::GetPerformanceHistory() const {
    std::lock_guard<std::mutex> lock(scaling_mutex_);
    std::map<std::string, double> history;

    for (const auto& entry : performance_history_) {
        double avg_performance = 0.0;
        for (const auto& feedback_pair : entry.second) {
            avg_performance += feedback_pair.second.actual_throughput_mkeys_per_sec;
        }
        avg_performance /= entry.second.size();
        history[entry.first] = avg_performance;
    }

    return history;
}

std::vector<ParallelismConfiguration> AdaptiveParallelismScaling::GetOptimalConfigurations() const {
    std::lock_guard<std::mutex> lock(scaling_mutex_);
    std::vector<ParallelismConfiguration> optimal_configs;

    for (const auto& decision : decision_history_) {
        if (decision.confidence_score >= 0.8) {
            optimal_configs.push_back(decision.selected_config);
        }
    }

    return optimal_configs;
}

void AdaptiveParallelismScaling::SetConfigurationConstraints(
    const std::map<std::string, std::string>& constraints) {
    std::lock_guard<std::mutex> lock(scaling_mutex_);
    current_constraints_ = constraints;
}

void AdaptiveParallelismScaling::SetPerformanceTargets(
    double min_throughput, double max_memory_utilization) {
    std::lock_guard<std::mutex> lock(scaling_mutex_);
    min_throughput_target_ = min_throughput;
    max_memory_utilization_target_ = max_memory_utilization;
}

void AdaptiveParallelismScaling::EnableAutomaticScaling(bool enabled) {
    std::lock_guard<std::mutex> lock(scaling_mutex_);
    automatic_scaling_enabled_ = enabled;
}

void AdaptiveParallelismScaling::SetScalingAggressiveness(double aggressiveness) {
    std::lock_guard<std::mutex> lock(scaling_mutex_);
    scaling_aggressiveness_ = std::clamp(aggressiveness, 0.0, 1.0);
}

json AdaptiveParallelismScaling::GetPerformanceAnalytics() const {
    std::lock_guard<std::mutex> lock(scaling_mutex_);

    json analytics;
    analytics["gpu_capabilities"] = GpuCapabilitiesToJson(current_gpu_capabilities_);
    analytics["decision_count"] = decision_history_.size();
    analytics["failed_configurations"] = failed_configurations_.size();
    analytics["learning_mode_enabled"] = learning_mode_enabled_;
    analytics["automatic_scaling_enabled"] = automatic_scaling_enabled_;
    analytics["scaling_aggressiveness"] = scaling_aggressiveness_;
    analytics["optimization_strategy"] = optimization_strategy_;

    // Performance statistics
    if (!decision_history_.empty()) {
        double avg_confidence = 0.0;
        for (const auto& decision : decision_history_) {
            avg_confidence += decision.confidence_score;
        }
        avg_confidence /= decision_history_.size();
        analytics["average_confidence_score"] = avg_confidence;
    }

    // Architecture performance
    json arch_perf;
    for (const auto& entry : architecture_performance_cache_) {
        arch_perf[entry.first] = entry.second;
    }
    analytics["architecture_performance_factors"] = arch_perf;

    return analytics;
}

std::string AdaptiveParallelismScaling::GenerateConfigurationReport() const {
    json analytics = GetPerformanceAnalytics();

    std::stringstream report;
    report << "=== Adaptive Parallelism Scaling Configuration Report ===\n\n";

    report << "GPU Device: " << current_gpu_capabilities_.device_name
           << " (Compute " << current_gpu_capabilities_.compute_capability / 10 << "."
           << current_gpu_capabilities_.compute_capability % 10 << ")\n";
    report << "Total Memory: " << current_gpu_capabilities_.total_memory_mb << " MB\n";
    report << "Streaming Multiprocessors: " << current_gpu_capabilities_.sm_count << "\n";
    report << "Max Threads per SM: " << current_gpu_capabilities_.max_threads_per_sm << "\n\n";

    report << "Scaling Configuration:\n";
    report << "  Learning Mode: " << (learning_mode_enabled_ ? "Enabled" : "Disabled") << "\n";
    report << "  Automatic Scaling: " << (automatic_scaling_enabled_ ? "Enabled" : "Disabled") << "\n";
    report << "  Scaling Aggressiveness: " << scaling_aggressiveness_ * 100 << "%\n";
    report << "  Optimization Strategy: " << optimization_strategy_ << "\n";
    report << "  Min Throughput Target: " << min_throughput_target_ << " Mkeys/sec\n";
    report << "  Max Memory Utilization: " << max_memory_utilization_target_ * 100 << "%\n\n";

    report << "Performance Statistics:\n";
    report << "  Total Decisions: " << decision_history_.size() << "\n";
    report << "  Failed Configurations: " << failed_configurations_.size() << "\n";
    if (!decision_history_.empty()) {
        double avg_confidence = analytics["average_confidence_score"];
        report << "  Average Confidence: " << avg_confidence * 100 << "%\n";
    }

    return report.str();
}

std::vector<std::string> AdaptiveParallelismScaling::GetOptimizationRecommendations() const {
    std::vector<std::string> recommendations;

    if (current_gpu_capabilities_.compute_capability < 80) {
        recommendations.push_back("Consider upgrading to Ampere (80) or newer GPU for better performance");
    }

    if (current_gpu_capabilities_.total_memory_mb < 8000) {
        recommendations.push_back("Consider GPU with more memory for larger workloads");
    }

    if (!learning_mode_enabled_) {
        recommendations.push_back("Enable learning mode for adaptive optimization");
    }

    if (!automatic_scaling_enabled_) {
        recommendations.push_back("Enable automatic scaling for dynamic performance tuning");
    }

    if (scaling_aggressiveness_ < 0.5) {
        recommendations.push_back("Increase scaling aggressiveness for faster optimization");
    }

    return recommendations;
}

double AdaptiveParallelismScaling::GetScalingEffectiveness() const {
    std::lock_guard<std::mutex> lock(scaling_mutex_);

    if (decision_history_.empty()) {
        return 0.0;
    }

    double effectiveness_sum = 0.0;
    int effective_decisions = 0;

    for (const auto& decision : decision_history_) {
        if (decision.confidence_score >= 0.7) {
            effectiveness_sum += decision.confidence_score;
            effective_decisions++;
        }
    }

    return effective_decisions > 0 ? effectiveness_sum / effective_decisions : 0.0;
}

ParallelismConfiguration AdaptiveParallelismScaling::GetSafeConfiguration() const {
    ParallelismConfiguration safe_config;

    safe_config.block_size = 512;
    safe_config.points_per_thread = 128;
    safe_config.grid_size = current_gpu_capabilities_.sm_count;
    safe_config.shared_memory_size = current_gpu_capabilities_.shared_memory_per_block / 2;
    safe_config.registers_per_thread = 64;
    safe_config.expected_occupancy = 0.75;
    safe_config.memory_utilization_estimate = 0.5;

    safe_config.configuration_rationale = "safe_fallback_configuration";

    return safe_config;
}

bool AdaptiveParallelismScaling::HasConfigurationFailed(const ParallelismConfiguration& config) const {
    std::lock_guard<std::mutex> lock(scaling_mutex_);
    return failed_configurations_.find(config) != failed_configurations_.end();
}

void AdaptiveParallelismScaling::MarkConfigurationAsFailed(const ParallelismConfiguration& config) {
    std::lock_guard<std::mutex> lock(scaling_mutex_);
    failed_configurations_.insert(config);
}

std::vector<ParallelismConfiguration> AdaptiveParallelismScaling::GetFallbackConfigurations() const {
    std::vector<ParallelismConfiguration> fallback_configs;

    // Ultra-conservative config
    ParallelismConfiguration ultra_conservative;
    ultra_conservative.block_size = 256;
    ultra_conservative.points_per_thread = 64;
    ultra_conservative.grid_size = current_gpu_capabilities_.sm_count / 2;
    ultra_conservative.configuration_rationale = "ultra_conservative_fallback";
    fallback_configs.push_back(ultra_conservative);

    // Memory-optimized config
    ParallelismConfiguration memory_optimized;
    memory_optimized.block_size = 512;
    memory_optimized.points_per_thread = 32;
    memory_optimized.grid_size = current_gpu_capabilities_.sm_count;
    memory_optimized.configuration_rationale = "memory_optimized_fallback";
    fallback_configs.push_back(memory_optimized);

    // Throughput-optimized config
    ParallelismConfiguration throughput_optimized;
    throughput_optimized.block_size = 1024;
    throughput_optimized.points_per_thread = 256;
    throughput_optimized.grid_size = current_gpu_capabilities_.sm_count * 2;
    throughput_optimized.configuration_rationale = "throughput_optimized_fallback";
    fallback_configs.push_back(throughput_optimized);

    return fallback_configs;
}

ParallelismConfiguration AdaptiveParallelismScaling::ActivateAutomaticFallback(
    const std::string& failure_reason,
    const ParallelismConfiguration& failed_config,
    size_t workload_size) {

    std::lock_guard<std::mutex> lock(scaling_mutex_);

    // Mark the failed configuration
    failed_configurations_.insert(failed_config);

    // Log the fallback activation
    if (configuration_logger_) {
        ConfigurationDecision log_decision;
        log_decision.decision_type = DecisionType::FALLBACK_ACTIVATION;
        log_decision.input_config = failed_config;
        log_decision.gpu_name = current_gpu_capabilities_.device_name;
        log_decision.compute_capability = current_gpu_capabilities_.compute_capability;
        log_decision.total_memory_mb = current_gpu_capabilities_.total_memory_mb;
        log_decision.free_memory_mb = current_gpu_capabilities_.free_memory_mb;
        log_decision.workload_size = workload_size;
        log_decision.operation_type = "fallback_activation";
        log_decision.decision_logic = "automatic_fallback";
        log_decision.rationale = failure_reason;
        log_decision.confidence_score = 0.9;
        log_decision.error_message = failure_reason;

        // Get available fallback configurations
        auto fallback_configs = GetFallbackConfigurations();
        std::vector<std::string> attempted_configs;

        // Try fallback configurations in order of appropriateness
        for (const auto& config : fallback_configs) {
            if (!HasConfigurationFailed(config)) {
                // Validate the fallback configuration
                if (ValidateConfiguration(config)) {
                    log_decision.output_config = config;
                    configuration_logger_->LogFallbackActivation(failure_reason, config, attempted_configs);

                    // Log successful fallback activation
                    std::string rationale = "Activated fallback: " + config.configuration_rationale +
                                          " due to: " + failure_reason;
                    log_decision.rationale = rationale;
                    configuration_logger_->LogConfigurationDecision(log_decision);

                    return config;
                } else {
                    attempted_configs.push_back(config.configuration_rationale + " (validation_failed)");
                }
            } else {
                attempted_configs.push_back(config.configuration_rationale + " (previously_failed)");
            }
        }

        // All fallbacks failed - use ultra-safe configuration
        ParallelismConfiguration emergency_config = GetSafeConfiguration();
        log_decision.output_config = emergency_config;
        attempted_configs.push_back("all_fallbacks_failed");
        configuration_logger_->LogFallbackActivation(failure_reason, emergency_config, attempted_configs);
        configuration_logger_->LogConfigurationDecision(log_decision);

        return emergency_config;
    }

    // No logger available - use safe fallback
    return GetSafeConfiguration();
}

bool AdaptiveParallelismScaling::DetectResourceConstraints(
    const ParallelismConfiguration& config,
    size_t workload_size,
    std::string& constraint_type) const {

    // Check memory constraints
    size_t estimated_memory = EstimateDetailedMemoryUsage(config, workload_size);
    if (estimated_memory > current_gpu_capabilities_.free_memory_mb * 1024 * 1024) {
        constraint_type = "memory_exhaustion";
        return true;
    }

    // Check compute constraints
    if (config.expected_occupancy < 0.1) {
        constraint_type = "insufficient_occupancy";
        return true;
    }

    // Check register constraints
    if (config.registers_per_thread > current_gpu_capabilities_.max_registers_per_thread) {
        constraint_type = "register_limit_exceeded";
        return true;
    }

    // Check shared memory constraints
    if (config.shared_memory_size > current_gpu_capabilities_.shared_memory_per_block) {
        constraint_type = "shared_memory_exceeded";
        return true;
    }

    // Check thread constraints
    int total_threads = config.block_size * config.grid_size;
    int max_threads = current_gpu_capabilities_.sm_count * current_gpu_capabilities_.max_threads_per_sm;
    if (total_threads > max_threads) {
        constraint_type = "thread_limit_exceeded";
        return true;
    }

    return false;
}

ParallelismConfiguration AdaptiveParallelismScaling::GetResourceConstraintAwareFallback(
    const std::string& constraint_type,
    size_t workload_size) const {

    auto fallback_configs = GetFallbackConfigurations();

    // Select fallback based on constraint type
    if (constraint_type == "memory_exhaustion") {
        // Prioritize memory-optimized fallback
        for (const auto& config : fallback_configs) {
            if (config.configuration_rationale == "memory_optimized_fallback" &&
                !HasConfigurationFailed(config)) {
                return config;
            }
        }
    } else if (constraint_type == "insufficient_occupancy") {
        // Prioritize compute-optimized fallback
        for (const auto& config : fallback_configs) {
            if (config.configuration_rationale == "compute_optimized_fallback" &&
                !HasConfigurationFailed(config)) {
                return config;
            }
        }
    } else if (constraint_type == "register_limit_exceeded" ||
               constraint_type == "shared_memory_exceeded") {
        // Use ultra-conservative fallback
        for (const auto& config : fallback_configs) {
            if (config.configuration_rationale == "ultra_conservative_fallback" &&
                !HasConfigurationFailed(config)) {
                return config;
            }
        }
    }

    // Default to safe configuration
    return GetSafeConfiguration();
}

// Private method implementations

std::vector<ParallelismConfiguration> AdaptiveParallelismScaling::GenerateCandidateConfigurations(
    const GpuCapabilities& gpu_caps,
    size_t workload_size) const {

    std::vector<ParallelismConfiguration> candidates;

    // Generate block size candidates
    std::vector<int> block_sizes = {256, 384, 512, 640, 768, 896, 1024};

    // Generate points per thread candidates
    std::vector<int> ppt_values = {64, 96, 128, 160, 192, 224, 256};

    // Generate grid size candidates
    std::vector<int> grid_sizes;
    for (int i = 1; i <= 4; ++i) {
        grid_sizes.push_back(gpu_caps.sm_count * i);
    }

    // Create combinations
    for (int block_size : block_sizes) {
        if (!ValidateBlockSize(block_size, gpu_caps)) continue;

        for (int ppt : ppt_values) {
            if (!ValidatePointsPerThread(ppt, gpu_caps)) continue;

            for (int grid_size : grid_sizes) {
                ParallelismConfiguration config;
                config.block_size = block_size;
                config.points_per_thread = ppt;
                config.grid_size = grid_size;
                config.shared_memory_size = gpu_caps.shared_memory_per_block / 2;
                config.registers_per_thread = 64;
                config.configuration_rationale = "candidate_combination";

                candidates.push_back(config);
            }
        }
    }

    return candidates;
}

double AdaptiveParallelismScaling::EstimateConfigurationPerformance(
    const ParallelismConfiguration& config,
    const GpuCapabilities& gpu_caps,
    size_t workload_size) const {

    double score = 0.0;

    // Occupancy factor
    double occupancy = CalculateOccupancy(config, gpu_caps);
    score += occupancy * optimization_weights_.at("occupancy");

    // Memory bandwidth factor
    double bandwidth_factor = GetMemoryBandwidthFactor(config);
    score += bandwidth_factor * optimization_weights_.at("memory_bandwidth");

    // Compute throughput factor
    double compute_factor = config.points_per_thread * config.block_size * config.grid_size;
    compute_factor = compute_factor / (1000000.0); // Normalize
    score += std::min(1.0, compute_factor) * optimization_weights_.at("compute_throughput");

    // Architecture factor
    double arch_factor = GetArchitecturePerformanceFactor(gpu_caps);
    score *= arch_factor;

    // Memory utilization penalty
    if (config.memory_utilization_estimate > 0.8) {
        score *= 0.8; // 20% penalty for high memory usage
    }

    return std::min(1.0, score);
}

double AdaptiveParallelismScaling::CalculateOccupancy(
    const ParallelismConfiguration& config,
    const GpuCapabilities& gpu_caps) const {

    // Calculate threads per SM
    int threads_per_block = config.block_size;
    int max_blocks_per_sm = gpu_caps.max_blocks_per_sm;
    int max_threads_per_sm = gpu_caps.max_threads_per_sm;

    int blocks_per_sm = std::min(max_blocks_per_sm, max_threads_per_sm / threads_per_block);
    int active_threads_per_sm = blocks_per_sm * threads_per_block;

    // Calculate occupancy
    double occupancy = static_cast<double>(active_threads_per_sm) / max_threads_per_sm;

    return std::min(1.0, occupancy);
}

double AdaptiveParallelismScaling::EstimateMemoryUsage(
    const ParallelismConfiguration& config,
    size_t workload_size) const {

    // Estimate memory usage in MB
    size_t base_memory = 100; // Base kernel memory usage in MB

    size_t working_set_memory = (config.block_size * config.grid_size * config.points_per_thread * 32) / (1024 * 1024);

    size_t shared_memory_usage = (config.shared_memory_size * config.grid_size) / 1024;

    size_t total_memory = base_memory + working_set_memory + shared_memory_usage;

    return static_cast<double>(total_memory);
}

double AdaptiveParallelismScaling::EstimateExecutionTime(
    const ParallelismConfiguration& config,
    const GpuCapabilities& gpu_caps,
    size_t workload_size) const {

    // Simplified execution time estimation
    double ops_per_point = 1000; // Estimated operations per point
    double total_ops = workload_size * config.points_per_thread * ops_per_point;

    double gpu_ops_per_sec = gpu_caps.clock_rate_mhz * 1000000 * gpu_caps.sm_count * 64; // Estimate

    double execution_time_sec = total_ops / gpu_ops_per_sec;

    return std::chrono::microseconds(static_cast<long long>(execution_time_sec * 1000000));
}

bool AdaptiveParallelismScaling::ApplyConstraints(
    ParallelismConfiguration& config,
    const std::map<std::string, std::string>& constraints) const {

    bool modified = false;

    for (const auto& constraint : constraints) {
        if (constraint.first == "max_block_size") {
            int max_size = std::stoi(constraint.second);
            if (config.block_size > max_size) {
                config.block_size = max_size;
                modified = true;
            }
        } else if (constraint.first == "max_points_per_thread") {
            int max_ppt = std::stoi(constraint.second);
            if (config.points_per_thread > max_ppt) {
                config.points_per_thread = max_ppt;
                modified = true;
            }
        } else if (constraint.first == "max_memory_utilization") {
            double max_util = std::stod(constraint.second);
            if (config.memory_utilization_estimate > max_util) {
                // Scale down configuration
                double scaling_factor = max_util / config.memory_utilization_estimate;
                config.points_per_thread = static_cast<int>(config.points_per_thread * scaling_factor);
                modified = true;
            }
        }
    }

    return modified;
}

void AdaptiveParallelismScaling::UpdateLearningModel(
    const ParallelismConfiguration& config,
    const PerformanceFeedback& feedback) {

    std::string key = GenerateConfigurationKey(config);

    // Update architecture performance cache
    std::string arch_key = std::to_string(current_gpu_capabilities_.compute_capability);
    if (architecture_performance_cache_.find(arch_key) == architecture_performance_cache_.end()) {
        architecture_performance_cache_[arch_key] = feedback.actual_throughput_mkeys_per_sec / 1000.0;
    } else {
        // Update with exponential moving average
        double alpha = 0.1;
        double current_factor = architecture_performance_cache_[arch_key];
        double measured_factor = feedback.actual_throughput_mkeys_per_sec / 1000.0;
        architecture_performance_cache_[arch_key] = alpha * measured_factor + (1.0 - alpha) * current_factor;
    }
}

std::string AdaptiveParallelismScaling::GenerateConfigurationKey(const ParallelismConfiguration& config) const {
    return std::to_string(config.block_size) + "_" +
           std::to_string(config.points_per_thread) + "_" +
           std::to_string(config.grid_size);
}

std::string AdaptiveParallelismScaling::SelectOptimalBlockSizes(const GpuCapabilities& gpu_caps) const {
    // Enhanced occupancy-based block size selection
    auto optimal_sizes = CalculateOptimalBlockSizesOccupancyBased(gpu_caps);

    std::stringstream result;
    for (size_t i = 0; i < optimal_sizes.size(); ++i) {
        if (i > 0) result << ",";
        result << optimal_sizes[i];
    }
    return result.str();
}

std::vector<int> AdaptiveParallelismScaling::CalculateOptimalBlockSizesOccupancyBased(const GpuCapabilities& gpu_caps) const {
    std::vector<int> candidate_sizes = {256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960, 1024};
    std::vector<std::pair<int, double>> size_occupancy_pairs;

    // Calculate occupancy for each block size
    for (int block_size : candidate_sizes) {
        if (!ValidateBlockSize(block_size, gpu_caps)) continue;

        // Estimate occupancy based on GPU architecture and block size
        double occupancy = EstimateOccupancyForBlockSize(block_size, gpu_caps);
        size_occupancy_pairs.emplace_back(block_size, occupancy);
    }

    // Sort by occupancy (descending)
    std::sort(size_occupancy_pairs.begin(), size_occupancy_pairs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Extract top block sizes with minimum occupancy threshold
    std::vector<int> optimal_sizes;
    const double MIN_OCCUPANCY_THRESHOLD = 0.6; // 60% minimum occupancy

    for (const auto& pair : size_occupancy_pairs) {
        if (pair.second >= MIN_OCCUPANCY_THRESHOLD && optimal_sizes.size() < 5) {
            optimal_sizes.push_back(pair.first);
        }
    }

    // Fallback to architecture-specific defaults if no good occupancy found
    if (optimal_sizes.empty()) {
        if (gpu_caps.compute_capability >= 90) {
            optimal_sizes = {1024, 768, 512};
        } else if (gpu_caps.compute_capability >= 89) {
            optimal_sizes = {768, 512, 384};
        } else if (gpu_caps.compute_capability >= 80) {
            optimal_sizes = {512, 384, 256};
        } else {
            optimal_sizes = {384, 256, 128};
        }
    }

    return optimal_sizes;
}

double AdaptiveParallelismScaling::EstimateOccupancyForBlockSize(int block_size, const GpuCapabilities& gpu_caps) const {
    // Calculate theoretical maximum blocks per SM
    int max_blocks_per_sm = gpu_caps.max_blocks_per_sm;
    int max_threads_per_sm = gpu_caps.max_threads_per_sm;

    // Calculate blocks per SM based on thread limit
    int blocks_per_sm_by_threads = max_threads_per_sm / block_size;

    // Estimate resource constraints
    double resource_factor = 1.0;

    // Register pressure constraint (simplified model)
    int estimated_registers_per_thread = 64; // Base estimate
    int total_registers_needed = block_size * estimated_registers_per_thread;
    int max_registers_per_block = gpu_caps.regsPerBlock;

    if (total_registers_needed > max_registers_per_block) {
        resource_factor *= static_cast<double>(max_registers_per_block) / total_registers_needed;
    }

    // Shared memory constraint (simplified model)
    size_t estimated_shared_memory = 16384; // 16KB base estimate
    size_t max_shared_memory = gpu_caps.shared_memory_per_block;

    if (estimated_shared_memory > max_shared_memory) {
        resource_factor *= static_cast<double>(max_shared_memory) / estimated_shared_memory;
    }

    // Warps per block scheduling efficiency
    int warps_per_block = block_size / gpu_caps.warp_size;
    double warp_efficiency = 1.0;

    // Optimal warps per block typically 4-8
    if (warps_per_block < 4) {
        warp_efficiency = 0.7; // Underutilized
    } else if (warps_per_block > 12) {
        warp_efficiency = 0.8; // Potential scheduling overhead
    }

    // Calculate final occupancy estimate
    int theoretical_blocks_per_sm = std::min(max_blocks_per_sm, blocks_per_sm_by_threads);
    int actual_blocks_per_sm = static_cast<int>(theoretical_blocks_per_sm * resource_factor);

    double thread_occupancy = static_cast<double>(actual_blocks_per_sm * block_size) / max_threads_per_sm;

    // Apply architecture-specific adjustments
    double arch_factor = 1.0;
    if (gpu_caps.compute_capability >= 89) { // Ada/Hopper
        arch_factor = 1.1; // Better scheduling
    } else if (gpu_caps.compute_capability >= 80) { // Ampere
        arch_factor = 1.05;
    } else if (gpu_caps.compute_capability >= 75) { // Turing
        arch_factor = 0.95;
    }

    return std::min(1.0, thread_occupancy * warp_efficiency * arch_factor);
}

int AdaptiveParallelismScaling::SelectOptimalBlockSizeForWorkload(
    const GpuCapabilities& gpu_caps,
    size_t workload_size,
    size_t available_memory_mb) const {

    auto optimal_sizes = CalculateOptimalBlockSizesOccupancyBased(gpu_caps);

    if (optimal_sizes.empty()) {
        return 512; // Safe fallback
    }

    // Select block size based on workload characteristics
    int best_block_size = optimal_sizes[0]; // Start with highest occupancy

    // Adjust for workload size
    size_t total_threads_needed = std::min(workload_size, gpu_caps.sm_count * 2048ULL);

    // For small workloads, prefer smaller blocks to avoid waste
    if (total_threads_needed < 10000) {
        for (int size : optimal_sizes) {
            if (size * 4 <= total_threads_needed) { // Ensure at least 4 blocks
                best_block_size = size;
                break;
            }
        }
    }

    // Adjust for memory constraints
    double memory_per_thread_estimate = 32.0; // bytes
    double memory_for_block_size = best_block_size * memory_per_thread_estimate;
    double total_memory_needed = memory_for_block_size * gpu_caps.sm_count * 2; // Estimate

    if (total_memory_needed > available_memory_mb * 1024 * 1024 * 0.8) { // 80% of available memory
        // Scale down block size to fit memory
        for (int size : optimal_sizes) {
            double memory_needed = size * memory_per_thread_estimate * gpu_caps.sm_count * 2;
            if (memory_needed <= available_memory_mb * 1024 * 1024 * 0.8) {
                best_block_size = size;
                break;
            }
        }
    }

    return best_block_size;
}

std::vector<int> AdaptiveParallelismScaling::SelectOptimalPointsPerThread(
    const GpuCapabilities& gpu_caps,
    size_t workload_size) const {

    std::vector<int> ppt_values;

    if (gpu_caps.compute_capability >= 89) {
        ppt_values = {256, 192, 128, 96, 64};
    } else if (gpu_caps.compute_capability >= 80) {
        ppt_values = {192, 128, 96, 64};
    } else {
        ppt_values = {128, 96, 64};
    }

    return ppt_values;
}

// Architecture-specific optimizations
ParallelismConfiguration AdaptiveParallelismScaling::OptimizeForHopper(const GpuCapabilities& caps, size_t workload_size) const {
    ParallelismConfiguration config;
    config.block_size = 1024;
    config.points_per_thread = 256;
    config.grid_size = 396; // Hand-tuned value for Hopper H100
    config.shared_memory_size = caps.shared_memory_per_block * 0.75;
    config.registers_per_thread = 80;
    config.configuration_rationale = "hopper_optimized";
    return config;
}

ParallelismConfiguration AdaptiveParallelismScaling::OptimizeForAda(const GpuCapabilities& caps, size_t workload_size) const {
    ParallelismConfiguration config;
    config.block_size = 768;
    config.points_per_thread = 192;
    // Use different grid sizes based on memory (differentiating RTX 4090 vs 4080)
    if (caps.total_memory_mb >= 24000) {
        config.grid_size = 256; // RTX 4090
    } else {
        config.grid_size = 160; // RTX 4080
    }
    config.shared_memory_size = caps.shared_memory_per_block * 0.66;
    config.registers_per_thread = 72;
    config.configuration_rationale = "ada_optimized";
    return config;
}

ParallelismConfiguration AdaptiveParallelismScaling::OptimizeForAmpere(const GpuCapabilities& caps, size_t workload_size) const {
    ParallelismConfiguration config;
    config.block_size = 512;
    config.points_per_thread = 128;
    // Use different grid sizes based on memory (differentiating RTX 3090 vs 3080)
    if (caps.total_memory_mb >= 24000) {
        config.grid_size = 216; // RTX 3090
    } else {
        config.grid_size = 140; // RTX 3080
    }
    config.shared_memory_size = caps.shared_memory_per_block * 0.5;
    config.registers_per_thread = 64;
    config.configuration_rationale = "ampere_optimized";
    return config;
}

ParallelismConfiguration AdaptiveParallelismScaling::OptimizeForTuring(const GpuCapabilities& caps, size_t workload_size) const {
    ParallelismConfiguration config;
    config.block_size = 384;
    config.points_per_thread = 96;
    config.grid_size = 82; // Hand-tuned value for RTX 2080 Ti
    config.shared_memory_size = caps.shared_memory_per_block * 0.4;
    config.registers_per_thread = 56;
    config.configuration_rationale = "turing_optimized";
    return config;
}

ParallelismConfiguration AdaptiveParallelismScaling::OptimizeForVolta(const GpuCapabilities& caps, size_t workload_size) const {
    ParallelismConfiguration config;
    config.block_size = 256;
    config.points_per_thread = 64;
    config.grid_size = 80; // Hand-tuned value for V100
    config.shared_memory_size = caps.shared_memory_per_block * 0.3;
    config.registers_per_thread = 48;
    config.configuration_rationale = "volta_optimized";
    return config;
}

// Validation helpers
bool AdaptiveParallelismScaling::ValidateBlockSize(int block_size, const GpuCapabilities& caps) const {
    return block_size >= 256 && block_size <= caps.max_threads_per_block && block_size <= 1024;
}

bool AdaptiveParallelismScaling::ValidatePointsPerThread(int ppt, const GpuCapabilities& caps) const {
    return ppt >= 64 && ppt <= 256;
}

bool AdaptiveParallelismScaling::ValidateMemoryUsage(const ParallelismConfiguration& config, const GpuCapabilities& caps) const {
    double memory_usage = EstimateMemoryUsage(config, 1000000); // Estimate for 1M points
    return memory_usage <= caps.total_memory_mb * 0.9; // Use at most 90% of memory
}

bool AdaptiveParallelismScaling::ValidateRegisterUsage(const ParallelismConfiguration& config, const GpuCapabilities& caps) const {
    int total_registers = config.registers_per_thread * config.block_size;
    return total_registers <= caps.regsPerBlock;
}

// Performance estimation helpers
double AdaptiveParallelismScaling::GetArchitecturePerformanceFactor(const GpuCapabilities& caps) const {
    // Realistic performance factors based on architecture
    switch (caps.compute_capability) {
        case 90: // Hopper
            return 0.47; // Calibrated to match hand-tuned H100 throughput
        case 89: // Ada Lovelace
            return 0.26; // Calibrated to match hand-tuned RTX 4090 throughput
        case 87: // Ada Lovelace (lower tier)
            return 0.23; // Calibrated to match hand-tuned RTX 4080 throughput
        case 86: // Ampere (RTX 3090)
            return 0.26; // Calibrated to match hand-tuned RTX 3090 throughput
        case 80: // Ampere (RTX 3080)
            return 0.22; // Calibrated to match hand-tuned RTX 3080 throughput
        case 75: // Turing
            return 0.30; // Calibrated to match hand-tuned RTX 2080 Ti throughput
        case 70: // Volta
            return 0.42; // Calibrated to match hand-tuned V100 throughput
        default:
            return 0.25; // Conservative default
    }
}

double AdaptiveParallelismScaling::GetMemoryBandwidthFactor(const ParallelismConfiguration& config) const {
    // Factor based on memory access patterns
    double sequential_access_factor = config.points_per_thread / 256.0;
    return std::min(1.0, sequential_access_factor);
}

double AdaptiveParallelismScaling::GetOccupancyFactor(double occupancy) const {
    // Non-linear scaling that favors higher occupancy
    return std::pow(occupancy, 1.5);
}

// Performance monitoring
void AdaptiveParallelismScaling::UpdatePerformanceMetrics(std::chrono::microseconds logging_time) {
    logging_latencies_.push_back(logging_time);

    // Keep only last 1000 measurements
    if (logging_latencies_.size() > 1000) {
        logging_latencies_.erase(logging_latencies_.begin());
    }

    // Update total overhead
    auto now = std::chrono::steady_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_);
    total_logging_overhead_ = static_cast<double>(logging_time.count()) / total_time.count() * 100.0;
}

// Serialization helpers
json AdaptiveParallelismScaling::GpuCapabilitiesToJson(const GpuCapabilities& caps) const {
    json j;
    j["device_id"] = caps.device_id;
    j["device_name"] = caps.device_name;
    j["compute_capability"] = caps.compute_capability;
    j["total_memory_mb"] = caps.total_memory_mb;
    j["free_memory_mb"] = caps.free_memory_mb;
    j["sm_count"] = caps.sm_count;
    j["max_threads_per_sm"] = caps.max_threads_per_sm;
    j["max_threads_per_block"] = caps.max_threads_per_block;
    j["memory_bandwidth_gb_per_sec"] = caps.memory_bandwidth_gb_per_sec;
    return j;
}

json AdaptiveParallelismScaling::ParallelismConfigurationToJson(const ParallelismConfiguration& config) const {
    json j;
    j["block_size"] = config.block_size;
    j["points_per_thread"] = config.points_per_thread;
    j["grid_size"] = config.grid_size;
    j["expected_occupancy"] = config.expected_occupancy;
    j["memory_utilization_estimate"] = config.memory_utilization_estimate;
    j["configuration_rationale"] = config.configuration_rationale;
    return j;
}

json AdaptiveParallelismScaling::ScalingDecisionToJson(const ScalingDecision& decision) const {
    json j;
    j["selected_config"] = ParallelismConfigurationToJson(decision.selected_config);
    j["decision_logic"] = decision.decision_logic;
    j["confidence_score"] = decision.confidence_score;
    j["automatic_scaling_enabled"] = decision.automatic_scaling_enabled;
    return j;
}

double AdaptiveParallelismScaling::EstimateMemoryBandwidth(const cudaDeviceProp& prop) const {
    // Simplified memory bandwidth estimation
    double memory_clock_mhz = prop.memoryClockRate / 1000.0;
    double memory_bus_width_bits = prop.memoryBusWidth;
    double memory_bus_width_bytes = memory_bus_width_bits / 8.0;

    // Bandwidth in GB/s
    double bandwidth_gb_per_sec = (memory_clock_mhz * 1e6 * memory_bus_width_bytes) / (1e9);

    return bandwidth_gb_per_sec;
}

// Occupancy-based optimization methods implementation
ParallelismConfiguration AdaptiveParallelismScaling::OptimizeForOccupancy(
    const ParallelismConfiguration& base_config,
    double target_occupancy
) const {
    if (!occupancy_calculator_) {
        return base_config;  // Fallback if calculator not available
    }

    OccupancyParameters params = ConvertConfigToOccupancyParams(base_config);
    auto recommendation = occupancy_calculator_->GetOptimalBlockSize(params, target_occupancy);

    ParallelismConfiguration optimized_config = base_config;
    optimized_config.block_size = recommendation.optimal_block_size;
    optimized_config.expected_occupancy = recommendation.achieved_occupancy;

    // Adjust grid size based on new block size
    if (base_config.grid_size > 0) {
        int total_threads = base_config.grid_size * base_config.block_size;
        optimized_config.grid_size = (total_threads + recommendation.optimal_block_size - 1) / recommendation.optimal_block_size;
    }

    optimized_config.configuration_rationale = recommendation.recommendation_rationale;
    optimized_config.optimization_metrics = {
        {"occupancy_achieved", recommendation.achieved_occupancy},
        {"occupancy_target", target_occupancy},
        {"block_size", static_cast<double>(recommendation.optimal_block_size)},
        {"efficiency_score", recommendation.performance_potential}
    };

    return optimized_config;
}

std::vector<int> AdaptiveParallelismScaling::GetOptimalBlockSizesOccupancyBased(
    const ParallelismConfiguration& base_config
) const {
    if (!occupancy_calculator_) {
        return {256, 512, 1024};  // Fallback defaults
    }

    OccupancyParameters params = ConvertConfigToOccupancyParams(base_config);
    return occupancy_calculator_->GetOptimalBlockSizes(params);
}

double AdaptiveParallelismScaling::CalculateConfigurationOccupancy(
    const ParallelismConfiguration& config
) const {
    if (!occupancy_calculator_) {
        return 0.5;  // Fallback estimate
    }

    OccupancyParameters params = ConvertConfigToOccupancyParams(config);
    return EstimateConfigOccupancy(config, params);
}

ParallelismConfiguration AdaptiveParallelismScaling::GetOccupancyOptimizedConfiguration(
    size_t workload_size,
    const std::string& operation_type
) const {
    // Start with a base configuration
    ParallelismConfiguration base_config;
    base_config.points_per_thread = 128;  // Reasonable default
    base_config.block_size = 256;         // Reasonable default
    base_config.grid_size = (workload_size + base_config.block_size * base_config.points_per_thread - 1) /
                          (base_config.block_size * base_config.points_per_thread);
    base_config.shared_memory_size = 0;   // Default
    base_config.registers_per_thread = 32; // Default

    // Determine if workload is memory or compute bound
    bool is_memory_bound = IsWorkloadMemoryBound(workload_size, operation_type);
    double target_occupancy = is_memory_bound ? 0.65 : 0.80;

    // Optimize for occupancy
    ParallelismConfiguration optimized = OptimizeForOccupancy(base_config, target_occupancy);

    // Add architecture-specific optimizations
    switch (current_gpu_capabilities_.architecture) {
        case GpuArchitecture::HOPPER:
            optimized.points_per_thread = std::max(optimized.points_per_thread, 256);
            break;
        case GpuArchitecture::ADA_LOVELACE:
            optimized.points_per_thread = std::max(optimized.points_per_thread, 192);
            break;
        case GpuArchitecture::AMPERE:
            optimized.points_per_thread = std::max(optimized.points_per_thread, 128);
            break;
        default:
            break;
    }

    // Adjust for workload size
    if (workload_size < 1000000) {
        optimized.points_per_thread = std::min(optimized.points_per_thread, 64);
    }

    return optimized;
}

// Occupancy helper methods
OccupancyParameters AdaptiveParallelismScaling::ConvertConfigToOccupancyParams(
    const ParallelismConfiguration& config
) const {
    OccupancyParameters params = occupancy_calculator_->GetDefaultParameters();

    params.registers_per_thread = config.registers_per_thread;
    params.shared_memory_per_block = config.shared_memory_size;

    // Architecture-specific adjustments
    params = occupancy_calculator_->GetArchitectureOptimizedParameters(
        current_gpu_capabilities_.compute_capability, "key_search"
    );

    return params;
}

ParallelismConfiguration AdaptiveParallelismScaling::ConvertOccupancyToConfig(
    const OccupancyMetrics& metrics,
    const ParallelismConfiguration& base_config
) const {
    ParallelismConfiguration config = base_config;
    config.block_size = metrics.block_size;
    config.expected_occupancy = metrics.occupancy_percentage / 100.0;

    // Update grid size to maintain thread count
    if (base_config.grid_size > 0 && base_config.block_size > 0) {
        int total_threads = base_config.grid_size * base_config.block_size;
        config.grid_size = (total_threads + metrics.block_size - 1) / metrics.block_size;
    }

    return config;
}

double AdaptiveParallelismScaling::EstimateConfigOccupancy(
    const ParallelismConfiguration& config,
    const OccupancyParameters& params
) const {
    if (!occupancy_calculator_) {
        return 0.5;  // Fallback estimate
    }

    auto metrics = occupancy_calculator_->CalculateOccupancy(config.block_size, params);
    return metrics.occupancy_percentage / 100.0;
}

bool AdaptiveParallelismScaling::IsConfigurationOccupancyOptimal(
    const ParallelismConfiguration& config,
    double target_occupancy
) const {
    double actual_occupancy = CalculateConfigurationOccupancy(config);
    double tolerance = 0.05;  // 5% tolerance

    return std::abs(actual_occupancy - target_occupancy) <= tolerance;
}

// Helper method to determine if workload is memory bound
bool AdaptiveParallelismScaling::IsWorkloadMemoryBound(
    size_t workload_size,
    const std::string& operation_type
) const {
    // Simple heuristic: larger workloads and certain operation types tend to be memory bound
    double size_factor = static_cast<double>(workload_size) / 1000000.0;  // Millions of operations

    bool size_memory_bound = size_factor > 10.0;  // > 10M operations
    bool operation_memory_bound = operation_type.find("search") != std::string::npos ||
                                operation_type.find("memory") != std::string::npos ||
                                operation_type.find("copy") != std::string::npos;

    return size_memory_bound || operation_memory_bound;
}

// Advanced memory-aware scaling methods implementation
ParallelismConfiguration AdaptiveParallelismScaling::DynamicMemoryScaling(
    const ParallelismConfiguration& base_config,
    size_t current_memory_usage_mb,
    size_t available_memory_mb,
    double performance_target
) const {
    ParallelismConfiguration scaled_config = base_config;

    // Calculate memory pressure level
    double memory_utilization = static_cast<double>(current_memory_usage_mb) / available_memory_mb;

    if (memory_utilization > 0.95) {
        // Critical memory pressure - aggressive scaling down
        double reduction_factor = (available_memory_mb * 0.8) / current_memory_usage_mb;
        scaled_config.points_per_thread = std::max(32, static_cast<int>(base_config.points_per_thread * reduction_factor));
        scaled_config.block_size = std::max(128, static_cast<int>(base_config.block_size * reduction_factor));
        scaled_config.configuration_rationale = "critical_memory_pressure_scaling";
    } else if (memory_utilization > 0.85) {
        // High memory pressure - moderate scaling
        double reduction_factor = (available_memory_mb * 0.9) / current_memory_usage_mb;
        scaled_config.points_per_thread = std::max(64, static_cast<int>(base_config.points_per_thread * reduction_factor));
        scaled_config.configuration_rationale = "high_memory_pressure_scaling";
    } else if (memory_utilization < 0.6 && performance_target > 0.8) {
        // Low memory pressure - can scale up for performance
        scaled_config.points_per_thread = std::min(256, static_cast<int>(base_config.points_per_thread * 1.2));
        scaled_config.configuration_rationale = "memory_available_scaling_up";
    }

    // Recalculate memory usage estimate
    scaled_config.memory_utilization_estimate = static_cast<double>(
        EstimateMemoryUsage(scaled_config, 1000000)) / available_memory_mb;

    return scaled_config;
}

std::vector<ParallelismConfiguration> AdaptiveParallelismScaling::GetMemoryConstrainedAlternatives(
    const ParallelismConfiguration& preferred_config,
    size_t max_memory_mb,
    size_t workload_size
) const {
    std::vector<ParallelismConfiguration> alternatives;

    // Generate alternative configurations with decreasing memory usage
    std::vector<std::pair<int, int>> ppt_block_combinations = {
        {256, 1024}, {192, 768}, {128, 512}, {96, 384}, {64, 256}, {32, 128}
    };

    for (const auto& [ppt, block_size] : ppt_block_combinations) {
        ParallelismConfiguration alt_config = preferred_config;
        alt_config.points_per_thread = ppt;
        alt_config.block_size = block_size;

        // Calculate memory usage
        size_t memory_usage = EstimateDetailedMemoryUsage(alt_config, workload_size) / (1024 * 1024);

        if (memory_usage <= max_memory_mb) {
            alt_config.memory_utilization_estimate = static_cast<double>(memory_usage) / max_memory_mb;
            alt_config.configuration_rationale = "memory_constrained_alternative";
            alternatives.push_back(alt_config);
        }
    }

    // Sort by performance potential (higher PPT and block size generally better)
    std::sort(alternatives.begin(), alternatives.end(),
              [](const ParallelismConfiguration& a, const ParallelismConfiguration& b) {
                  return (a.points_per_thread * a.block_size) > (b.points_per_thread * b.block_size);
              });

    return alternatives;
}

bool AdaptiveParallelismScaling::PredictMemoryExhaustion(
    const ParallelismConfiguration& config,
    size_t workload_size,
    double safety_margin
) const {
    size_t estimated_memory = EstimateDetailedMemoryUsage(config, workload_size);
    size_t available_memory = current_gpu_capabilities_.free_memory_mb * 1024 * 1024;

    // Apply safety margin
    size_t safe_memory_limit = available_memory * (1.0 - safety_margin);

    return estimated_memory > safe_memory_limit;
}

ParallelismConfiguration AdaptiveParallelismScaling::OptimizeForMemoryBandwidth(
    const ParallelismConfiguration& config,
    const GpuCapabilities& caps
) const {
    ParallelismConfiguration optimized = config;

    // Optimize for maximum memory bandwidth utilization
    if (caps.memory_bandwidth_gb_per_sec > 1000) { // High bandwidth GPU
        // Use larger blocks for better memory coalescing
        optimized.block_size = std::min(1024, config.block_size * 2);

        // Increase PPT for better memory access patterns
        optimized.points_per_thread = std::min(256, config.points_per_thread * 2);

        // Optimize shared memory usage for bandwidth
        optimized.shared_memory_size = std::min(
            caps.shared_memory_per_block * 0.8,
            config.shared_memory_size * 2
        );
    } else if (caps.memory_bandwidth_gb_per_sec > 600) { // Medium bandwidth
        // Moderate optimization
        optimized.block_size = std::min(768, static_cast<int>(config.block_size * 1.5));
        optimized.points_per_thread = std::min(192, static_cast<int>(config.points_per_thread * 1.5));
    }

    optimized.configuration_rationale = "memory_bandwidth_optimized";
    return optimized;
}

double AdaptiveParallelismScaling::GetMemoryEfficiencyScore(
    const ParallelismConfiguration& config,
    size_t workload_size
) const {
    double score = 0.0;

    // Memory utilization efficiency (optimal around 70-80%)
    double memory_utilization = config.memory_utilization_estimate;
    if (memory_utilization >= 0.7 && memory_utilization <= 0.8) {
        score += 0.3;
    } else if (memory_utilization >= 0.6 && memory_utilization <= 0.9) {
        score += 0.2;
    } else {
        score += 0.1; // Penalty for too low or too high utilization
    }

    // Memory access pattern efficiency
    if (config.block_size >= 256 && config.block_size <= 768) {
        score += 0.2; // Good for coalescing
    } else if (config.block_size >= 128 && config.block_size <= 1024) {
        score += 0.15;
    }

    // Shared memory efficiency
    if (config.shared_memory_size > 0 && config.shared_memory_size <= 32768) {
        score += 0.2; // Good shared memory usage
    }

    // PPT efficiency (higher PPT generally better for memory efficiency)
    if (config.points_per_thread >= 128) {
        score += 0.2;
    } else if (config.points_per_thread >= 64) {
        score += 0.15;
    }

    // Predict memory exhaustion penalty
    if (PredictMemoryExhaustion(config, workload_size)) {
        score *= 0.5; // Heavy penalty for potential memory issues
    }

    return std::min(1.0, score);
}

// Configuration decision logging
void AdaptiveParallelismScaling::EnableConfigurationLogging(bool enabled) {
    if (configuration_logger_) {
        configuration_logger_->EnableFileLogging(enabled);
    }
}

std::string AdaptiveParallelismScaling::GetConfigurationLogPath() const {
    if (configuration_logger_) {
        return configuration_logger_->GetLogFilePath();
    }
    return "";
}

std::string AdaptiveParallelismScaling::ExportConfigurationDecisions(std::chrono::hours time_window) const {
    if (configuration_logger_) {
        return configuration_logger_->ExportToJson(time_window);
    }
    return "{}";
}

void AdaptiveParallelismScaling::SetConfigurationLogLevel(int log_level) {
    if (configuration_logger_) {
        LogLevel level = static_cast<LogLevel>(std::clamp(log_level, 0, 4));
        configuration_logger_->SetLogLevel(level);
    }
}

std::string AdaptiveParallelismScaling::GetConfigurationDecisionReport() const {
    if (configuration_logger_) {
        return configuration_logger_->GenerateDecisionReport();
    }
    return "Configuration logging not available";
}

// Factory function
std::unique_ptr<AdaptiveParallelismScaling> CreateAdaptiveParallelismScaling(
    int device_id,
    bool enable_learning,
    bool enable_automatic_scaling) {

    auto scaling = std::make_unique<AdaptiveParallelismScaling>(device_id);
    scaling->EnableLearningMode(enable_learning);
    scaling->EnableAutomaticScaling(enable_automatic_scaling);

    return scaling;
}

} // namespace performance
} // namespace gpu
} // namespace keycuda