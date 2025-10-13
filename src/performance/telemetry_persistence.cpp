/**
 * Telemetry Persistence Implementation
 * 
 * Implements P1-007: Dynamic Performance Tuning
 * - JSONL format telemetry storage
 * - SHA-256 digest protection
 * - Serialization and deserialization
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/performance/telemetry_persistence.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P1-007 dynamic performance tuning
 * @spdx_license_identifier MIT
 */

#include "telemetry_persistence.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>

using json = nlohmann::json;

namespace puzzle71 {
namespace performance {

// Save telemetry packet to JSONL file
bool TelemetryPersistence::save_telemetry(
    const PerformanceMonitor::TelemetryPacket& packet,
    const std::string& filepath) {
    
    try {
        // Open file in append mode
        std::ofstream file(filepath, std::ios::app);
        if (!file.is_open()) {
            return false;
        }
        
        // Serialize to JSON
        std::string json_str = serialize(packet);
        
        // Write JSONL (one line per packet)
        file << json_str << "\n";
        
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

// Load telemetry history from JSONL file
std::vector<PerformanceMonitor::TelemetryPacket> TelemetryPersistence::load_telemetry_history(
    const std::string& filepath) {
    
    std::vector<PerformanceMonitor::TelemetryPacket> history;
    
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return history;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            // Deserialize each line
            auto packet = deserialize(line);
            
            // Verify digest
            if (verify_digest(packet)) {
                history.push_back(packet);
            }
        }
        
        return history;
    } catch (const std::exception& e) {
        return history;
    }
}

// Calculate SHA-256 digest for telemetry packet
std::string TelemetryPersistence::calculate_digest(
    const PerformanceMonitor::TelemetryPacket& packet) {
    
    // Build string to hash
    std::stringstream ss;
    ss << packet.gpu_model << "|"
       << packet.gpu_id << "|"
       << std::fixed << std::setprecision(6)
       << packet.metrics.keys_per_sec << "|"
       << packet.metrics.gpu_utilization << "|"
       << packet.metrics.memory_bandwidth << "|"
       << packet.metrics.active_blocks << "|"
       << packet.metrics.active_warps;
    
    // Add configuration
    for (const auto& [key, value] : packet.config) {
        ss << "|" << key << "=" << value;
    }
    
    std::string data = ss.str();
    
    // Calculate SHA-256
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.length(), hash);
    
    // Convert to hex string
    return bytes_to_hex(hash, SHA256_DIGEST_LENGTH);
}

// Verify SHA-256 digest of telemetry packet
bool TelemetryPersistence::verify_digest(
    const PerformanceMonitor::TelemetryPacket& packet) {
    
    std::string calculated_digest = calculate_digest(packet);
    return calculated_digest == packet.digest;
}

// Serialize telemetry packet to JSON string
std::string TelemetryPersistence::serialize(
    const PerformanceMonitor::TelemetryPacket& packet) {
    
    json j;
    
    // Basic info
    j["gpu_model"] = packet.gpu_model;
    j["gpu_id"] = packet.gpu_id;
    
    // Metrics
    j["metrics"]["keys_per_sec"] = packet.metrics.keys_per_sec;
    j["metrics"]["gpu_utilization"] = packet.metrics.gpu_utilization;
    j["metrics"]["memory_bandwidth"] = packet.metrics.memory_bandwidth;
    j["metrics"]["active_blocks"] = packet.metrics.active_blocks;
    j["metrics"]["active_warps"] = packet.metrics.active_warps;
    
    // Timestamp (convert to seconds since epoch)
    auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        packet.metrics.timestamp.time_since_epoch()
    ).count();
    j["metrics"]["timestamp_ms"] = timestamp_ms;
    
    // Configuration
    for (const auto& [key, value] : packet.config) {
        j["config"][key] = value;
    }
    
    // Digest
    j["digest"] = packet.digest;
    
    return j.dump();
}

// Deserialize JSON string to telemetry packet
PerformanceMonitor::TelemetryPacket TelemetryPersistence::deserialize(
    const std::string& json_str) {
    
    PerformanceMonitor::TelemetryPacket packet;
    
    try {
        json j = json::parse(json_str);
        
        // Basic info
        packet.gpu_model = j["gpu_model"];
        packet.gpu_id = j["gpu_id"];
        
        // Metrics
        packet.metrics.keys_per_sec = j["metrics"]["keys_per_sec"];
        packet.metrics.gpu_utilization = j["metrics"]["gpu_utilization"];
        packet.metrics.memory_bandwidth = j["metrics"]["memory_bandwidth"];
        packet.metrics.active_blocks = j["metrics"]["active_blocks"];
        packet.metrics.active_warps = j["metrics"]["active_warps"];
        
        // Timestamp (convert from milliseconds since epoch)
        if (j["metrics"].contains("timestamp_ms")) {
            int64_t timestamp_ms = j["metrics"]["timestamp_ms"];
            packet.metrics.timestamp = std::chrono::steady_clock::time_point(
                std::chrono::milliseconds(timestamp_ms)
            );
        }
        
        // Configuration
        for (auto& [key, value] : j["config"].items()) {
            packet.config[key] = value;
        }
        
        // Digest
        packet.digest = j["digest"];
        
    } catch (const std::exception& e) {
        // Return empty packet on error
    }
    
    return packet;
}

// Convert bytes to hex string (private)
std::string TelemetryPersistence::bytes_to_hex(const unsigned char* bytes, size_t length) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < length; ++i) {
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    
    return ss.str();
}

} // namespace performance
} // namespace puzzle71

