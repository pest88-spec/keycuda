/**
 * Unit Tests for WORM (Write-Once-Read-Many) Audit Logger
 * 
 * Tests P1-006 implementation:
 * - Append-only file mode
 * - 5-second flush mechanism
 * - File immutability
 * - SHA-256 integrity chain
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  tests/unit/test_audit_logger_worm.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P1-006 WORM storage testing
 * @spdx_license_identifier MIT
 */

#include <gtest/gtest.h>
#include "../../src/integration/audit_logger.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

class AuditLoggerWORMTest : public ::testing::Test {
protected:
    std::string test_log_dir_;
    
    void SetUp() override {
        // Create unique test directory
        test_log_dir_ = "test_audit_logs_" + std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());
        fs::create_directories(test_log_dir_);
    }
    
    void TearDown() override {
        // Clean up test directory
        if (fs::exists(test_log_dir_)) {
            // Remove immutable attributes first
            for (const auto& entry : fs::directory_iterator(test_log_dir_)) {
                if (entry.is_regular_file()) {
                    #ifdef _WIN32
                        // Windows: Remove read-only attribute
                        DWORD attrs = GetFileAttributesA(entry.path().string().c_str());
                        if (attrs != INVALID_FILE_ATTRIBUTES) {
                            SetFileAttributesA(entry.path().string().c_str(), 
                                              attrs & ~FILE_ATTRIBUTE_READONLY);
                        }
                    #else
                        // Linux: Restore write permissions
                        fs::permissions(entry.path(),
                            fs::perms::owner_write,
                            fs::perm_options::add);
                    #endif
                }
            }
            fs::remove_all(test_log_dir_);
        }
    }
};

// Test 1: Append-Only File Mode
TEST_F(AuditLoggerWORMTest, AppendOnlyMode) {
    // Create logger
    AuditLogger logger(test_log_dir_, true, 10000, 100, false);
    
    // Write first entry
    ASSERT_TRUE(logger.log_integration_operation("user1", "session1", "op1", "res1"));
    
    // Write second entry
    ASSERT_TRUE(logger.log_integration_operation("user2", "session2", "op2", "res2"));
    
    // Verify both entries exist
    auto entries = logger.get_all_entries();
    ASSERT_EQ(entries.size(), 2);
    
    // Verify entries are in order
    EXPECT_EQ(entries[0].user_id, "user1");
    EXPECT_EQ(entries[1].user_id, "user2");
}

// Test 2: 5-Second Flush Mechanism
TEST_F(AuditLoggerWORMTest, FlushMechanism) {
    // Create logger
    AuditLogger logger(test_log_dir_, true, 10000, 100, false);
    
    // Write entry
    ASSERT_TRUE(logger.log_integration_operation("user1", "session1", "test_op", "resource1"));
    
    // Wait for flush (5 seconds + buffer)
    std::this_thread::sleep_for(std::chrono::seconds(6));
    
    // Verify file exists and is not empty
    auto log_files = logger.get_log_files();
    ASSERT_FALSE(log_files.empty());
    
    std::string log_file = log_files[0];
    ASSERT_TRUE(fs::exists(log_file));
    EXPECT_GT(fs::file_size(log_file), 0);
}

// Test 3: File Immutability (Windows/Linux)
TEST_F(AuditLoggerWORMTest, FileImmutability) {
    std::string log_file;
    
    {
        // Create logger in scope
        AuditLogger logger(test_log_dir_, true, 10000, 100, false);
        
        // Write entry
        ASSERT_TRUE(logger.log_integration_operation("user1", "session1", "test_op", "resource1"));
        
        // Wait for flush
        std::this_thread::sleep_for(std::chrono::seconds(6));
        
        // Get log file path
        auto log_files = logger.get_log_files();
        ASSERT_FALSE(log_files.empty());
        log_file = log_files[0];
    }
    // Logger destroyed here, file should be immutable
    
    // Verify file exists
    ASSERT_TRUE(fs::exists(log_file));
    
    // Try to open file for writing (should fail or be read-only)
    std::ofstream file(log_file, std::ios::trunc);
    
    #ifdef _WIN32
        // Windows: File should not be writable
        EXPECT_FALSE(file.is_open() || !file.good());
    #else
        // Linux: File should be read-only
        auto perms = fs::status(log_file).permissions();
        EXPECT_EQ((perms & fs::perms::owner_write), fs::perms::none);
    #endif
}

// Test 4: SHA-256 Integrity Chain
TEST_F(AuditLoggerWORMTest, IntegrityChain) {
    // Create logger
    AuditLogger logger(test_log_dir_, true, 10000, 100, false);
    
    // Write multiple entries
    ASSERT_TRUE(logger.log_integration_operation("user1", "session1", "op1", "res1"));
    ASSERT_TRUE(logger.log_integration_operation("user2", "session2", "op2", "res2"));
    ASSERT_TRUE(logger.log_integration_operation("user3", "session3", "op3", "res3"));
    
    // Wait for flush
    std::this_thread::sleep_for(std::chrono::seconds(6));
    
    // Verify integrity chain
    auto entries = logger.get_all_entries();
    ASSERT_EQ(entries.size(), 3);
    
    // Verify hash chain
    for (size_t i = 1; i < entries.size(); i++) {
        EXPECT_EQ(entries[i].previous_hash, entries[i-1].entry_hash);
        EXPECT_FALSE(entries[i].entry_hash.empty());
    }
    
    // Verify integrity
    auto integrity_result = logger.verify_integrity();
    EXPECT_TRUE(integrity_result.is_valid);
    EXPECT_EQ(integrity_result.total_entries_checked, 3);
    EXPECT_TRUE(integrity_result.tampered_entries.empty());
}

// Test 5: Performance Test (1000 entries)
TEST_F(AuditLoggerWORMTest, PerformanceTest) {
    // Create logger
    AuditLogger logger(test_log_dir_, true, 10000, 100, false);
    
    auto start = std::chrono::steady_clock::now();
    
    // Write 1000 entries
    for (int i = 0; i < 1000; i++) {
        ASSERT_TRUE(logger.log_integration_operation(
            "user" + std::to_string(i),
            "session" + std::to_string(i),
            "test_op",
            "resource" + std::to_string(i)
        ));
    }
    
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Verify performance (should be < 5 seconds for 1000 entries)
    EXPECT_LT(elapsed, 5000);
    
    // Verify all entries written
    auto entries = logger.get_all_entries();
    EXPECT_EQ(entries.size(), 1000);
}

// Test 6: Concurrent Write Test
TEST_F(AuditLoggerWORMTest, ConcurrentWriteTest) {
    // Create logger
    AuditLogger logger(test_log_dir_, true, 10000, 100, false);
    
    // Create multiple threads writing concurrently
    std::vector<std::thread> threads;
    const int num_threads = 10;
    const int entries_per_thread = 100;
    
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&logger, t, entries_per_thread]() {
            for (int i = 0; i < entries_per_thread; i++) {
                logger.log_integration_operation(
                    "user_thread" + std::to_string(t),
                    "session_thread" + std::to_string(t),
                    "concurrent_op",
                    "resource" + std::to_string(i)
                );
            }
        });
    }
    
    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Wait for flush
    std::this_thread::sleep_for(std::chrono::seconds(6));
    
    // Verify all entries written
    auto entries = logger.get_all_entries();
    EXPECT_EQ(entries.size(), num_threads * entries_per_thread);
    
    // Verify integrity
    auto integrity_result = logger.verify_integrity();
    EXPECT_TRUE(integrity_result.is_valid);
}

// Test 7: Log Rotation with Immutability
TEST_F(AuditLoggerWORMTest, LogRotationImmutability) {
    // Create logger with small max entries
    AuditLogger logger(test_log_dir_, true, 10, 100, true);
    
    // Write more than max entries to trigger rotation
    for (int i = 0; i < 25; i++) {
        ASSERT_TRUE(logger.log_integration_operation(
            "user" + std::to_string(i),
            "session" + std::to_string(i),
            "test_op",
            "resource" + std::to_string(i)
        ));
    }
    
    // Wait for flush
    std::this_thread::sleep_for(std::chrono::seconds(6));
    
    // Verify multiple log files created
    auto log_files = logger.get_log_files();
    EXPECT_GT(log_files.size(), 1);
    
    // Verify all rotated files are immutable
    for (const auto& log_file : log_files) {
        if (fs::exists(log_file)) {
            #ifdef _WIN32
                DWORD attrs = GetFileAttributesA(log_file.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES) {
                    // Check if read-only
                    EXPECT_NE((attrs & FILE_ATTRIBUTE_READONLY), 0);
                }
            #else
                auto perms = fs::status(log_file).permissions();
                // Check if write permission is removed
                EXPECT_EQ((perms & fs::perms::owner_write), fs::perms::none);
            #endif
        }
    }
}

// Main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

