/**
 * Configuration Tuner for Dynamic Performance Tuning
 * 
 * Implements P1-007: Dynamic Performance Tuning
 * - Adaptive CUDA configuration adjustment
 * - Strategy-based tuning algorithms
 * - Configuration validation and safety checks
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/performance/configuration_tuner.h
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P1-007 dynamic performance tuning
 * @spdx_license_identifier MIT
 */

#pragma once

#include "performance_monitor.h"
#include <string>
#include <mutex>

namespace puzzle71 {
namespace performance {

/**
 * Configuration Tuner for Adaptive Performance Optimization
 * 
 * Automatically adjusts CUDA configuration based on performance metrics
 * to maximize GPU utilization and throughput.
 */
class ConfigurationTuner {
public:
    /**
     * CUDA configuration parameters
     */
    struct CUDAConfig {
        int block_size;            // Threads per block
        int grid_size;             // Number of blocks
        int points_per_thread;     // Work per thread
        int shared_memory_size;    // Shared memory per block (bytes)
        
        CUDAConfig()
            : block_size(256),
              grid_size(1024),
              points_per_thread(32),
              shared_memory_size(8192) {}
        
        CUDAConfig(int bs, int gs, int ppt, int sms)
            : block_size(bs),
              grid_size(gs),
              points_per_thread(ppt),
              shared_memory_size(sms) {}
    };
    
    /**
     * Tuning result with new configuration and rationale
     */
    struct TuningResult {
        CUDAConfig new_config;           // Recommended configuration
        std::string reason;              // Tuning rationale
        double expected_improvement;     // Expected performance gain (0.0-1.0)
        
        TuningResult()
            : expected_improvement(0.0) {}
    };
    
    /**
     * Tuning strategy enumeration
     */
    enum class TuningStrategy {
        INCREASE_PARALLELISM,      // Increase grid_size (more blocks)
        DECREASE_PARALLELISM,      // Decrease grid_size (fewer blocks)
        INCREASE_WORK_PER_THREAD,  // Increase points_per_thread
        DECREASE_WORK_PER_THREAD,  // Decrease points_per_thread
        OPTIMIZE_MEMORY,           // Adjust shared_memory_size
        NO_CHANGE                  // No tuning needed
    };
    
    /**
     * Constructor
     * 
     * @param initial_config Initial CUDA configuration
     */
    explicit ConfigurationTuner(const CUDAConfig& initial_config);
    
    /**
     * Destructor
     */
    ~ConfigurationTuner();
    
    /**
     * Suggest tuning based on performance metrics
     * 
     * @param metrics Current performance metrics
     * @return Tuning result with new configuration
     */
    TuningResult suggest_tuning(const PerformanceMonitor::PerformanceMetrics& metrics);
    
    /**
     * Apply new configuration
     * 
     * @param config New CUDA configuration
     * @return True if successful
     */
    bool apply_config(const CUDAConfig& config);
    
    /**
     * Get current configuration
     * 
     * @return Current CUDA configuration
     */
    CUDAConfig get_current_config() const;
    
    /**
     * Reset to initial configuration
     */
    void reset_to_initial();
    
    /**
     * Set configuration limits
     * 
     * @param min_config Minimum allowed configuration
     * @param max_config Maximum allowed configuration
     */
    void set_limits(const CUDAConfig& min_config, const CUDAConfig& max_config);
    
    /**
     * Get minimum configuration limits
     * 
     * @return Minimum configuration
     */
    CUDAConfig get_min_config() const;
    
    /**
     * Get maximum configuration limits
     * 
     * @return Maximum configuration
     */
    CUDAConfig get_max_config() const;

private:
    // Current configuration
    CUDAConfig current_config_;
    
    // Initial configuration (for reset)
    CUDAConfig initial_config_;
    
    // Configuration limits
    CUDAConfig min_config_;
    CUDAConfig max_config_;
    
    // Thread safety
    mutable std::mutex config_mutex_;
    
    /**
     * Select tuning strategy based on metrics
     * 
     * @param metrics Performance metrics
     * @return Selected tuning strategy
     */
    TuningStrategy select_strategy(const PerformanceMonitor::PerformanceMetrics& metrics);
    
    /**
     * Calculate new configuration based on strategy
     * 
     * @param strategy Tuning strategy
     * @return New configuration
     */
    CUDAConfig calculate_new_config(TuningStrategy strategy);
    
    /**
     * Validate configuration
     * 
     * @param config Configuration to validate
     * @return True if valid
     */
    bool validate_config(const CUDAConfig& config);
    
    /**
     * Clamp configuration to limits
     * 
     * @param config Configuration to clamp
     * @return Clamped configuration
     */
    CUDAConfig clamp_to_limits(const CUDAConfig& config);
    
    /**
     * Estimate performance improvement
     * 
     * @param old_config Old configuration
     * @param new_config New configuration
     * @param metrics Current metrics
     * @return Estimated improvement (0.0-1.0)
     */
    double estimate_improvement(
        const CUDAConfig& old_config,
        const CUDAConfig& new_config,
        const PerformanceMonitor::PerformanceMetrics& metrics
    );
    
    /**
     * Convert strategy to string
     * 
     * @param strategy Tuning strategy
     * @return Strategy name
     */
    std::string strategy_to_string(TuningStrategy strategy);
};

} // namespace performance
} // namespace puzzle71

