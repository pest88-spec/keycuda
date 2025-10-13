/**
 * Unit Tests for TelemetryPersistence
 * 
 * Tests P1-007: Dynamic Performance Tuning
 * - JSONL serialization and deserialization
 * - SHA-256 digest calculation and verification
 * - File I/O operations
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  tests/unit/test_telemetry_persistence.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P1-007 testing
 * @spdx_license_identifier MIT
 */

#include <gtest/gtest.h>
#include "../../src/performance/telemetry_persistence.h"
#include "../../src/performance/performance_monitor.h"
#include <filesystem>
#include <fstream>

using namespace puzzle71::performance;

class TelemetryPersistenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        std::filesystem::create_directories("test_telemetry");
        test_file_ = "test_telemetry/test.jsonl";
    }
    
    void TearDown() override {
        // Clean up test files
        std::filesystem::remove_all("test_telemetry");
    }
    
    PerformanceMonitor::TelemetryPacket create_test_packet() {
        PerformanceMonitor::TelemetryPacket packet;
        packet.gpu_model = "RTX 2080 Ti";
        packet.gpu_id = 0;
        packet.metrics.keys_per_sec = 1000.0;
        packet.metrics.gpu_utilization = 0.85;
        packet.metrics.memory_bandwidth = 0.75;
        packet.metrics.active_blocks = 100;
        packet.metrics.active_warps = 50;
        packet.metrics.timestamp = std::chrono::steady_clock::now();
        packet.config["block_size"] = 256;
        packet.config["grid_size"] = 1024;
        packet.config["points_per_thread"] = 32;
        return packet;
    }
    
    std::string test_file_;
};

// Test 1: Calculate digest
TEST_F(TelemetryPersistenceTest, CalculateDigest) {
    auto packet = create_test_packet();
    
    std::string digest = TelemetryPersistence::calculate_digest(packet);
    
    // Digest should be 64 characters (SHA-256 hex)
    EXPECT_EQ(digest.length(), 64);
    
    // Digest should be deterministic
    std::string digest2 = TelemetryPersistence::calculate_digest(packet);
    EXPECT_EQ(digest, digest2);
}

// Test 2: Verify digest - Valid
TEST_F(TelemetryPersistenceTest, VerifyDigest_Valid) {
    auto packet = create_test_packet();
    packet.digest = TelemetryPersistence::calculate_digest(packet);
    
    EXPECT_TRUE(TelemetryPersistence::verify_digest(packet));
}

// Test 3: Verify digest - Invalid
TEST_F(TelemetryPersistenceTest, VerifyDigest_Invalid) {
    auto packet = create_test_packet();
    packet.digest = "invalid_digest_1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
    
    EXPECT_FALSE(TelemetryPersistence::verify_digest(packet));
}

// Test 4: Serialize packet
TEST_F(TelemetryPersistenceTest, SerializePacket) {
    auto packet = create_test_packet();
    packet.digest = TelemetryPersistence::calculate_digest(packet);
    
    std::string json_str = TelemetryPersistence::serialize(packet);
    
    // Should contain key fields
    EXPECT_NE(json_str.find("gpu_model"), std::string::npos);
    EXPECT_NE(json_str.find("RTX 2080 Ti"), std::string::npos);
    EXPECT_NE(json_str.find("keys_per_sec"), std::string::npos);
    EXPECT_NE(json_str.find("digest"), std::string::npos);
}

// Test 5: Deserialize packet
TEST_F(TelemetryPersistenceTest, DeserializePacket) {
    auto original = create_test_packet();
    original.digest = TelemetryPersistence::calculate_digest(original);
    
    std::string json_str = TelemetryPersistence::serialize(original);
    auto deserialized = TelemetryPersistence::deserialize(json_str);
    
    EXPECT_EQ(deserialized.gpu_model, original.gpu_model);
    EXPECT_EQ(deserialized.gpu_id, original.gpu_id);
    EXPECT_DOUBLE_EQ(deserialized.metrics.keys_per_sec, original.metrics.keys_per_sec);
    EXPECT_DOUBLE_EQ(deserialized.metrics.gpu_utilization, original.metrics.gpu_utilization);
    EXPECT_DOUBLE_EQ(deserialized.metrics.memory_bandwidth, original.metrics.memory_bandwidth);
    EXPECT_EQ(deserialized.config["block_size"], original.config["block_size"]);
    EXPECT_EQ(deserialized.digest, original.digest);
}

// Test 6: Save telemetry to file
TEST_F(TelemetryPersistenceTest, SaveTelemetry) {
    auto packet = create_test_packet();
    packet.digest = TelemetryPersistence::calculate_digest(packet);
    
    EXPECT_TRUE(TelemetryPersistence::save_telemetry(packet, test_file_));
    
    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(test_file_));
    
    // Verify file content
    std::ifstream file(test_file_);
    std::string line;
    std::getline(file, line);
    
    EXPECT_NE(line.find("RTX 2080 Ti"), std::string::npos);
}

// Test 7: Load telemetry history
TEST_F(TelemetryPersistenceTest, LoadTelemetryHistory) {
    // Save multiple packets
    for (int i = 0; i < 3; ++i) {
        auto packet = create_test_packet();
        packet.gpu_id = i;
        packet.digest = TelemetryPersistence::calculate_digest(packet);
        
        TelemetryPersistence::save_telemetry(packet, test_file_);
    }
    
    // Load history
    auto history = TelemetryPersistence::load_telemetry_history(test_file_);
    
    EXPECT_EQ(history.size(), 3);
    EXPECT_EQ(history[0].gpu_id, 0);
    EXPECT_EQ(history[1].gpu_id, 1);
    EXPECT_EQ(history[2].gpu_id, 2);
}

// Test 8: Load telemetry history - Empty file
TEST_F(TelemetryPersistenceTest, LoadTelemetryHistory_EmptyFile) {
    // Create empty file
    std::ofstream file(test_file_);
    file.close();
    
    auto history = TelemetryPersistence::load_telemetry_history(test_file_);
    
    EXPECT_EQ(history.size(), 0);
}

// Test 9: Load telemetry history - Non-existent file
TEST_F(TelemetryPersistenceTest, LoadTelemetryHistory_NonExistent) {
    auto history = TelemetryPersistence::load_telemetry_history("non_existent.jsonl");
    
    EXPECT_EQ(history.size(), 0);
}

// Test 10: Digest tamper detection
TEST_F(TelemetryPersistenceTest, DigestTamperDetection) {
    auto packet = create_test_packet();
    packet.digest = TelemetryPersistence::calculate_digest(packet);
    
    // Save packet
    TelemetryPersistence::save_telemetry(packet, test_file_);
    
    // Manually tamper with file
    std::ifstream in_file(test_file_);
    std::string content;
    std::getline(in_file, content);
    in_file.close();
    
    // Modify content (change keys_per_sec)
    size_t pos = content.find("1000.0");
    if (pos != std::string::npos) {
        content.replace(pos, 6, "2000.0");
    }
    
    // Write tampered content
    std::ofstream out_file(test_file_);
    out_file << content << "\n";
    out_file.close();
    
    // Load history - should detect tamper
    auto history = TelemetryPersistence::load_telemetry_history(test_file_);
    
    // Tampered packet should be rejected
    EXPECT_EQ(history.size(), 0);
}

// Test 11: JSONL format - Multiple packets
TEST_F(TelemetryPersistenceTest, JSONLFormat_MultiplePackets) {
    // Save 5 packets
    for (int i = 0; i < 5; ++i) {
        auto packet = create_test_packet();
        packet.gpu_id = i;
        packet.metrics.keys_per_sec = 1000.0 + i * 100.0;
        packet.digest = TelemetryPersistence::calculate_digest(packet);
        
        TelemetryPersistence::save_telemetry(packet, test_file_);
    }
    
    // Verify file has 5 lines
    std::ifstream file(test_file_);
    int line_count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            ++line_count;
        }
    }
    
    EXPECT_EQ(line_count, 5);
}

// Test 12: Round-trip serialization
TEST_F(TelemetryPersistenceTest, RoundTripSerialization) {
    auto original = create_test_packet();
    original.digest = TelemetryPersistence::calculate_digest(original);
    
    // Serialize
    std::string json_str = TelemetryPersistence::serialize(original);
    
    // Deserialize
    auto deserialized = TelemetryPersistence::deserialize(json_str);
    
    // Verify digest still matches
    EXPECT_TRUE(TelemetryPersistence::verify_digest(deserialized));
}

// Main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

