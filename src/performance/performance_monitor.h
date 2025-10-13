/**
 * Performance Monitor for Dynamic Performance Tuning
 * 
 * Implements P1-007: Dynamic Performance Tuning
 * - Sliding window throughput monitoring
 * - Real-time performance metrics collection
 * - Telemetry data persistence with SHA-256 protection
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/performance/performance_monitor.h
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P1-007 dynamic performance tuning
 * @spdx_license_identifier MIT
 */

#pragma once

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <chrono>
#include <mutex>

namespace puzzle71 {
namespace performance {

/**
 * Performance Monitor with Sliding Window
 * 
 * Monitors GPU performance metrics using a sliding window approach
 * for adaptive performance tuning.
 */
class PerformanceMonitor {
public:
    /**
     * Performance metrics snapshot
     */
    struct PerformanceMetrics {
        double keys_per_sec;           // Throughput (keys/second)
        double gpu_utilization;        // GPU utilization (0.0-1.0)
        double memory_bandwidth;       // Memory bandwidth utilization (0.0-1.0)
        int active_blocks;             // Number of active blocks
        int active_warps;              // Number of active warps
        std::chrono::steady_clock::time_point timestamp;
        
        PerformanceMetrics()
            : keys_per_sec(0.0),
              gpu_utilization(0.0),
              memory_bandwidth(0.0),
              active_blocks(0),
              active_warps(0),
              timestamp(std::chrono::steady_clock::now()) {}
    };
    
    /**
     * Telemetry packet for persistence
     */
    struct TelemetryPacket {
        std::string gpu_model;         // GPU model name
        int gpu_id;                    // GPU device ID
        PerformanceMetrics metrics;    // Performance metrics
        std::map<std::string, int> config;  // Current CUDA configuration
        std::string digest;            // SHA-256 digest for integrity
        
        TelemetryPacket()
            : gpu_id(0) {}
    };
    
    /**
     * Constructor
     * 
     * @param window_size Size of sliding window (default: 10)
     * @param target_gpu_util Target GPU utilization (default: 0.90)
     * @param target_mem_bw Target memory bandwidth (default: 0.80)
     */
    explicit PerformanceMonitor(
        size_t window_size = 10,
        double target_gpu_util = 0.90,
        double target_mem_bw = 0.80
    );
    
    /**
     * Destructor
     */
    ~PerformanceMonitor();
    
    /**
     * Add performance sample to sliding window
     * 
     * @param metrics Performance metrics snapshot
     */
    void add_sample(const PerformanceMetrics& metrics);
    
    /**
     * Get average metrics from sliding window
     * 
     * @return Average performance metrics
     */
    PerformanceMetrics get_average_metrics() const;
    
    /**
     * Check if performance tuning is needed
     * 
     * @return True if tuning is recommended
     */
    bool needs_tuning() const;
    
    /**
     * Save telemetry packet to file
     * 
     * @param packet Telemetry packet to save
     * @return True if successful
     */
    bool save_telemetry(const TelemetryPacket& packet);
    
    /**
     * Load telemetry history from file
     * 
     * @return True if successful
     */
    bool load_telemetry_history();
    
    /**
     * Get telemetry history
     * 
     * @return Vector of telemetry packets
     */
    const std::vector<TelemetryPacket>& get_telemetry_history() const;
    
    /**
     * Set telemetry file path
     * 
     * @param filepath Path to telemetry file
     */
    void set_telemetry_file(const std::string& filepath);
    
    /**
     * Get current window size
     * 
     * @return Window size
     */
    size_t get_window_size() const;
    
    /**
     * Get target GPU utilization
     * 
     * @return Target GPU utilization (0.0-1.0)
     */
    double get_target_gpu_utilization() const;
    
    /**
     * Get target memory bandwidth
     * 
     * @return Target memory bandwidth (0.0-1.0)
     */
    double get_target_memory_bandwidth() const;
    
    /**
     * Clear sliding window
     */
    void clear_window();
    
    /**
     * Get number of samples in window
     * 
     * @return Number of samples
     */
    size_t get_sample_count() const;

private:
    // Sliding window of performance metrics
    std::deque<PerformanceMetrics> metrics_window_;
    
    // Window configuration
    size_t window_size_;
    
    // Performance targets
    double target_gpu_utilization_;
    double target_memory_bandwidth_;
    
    // Telemetry data
    std::vector<TelemetryPacket> telemetry_history_;
    std::string telemetry_file_;
    
    // Thread safety
    mutable std::mutex window_mutex_;
    mutable std::mutex telemetry_mutex_;
    
    /**
     * Calculate average of metrics in window
     * 
     * @return Average metrics
     */
    PerformanceMetrics calculate_average() const;
    
    /**
     * Check if GPU utilization is below target
     * 
     * @param avg_metrics Average metrics
     * @return True if below target
     */
    bool is_gpu_underutilized(const PerformanceMetrics& avg_metrics) const;
    
    /**
     * Check if memory bandwidth is below target
     * 
     * @param avg_metrics Average metrics
     * @return True if below target
     */
    bool is_memory_underutilized(const PerformanceMetrics& avg_metrics) const;
};

} // namespace performance
} // namespace puzzle71

