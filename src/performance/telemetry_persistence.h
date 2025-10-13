/**
 * Telemetry Persistence for Dynamic Performance Tuning
 * 
 * Implements P1-007: Dynamic Performance Tuning
 * - JSONL format telemetry storage
 * - SHA-256 digest protection
 * - Serialization and deserialization
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/performance/telemetry_persistence.h
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
#include <vector>

namespace puzzle71 {
namespace performance {

/**
 * Telemetry Persistence Manager
 * 
 * Handles serialization, deserialization, and integrity verification
 * of telemetry data with SHA-256 protection.
 */
class TelemetryPersistence {
public:
    /**
     * Save telemetry packet to JSONL file
     * 
     * @param packet Telemetry packet to save
     * @param filepath Path to JSONL file
     * @return True if successful
     */
    static bool save_telemetry(
        const PerformanceMonitor::TelemetryPacket& packet,
        const std::string& filepath
    );
    
    /**
     * Load telemetry history from JSONL file
     * 
     * @param filepath Path to JSONL file
     * @return Vector of telemetry packets
     */
    static std::vector<PerformanceMonitor::TelemetryPacket> load_telemetry_history(
        const std::string& filepath
    );
    
    /**
     * Calculate SHA-256 digest for telemetry packet
     * 
     * @param packet Telemetry packet
     * @return SHA-256 digest (hex string)
     */
    static std::string calculate_digest(
        const PerformanceMonitor::TelemetryPacket& packet
    );
    
    /**
     * Verify SHA-256 digest of telemetry packet
     * 
     * @param packet Telemetry packet with digest
     * @return True if digest is valid
     */
    static bool verify_digest(
        const PerformanceMonitor::TelemetryPacket& packet
    );
    
    /**
     * Serialize telemetry packet to JSON string
     * 
     * @param packet Telemetry packet
     * @return JSON string
     */
    static std::string serialize(
        const PerformanceMonitor::TelemetryPacket& packet
    );
    
    /**
     * Deserialize JSON string to telemetry packet
     * 
     * @param json_str JSON string
     * @return Telemetry packet
     */
    static PerformanceMonitor::TelemetryPacket deserialize(
        const std::string& json_str
    );

private:
    /**
     * Convert bytes to hex string
     * 
     * @param bytes Byte array
     * @param length Array length
     * @return Hex string
     */
    static std::string bytes_to_hex(const unsigned char* bytes, size_t length);
};

} // namespace performance
} // namespace puzzle71

