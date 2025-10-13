/**
 * Configuration Tuner Implementation
 * 
 * Implements P1-007: Dynamic Performance Tuning
 * - Adaptive CUDA configuration adjustment
 * - Strategy-based tuning algorithms
 * - Configuration validation and safety checks
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/performance/configuration_tuner.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P1-007 dynamic performance tuning
 * @spdx_license_identifier MIT
 */

#include "configuration_tuner.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace puzzle71 {
namespace performance {

// Constructor
ConfigurationTuner::ConfigurationTuner(const CUDAConfig& initial_config)
    : current_config_(initial_config),
      initial_config_(initial_config) {
    
    // Set default limits
    min_config_ = CUDAConfig(128, 256, 16, 4096);
    max_config_ = CUDAConfig(1024, 4096, 128, 49152);
    
    // Validate initial configuration
    if (!validate_config(current_config_)) {
        current_config_ = clamp_to_limits(current_config_);
    }
}

// Destructor
ConfigurationTuner::~ConfigurationTuner() {
    // No cleanup needed
}

// Suggest tuning based on performance metrics
ConfigurationTuner::TuningResult ConfigurationTuner::suggest_tuning(
    const PerformanceMonitor::PerformanceMetrics& metrics) {
    
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    TuningResult result;
    
    // Select tuning strategy
    TuningStrategy strategy = select_strategy(metrics);
    
    if (strategy == TuningStrategy::NO_CHANGE) {
        result.new_config = current_config_;
        result.reason = "Performance is optimal, no tuning needed";
        result.expected_improvement = 0.0;
        return result;
    }
    
    // Calculate new configuration
    CUDAConfig new_config = calculate_new_config(strategy);
    
    // Validate and clamp
    new_config = clamp_to_limits(new_config);
    
    // Estimate improvement
    double improvement = estimate_improvement(current_config_, new_config, metrics);
    
    // Build result
    result.new_config = new_config;
    result.reason = strategy_to_string(strategy);
    result.expected_improvement = improvement;
    
    return result;
}

// Apply new configuration
bool ConfigurationTuner::apply_config(const CUDAConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (!validate_config(config)) {
        return false;
    }
    
    current_config_ = config;
    return true;
}

// Get current configuration
ConfigurationTuner::CUDAConfig ConfigurationTuner::get_current_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return current_config_;
}

// Reset to initial configuration
void ConfigurationTuner::reset_to_initial() {
    std::lock_guard<std::mutex> lock(config_mutex_);
    current_config_ = initial_config_;
}

// Set configuration limits
void ConfigurationTuner::set_limits(const CUDAConfig& min_config, const CUDAConfig& max_config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    min_config_ = min_config;
    max_config_ = max_config;
}

// Get minimum configuration limits
ConfigurationTuner::CUDAConfig ConfigurationTuner::get_min_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return min_config_;
}

// Get maximum configuration limits
ConfigurationTuner::CUDAConfig ConfigurationTuner::get_max_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return max_config_;
}

// Select tuning strategy based on metrics (private)
ConfigurationTuner::TuningStrategy ConfigurationTuner::select_strategy(
    const PerformanceMonitor::PerformanceMetrics& metrics) {
    
    // Check GPU utilization
    if (metrics.gpu_utilization < 0.70) {
        // GPU is significantly underutilized - increase parallelism
        return TuningStrategy::INCREASE_PARALLELISM;
    } else if (metrics.gpu_utilization > 0.95) {
        // GPU is overutilized - decrease parallelism
        return TuningStrategy::DECREASE_PARALLELISM;
    }
    
    // Check memory bandwidth
    if (metrics.memory_bandwidth < 0.60) {
        // Memory is underutilized - increase work per thread
        return TuningStrategy::INCREASE_WORK_PER_THREAD;
    } else if (metrics.memory_bandwidth > 0.90) {
        // Memory is saturated - decrease work per thread
        return TuningStrategy::DECREASE_WORK_PER_THREAD;
    }
    
    // Check if both are in acceptable range but not optimal
    if (metrics.gpu_utilization < 0.90 || metrics.memory_bandwidth < 0.80) {
        // Try memory optimization
        return TuningStrategy::OPTIMIZE_MEMORY;
    }
    
    // Performance is optimal
    return TuningStrategy::NO_CHANGE;
}

// Calculate new configuration based on strategy (private)
ConfigurationTuner::CUDAConfig ConfigurationTuner::calculate_new_config(TuningStrategy strategy) {
    CUDAConfig new_config = current_config_;
    
    switch (strategy) {
        case TuningStrategy::INCREASE_PARALLELISM:
            // Increase grid size by 25%
            new_config.grid_size = static_cast<int>(current_config_.grid_size * 1.25);
            break;
            
        case TuningStrategy::DECREASE_PARALLELISM:
            // Decrease grid size by 20%
            new_config.grid_size = static_cast<int>(current_config_.grid_size * 0.80);
            break;
            
        case TuningStrategy::INCREASE_WORK_PER_THREAD:
            // Increase points per thread by 25%
            new_config.points_per_thread = static_cast<int>(current_config_.points_per_thread * 1.25);
            break;
            
        case TuningStrategy::DECREASE_WORK_PER_THREAD:
            // Decrease points per thread by 20%
            new_config.points_per_thread = static_cast<int>(current_config_.points_per_thread * 0.80);
            break;
            
        case TuningStrategy::OPTIMIZE_MEMORY:
            // Adjust shared memory size based on block size
            new_config.shared_memory_size = current_config_.block_size * 8 * sizeof(unsigned int);
            break;
            
        case TuningStrategy::NO_CHANGE:
        default:
            // No change
            break;
    }
    
    return new_config;
}

// Validate configuration (private)
bool ConfigurationTuner::validate_config(const CUDAConfig& config) {
    // Check block size (must be multiple of 32 for warp alignment)
    if (config.block_size % 32 != 0) {
        return false;
    }
    
    // Check if within limits
    if (config.block_size < min_config_.block_size || config.block_size > max_config_.block_size) {
        return false;
    }
    
    if (config.grid_size < min_config_.grid_size || config.grid_size > max_config_.grid_size) {
        return false;
    }
    
    if (config.points_per_thread < min_config_.points_per_thread || 
        config.points_per_thread > max_config_.points_per_thread) {
        return false;
    }
    
    if (config.shared_memory_size < min_config_.shared_memory_size || 
        config.shared_memory_size > max_config_.shared_memory_size) {
        return false;
    }
    
    return true;
}

// Clamp configuration to limits (private)
ConfigurationTuner::CUDAConfig ConfigurationTuner::clamp_to_limits(const CUDAConfig& config) {
    CUDAConfig clamped = config;
    
    // Clamp block size (round to nearest multiple of 32)
    clamped.block_size = std::max(min_config_.block_size, 
                                  std::min(max_config_.block_size, config.block_size));
    clamped.block_size = (clamped.block_size / 32) * 32;
    
    // Clamp grid size
    clamped.grid_size = std::max(min_config_.grid_size, 
                                 std::min(max_config_.grid_size, config.grid_size));
    
    // Clamp points per thread
    clamped.points_per_thread = std::max(min_config_.points_per_thread, 
                                         std::min(max_config_.points_per_thread, config.points_per_thread));
    
    // Clamp shared memory size
    clamped.shared_memory_size = std::max(min_config_.shared_memory_size, 
                                          std::min(max_config_.shared_memory_size, config.shared_memory_size));
    
    return clamped;
}

// Estimate performance improvement (private)
double ConfigurationTuner::estimate_improvement(
    const CUDAConfig& old_config,
    const CUDAConfig& new_config,
    const PerformanceMonitor::PerformanceMetrics& metrics) {
    
    // Calculate parallelism change
    double parallelism_ratio = static_cast<double>(new_config.grid_size) / old_config.grid_size;
    
    // Calculate work per thread change
    double work_ratio = static_cast<double>(new_config.points_per_thread) / old_config.points_per_thread;
    
    // Estimate improvement based on current utilization
    double gpu_gap = 1.0 - metrics.gpu_utilization;
    double mem_gap = 1.0 - metrics.memory_bandwidth;
    
    // Simple heuristic: improvement is proportional to utilization gap and config change
    double improvement = 0.0;
    
    if (parallelism_ratio > 1.0) {
        // Increasing parallelism helps if GPU is underutilized
        improvement += gpu_gap * (parallelism_ratio - 1.0);
    }
    
    if (work_ratio > 1.0) {
        // Increasing work per thread helps if memory is underutilized
        improvement += mem_gap * (work_ratio - 1.0);
    }
    
    // Cap improvement at 50%
    return std::min(0.50, improvement);
}

// Convert strategy to string (private)
std::string ConfigurationTuner::strategy_to_string(TuningStrategy strategy) {
    switch (strategy) {
        case TuningStrategy::INCREASE_PARALLELISM:
            return "Increase parallelism (GPU underutilized)";
        case TuningStrategy::DECREASE_PARALLELISM:
            return "Decrease parallelism (GPU overutilized)";
        case TuningStrategy::INCREASE_WORK_PER_THREAD:
            return "Increase work per thread (memory underutilized)";
        case TuningStrategy::DECREASE_WORK_PER_THREAD:
            return "Decrease work per thread (memory saturated)";
        case TuningStrategy::OPTIMIZE_MEMORY:
            return "Optimize memory usage";
        case TuningStrategy::NO_CHANGE:
        default:
            return "No change needed";
    }
}

} // namespace performance
} // namespace puzzle71

