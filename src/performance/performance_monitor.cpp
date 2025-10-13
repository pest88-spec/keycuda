/**
 * Performance Monitor Implementation
 * 
 * Implements P1-007: Dynamic Performance Tuning
 * - Sliding window throughput monitoring
 * - Real-time performance metrics collection
 * - Telemetry data persistence with SHA-256 protection
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/performance/performance_monitor.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P1-007 dynamic performance tuning
 * @spdx_license_identifier MIT
 */

#include "performance_monitor.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>

using json = nlohmann::json;

namespace puzzle71 {
namespace performance {

// Constructor
PerformanceMonitor::PerformanceMonitor(
    size_t window_size,
    double target_gpu_util,
    double target_mem_bw
) : window_size_(window_size),
    target_gpu_utilization_(target_gpu_util),
    target_memory_bandwidth_(target_mem_bw),
    telemetry_file_("telemetry/performance.jsonl") {
}

// Destructor
PerformanceMonitor::~PerformanceMonitor() {
    // No cleanup needed (RAII handles everything)
}

// Add performance sample to sliding window
void PerformanceMonitor::add_sample(const PerformanceMetrics& metrics) {
    std::lock_guard<std::mutex> lock(window_mutex_);
    
    // Add new sample
    metrics_window_.push_back(metrics);
    
    // Remove oldest sample if window is full
    if (metrics_window_.size() > window_size_) {
        metrics_window_.pop_front();
    }
}

// Get average metrics from sliding window
PerformanceMonitor::PerformanceMetrics PerformanceMonitor::get_average_metrics() const {
    std::lock_guard<std::mutex> lock(window_mutex_);
    return calculate_average();
}

// Check if performance tuning is needed
bool PerformanceMonitor::needs_tuning() const {
    std::lock_guard<std::mutex> lock(window_mutex_);
    
    // Need at least half window size for reliable statistics
    if (metrics_window_.size() < window_size_ / 2) {
        return false;
    }
    
    auto avg_metrics = calculate_average();
    
    // Check if GPU or memory is underutilized
    return is_gpu_underutilized(avg_metrics) || 
           is_memory_underutilized(avg_metrics);
}

// Save telemetry packet to file
bool PerformanceMonitor::save_telemetry(const TelemetryPacket& packet) {
    std::lock_guard<std::mutex> lock(telemetry_mutex_);
    
    try {
        // Open file in append mode
        std::ofstream file(telemetry_file_, std::ios::app);
        if (!file.is_open()) {
            return false;
        }
        
        // Serialize to JSON
        json j;
        j["gpu_model"] = packet.gpu_model;
        j["gpu_id"] = packet.gpu_id;
        j["metrics"]["keys_per_sec"] = packet.metrics.keys_per_sec;
        j["metrics"]["gpu_utilization"] = packet.metrics.gpu_utilization;
        j["metrics"]["memory_bandwidth"] = packet.metrics.memory_bandwidth;
        j["metrics"]["active_blocks"] = packet.metrics.active_blocks;
        j["metrics"]["active_warps"] = packet.metrics.active_warps;
        
        // Add configuration
        for (const auto& [key, value] : packet.config) {
            j["config"][key] = value;
        }
        
        // Add digest
        j["digest"] = packet.digest;
        
        // Write JSONL (one line per packet)
        file << j.dump() << "\n";
        
        // Add to history
        telemetry_history_.push_back(packet);
        
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

// Load telemetry history from file
bool PerformanceMonitor::load_telemetry_history() {
    std::lock_guard<std::mutex> lock(telemetry_mutex_);
    
    try {
        std::ifstream file(telemetry_file_);
        if (!file.is_open()) {
            return false;
        }
        
        telemetry_history_.clear();
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            json j = json::parse(line);
            
            TelemetryPacket packet;
            packet.gpu_model = j["gpu_model"];
            packet.gpu_id = j["gpu_id"];
            packet.metrics.keys_per_sec = j["metrics"]["keys_per_sec"];
            packet.metrics.gpu_utilization = j["metrics"]["gpu_utilization"];
            packet.metrics.memory_bandwidth = j["metrics"]["memory_bandwidth"];
            packet.metrics.active_blocks = j["metrics"]["active_blocks"];
            packet.metrics.active_warps = j["metrics"]["active_warps"];
            
            // Load configuration
            for (auto& [key, value] : j["config"].items()) {
                packet.config[key] = value;
            }
            
            packet.digest = j["digest"];
            
            telemetry_history_.push_back(packet);
        }
        
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

// Get telemetry history
const std::vector<PerformanceMonitor::TelemetryPacket>& 
PerformanceMonitor::get_telemetry_history() const {
    std::lock_guard<std::mutex> lock(telemetry_mutex_);
    return telemetry_history_;
}

// Set telemetry file path
void PerformanceMonitor::set_telemetry_file(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(telemetry_mutex_);
    telemetry_file_ = filepath;
}

// Get current window size
size_t PerformanceMonitor::get_window_size() const {
    return window_size_;
}

// Get target GPU utilization
double PerformanceMonitor::get_target_gpu_utilization() const {
    return target_gpu_utilization_;
}

// Get target memory bandwidth
double PerformanceMonitor::get_target_memory_bandwidth() const {
    return target_memory_bandwidth_;
}

// Clear sliding window
void PerformanceMonitor::clear_window() {
    std::lock_guard<std::mutex> lock(window_mutex_);
    metrics_window_.clear();
}

// Get number of samples in window
size_t PerformanceMonitor::get_sample_count() const {
    std::lock_guard<std::mutex> lock(window_mutex_);
    return metrics_window_.size();
}

// Calculate average of metrics in window (private)
PerformanceMonitor::PerformanceMetrics PerformanceMonitor::calculate_average() const {
    if (metrics_window_.empty()) {
        return PerformanceMetrics();
    }
    
    PerformanceMetrics avg;
    
    // Sum all metrics
    for (const auto& metrics : metrics_window_) {
        avg.keys_per_sec += metrics.keys_per_sec;
        avg.gpu_utilization += metrics.gpu_utilization;
        avg.memory_bandwidth += metrics.memory_bandwidth;
        avg.active_blocks += metrics.active_blocks;
        avg.active_warps += metrics.active_warps;
    }
    
    // Calculate average
    size_t count = metrics_window_.size();
    avg.keys_per_sec /= count;
    avg.gpu_utilization /= count;
    avg.memory_bandwidth /= count;
    avg.active_blocks /= count;
    avg.active_warps /= count;
    
    // Use latest timestamp
    avg.timestamp = metrics_window_.back().timestamp;
    
    return avg;
}

// Check if GPU utilization is below target (private)
bool PerformanceMonitor::is_gpu_underutilized(const PerformanceMetrics& avg_metrics) const {
    return avg_metrics.gpu_utilization < target_gpu_utilization_;
}

// Check if memory bandwidth is below target (private)
bool PerformanceMonitor::is_memory_underutilized(const PerformanceMetrics& avg_metrics) const {
    return avg_metrics.memory_bandwidth < target_memory_bandwidth_;
}

} // namespace performance
} // namespace puzzle71

