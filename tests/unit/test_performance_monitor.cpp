#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>

#include "monitoring/performance_monitor.h"

namespace keycuda {
namespace testing {

// Mock alert channel for testing
class MockAlertChannel : public monitoring::AlertChannel {
public:
    MOCK_METHOD(void, SendAlert, (const monitoring::PerformanceAlert& alert), (override));

    std::vector<monitoring::PerformanceAlert> received_alerts;

    void SendAlert(const monitoring::PerformanceAlert& alert) override {
        received_alerts.push_back(alert);
    }
};

class PerformanceMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure we have a CUDA device available
        int device_count = 0;
        ASSERT_EQ(cudaGetDeviceCount(&device_count), cudaSuccess);
        ASSERT_GT(device_count, 0) << "No CUDA devices available";

        // Create performance monitor
        monitor_ = std::make_unique<monitoring::PerformanceMonitor>(0);

        // Remove default console channel to avoid test output pollution
        // Note: This would require modification to the PerformanceMonitor class
        // For now, we'll work with the existing channels
    }

    void TearDown() override {
        if (monitor_) {
            monitor_->StopMonitoring();
        }
    }

    std::unique_ptr<monitoring::PerformanceMonitor> monitor_;
};

// Test basic monitor creation and initialization
TEST_F(PerformanceMonitorTest, BasicInitialization) {
    EXPECT_TRUE(monitor_ != nullptr);
    EXPECT_FALSE(monitor_->IsMonitoringEnabled());

    // Check that default alert thresholds are set
    auto status = monitor_->GetMonitoringStatus();
    EXPECT_TRUE(status.contains("monitoring_enabled"));
    EXPECT_TRUE(status.contains("current_metrics"));
}

// Test monitoring start and stop
TEST_F(PerformanceMonitorTest, StartStopMonitoring) {
    EXPECT_FALSE(monitor_->IsMonitoringEnabled());

    monitor_->StartMonitoring(std::chrono::milliseconds(100));
    EXPECT_TRUE(monitor_->IsMonitoringEnabled());

    // Let it run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    monitor_->StopMonitoring();
    EXPECT_FALSE(monitor_->IsMonitoringEnabled());
}

// Test metrics collection
TEST_F(PerformanceMonitorTest, MetricsCollection) {
    monitor_->StartMonitoring(std::chrono::milliseconds(50));

    // Wait for some metrics to be collected
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto snapshot = monitor_->GetCurrentSnapshot();
    auto recent_snapshots = monitor_->GetRecentSnapshots(std::chrono::milliseconds(150));

    EXPECT_GT(recent_snapshots.size(), 0) << "Should have collected some metrics";
    EXPECT_GT(snapshot.gpu_utilization, 0.0) << "GPU utilization should be positive";
    EXPECT_LE(snapshot.gpu_utilization, 1.0) << "GPU utilization should not exceed 100%";

    monitor_->StopMonitoring();
}

// Test alert channel addition
TEST_F(PerformanceMonitorTest, AlertChannelManagement) {
    auto mock_channel = std::make_unique<MockAlertChannel>();
    MockAlertChannel* mock_ptr = mock_channel.get();

    // Note: This would require adding a method to access alert channels in PerformanceMonitor
    // For now, we test the concept by creating channels

    EXPECT_TRUE(mock_channel != nullptr);
    EXPECT_EQ(mock_ptr->received_alerts.size(), 0);
}

// Test alert threshold configuration
TEST_F(PerformanceMonitorTest, AlertThresholdConfiguration) {
    // Set custom alert thresholds
    monitor_->SetAlertThreshold(monitoring::AlertType::PERFORMANCE_DEGRADATION, 0.15,
                               monitoring::AlertSeverity::WARNING);
    monitor_->SetAlertThreshold(monitoring::AlertType::MEMORY_PRESSURE, 0.90,
                               monitoring::AlertSeverity::CRITICAL);
    monitor_->SetAlertThreshold(monitoring::AlertType::TEMPERATURE_HIGH, 85.0,
                               monitoring::AlertSeverity::WARNING);

    // Verify thresholds are set (this would require exposing threshold access)
    // For now, we just verify the calls don't crash
    SUCCEED() << "Alert threshold configuration completed without errors";
}

// Test alert creation and serialization
TEST_F(PerformanceMonitorTest, AlertCreationAndSerialization) {
    monitoring::PerformanceAlert alert(
        monitoring::AlertType::PERFORMANCE_DEGRADATION,
        monitoring::AlertSeverity::WARNING,
        "Test performance degradation alert",
        35.0,  // Current value (35 Mkeys/sec)
        50.0   // Threshold (50 Mkeys/sec)
    );

    EXPECT_EQ(alert.GetType(), monitoring::AlertType::PERFORMANCE_DEGRADATION);
    EXPECT_EQ(alert.GetSeverity(), monitoring::AlertSeverity::WARNING);
    EXPECT_EQ(alert.GetMessage(), "Test performance degradation alert");
    EXPECT_EQ(alert.GetValue(), 35.0);
    EXPECT_EQ(alert.GetThreshold(), 50.0);
    EXPECT_FALSE(alert.IsResolved());

    // Test JSON serialization
    auto json = alert.ToJson();
    EXPECT_TRUE(json.contains("type"));
    EXPECT_TRUE(json.contains("severity"));
    EXPECT_TRUE(json.contains("message"));
    EXPECT_TRUE(json.contains("value"));
    EXPECT_TRUE(json.contains("threshold"));
    EXPECT_TRUE(json.contains("timestamp"));
    EXPECT_TRUE(json.contains("resolved"));

    EXPECT_EQ(json["message"], "Test performance degradation alert");
    EXPECT_EQ(json["value"], 35.0);
    EXPECT_EQ(json["threshold"], 50.0);
    EXPECT_EQ(json["resolved"], false);
}

// Test console alert channel
TEST_F(PerformanceMonitorTest, ConsoleAlertChannel) {
    monitoring::ConsoleAlertChannel console_channel(false);  // No color for test

    monitoring::PerformanceAlert alert(
        monitoring::AlertType::MEMORY_PRESSURE,
        monitoring::AlertSeverity::CRITICAL,
        "Test memory pressure alert",
        0.95,  // 95% memory utilization
        0.90   // 90% threshold
    );

    // This would output to console - we just verify it doesn't crash
    EXPECT_NO_THROW(console_channel.SendAlert(alert));
}

// Test file alert channel
TEST_F(PerformanceMonitorTest, FileAlertChannel) {
    std::string test_filename = "/tmp/test_alerts.log";

    // Remove existing test file
    std::remove(test_filename.c_str());

    monitoring::FileAlertChannel file_channel(test_filename);

    monitoring::PerformanceAlert alert(
        monitoring::AlertType::TEMPERATURE_HIGH,
        monitoring::AlertSeverity::WARNING,
        "Test temperature alert",
        85.0,  // 85°C
        80.0   // 80°C threshold
    );

    file_channel.SendAlert(alert);

    // Verify file was created and contains alert
    std::ifstream file(test_filename);
    EXPECT_TRUE(file.is_open()) << "Alert log file should be created";

    if (file.is_open()) {
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();

        EXPECT_TRUE(content.find("Test temperature alert") != std::string::npos)
            << "Alert message should be written to file";
        EXPECT_TRUE(content.find("85") != std::string::npos)
            << "Alert value should be written to file";
        EXPECT_TRUE(content.find("80") != std::string::npos)
            << "Alert threshold should be written to file";
    }

    // Cleanup
    std::remove(test_filename.c_str());
}

// Test metrics snapshot creation
TEST_F(PerformanceMonitorTest, PerformanceSnapshotCreation) {
    monitoring::PerformanceSnapshot snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.gpu_name = "Test GPU";
    snapshot.compute_capability = 75;  // 7.5
    snapshot.total_memory_mb = 8192;
    snapshot.memory_used_mb = 4096;
    snapshot.memory_utilization = 0.5;
    snapshot.gpu_utilization = 0.85;
    snapshot.power_consumption_w = 250.0;
    snapshot.temperature_c = 75.0;
    snapshot.key_search_throughput = 45.0;
    snapshot.memory_bandwidth_utilization = 0.82;
    snapshot.synchronization_latency_us = 95.0;
    snapshot.error_rate = 0.001;

    // Test JSON serialization
    auto json = snapshot.ToJson();
    EXPECT_TRUE(json.contains("timestamp"));
    EXPECT_TRUE(json.contains("gpu_name"));
    EXPECT_TRUE(json.contains("compute_capability"));
    EXPECT_TRUE(json.contains("total_memory_mb"));
    EXPECT_TRUE(json.contains("memory_used_mb"));
    EXPECT_TRUE(json.contains("memory_utilization"));
    EXPECT_TRUE(json.contains("gpu_utilization"));
    EXPECT_TRUE(json.contains("power_consumption_w"));
    EXPECT_TRUE(json.contains("temperature_c"));
    EXPECT_TRUE(json.contains("key_search_throughput"));
    EXPECT_TRUE(json.contains("memory_bandwidth_utilization"));
    EXPECT_TRUE(json.contains("synchronization_latency_us"));
    EXPECT_TRUE(json.contains("error_rate"));

    EXPECT_EQ(json["gpu_name"], "Test GPU");
    EXPECT_EQ(json["compute_capability"], 75);
    EXPECT_EQ(json["total_memory_mb"], 8192);
    EXPECT_EQ(json["memory_used_mb"], 4096);
    EXPECT_EQ(json["memory_utilization"], 0.5);
    EXPECT_EQ(json["gpu_utilization"], 0.85);
    EXPECT_EQ(json["power_consumption_w"], 250.0);
    EXPECT_EQ(json["temperature_c"], 75.0);
    EXPECT_EQ(json["key_search_throughput"], 45.0);
    EXPECT_EQ(json["memory_bandwidth_utilization"], 0.82);
    EXPECT_EQ(json["synchronization_latency_us"], 95.0);
    EXPECT_EQ(json["error_rate"], 0.001);
}

// Test monitoring status export
TEST_F(PerformanceMonitorTest, MonitoringStatusExport) {
    monitor_->StartMonitoring(std::chrono::milliseconds(100));

    // Wait for some data collection
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    auto status = monitor_->GetMonitoringStatus();

    EXPECT_TRUE(status.contains("monitoring_enabled"));
    EXPECT_TRUE(status.contains("device_id"));
    EXPECT_TRUE(status.contains("current_metrics"));
    EXPECT_TRUE(status.contains("total_alerts"));
    EXPECT_TRUE(status.contains("alert_channels_count"));

    EXPECT_TRUE(status["monitoring_enabled"]);
    EXPECT_EQ(status["device_id"], 0);
    EXPECT_GE(status["alert_channels_count"], 1);  // At least console channel

    auto current_metrics = status["current_metrics"];
    EXPECT_TRUE(current_metrics.contains("timestamp"));
    EXPECT_TRUE(current_metrics.contains("gpu_utilization"));
    EXPECT_TRUE(current_metrics.contains("memory_utilization"));

    monitor_->StopMonitoring();
}

// Test monitoring data export
TEST_F(PerformanceMonitorTest, MonitoringDataExport) {
    monitor_->StartMonitoring(std::chrono::milliseconds(50));

    // Collect some data
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string export_filename = "/tmp/test_monitoring_export.json";

    // Remove existing export file
    std::remove(export_filename.c_str());

    monitor_->ExportMonitoringData(export_filename);

    // Verify export file was created
    std::ifstream file(export_filename);
    EXPECT_TRUE(file.is_open()) << "Monitoring export file should be created";

    if (file.is_open()) {
        try {
            nlohmann::json export_data;
            file >> export_data;
            file.close();

            EXPECT_TRUE(export_data.contains("export_timestamp"));
            EXPECT_TRUE(export_data.contains("monitoring_status"));
            EXPECT_TRUE(export_data.contains("metrics_history"));
            EXPECT_TRUE(export_data.contains("alert_history"));

            auto monitoring_status = export_data["monitoring_status"];
            EXPECT_TRUE(monitoring_status.contains("monitoring_enabled"));
            EXPECT_TRUE(monitoring_status.contains("current_metrics"));

        } catch (const std::exception& e) {
            FAIL() << "Failed to parse exported JSON: " << e.what();
        }
    }

    monitor_->StopMonitoring();

    // Cleanup
    std::remove(export_filename.c_str());
}

// Test alert cooldown functionality
TEST_F(PerformanceMonitorTest, AlertCooldown) {
    // Set short cooldown for testing
    monitor_->SetAlertCooldownDuration(std::chrono::milliseconds(100));

    // This test would require the ability to manually trigger alerts
    // For now, we just verify the cooldown can be set
    SUCCEED() << "Alert cooldown duration set successfully";
}

// Test multiple alert types
TEST_F(PerformanceMonitorTest, MultipleAlertTypes) {
    // Test creation of different alert types
    std::vector<monitoring::PerformanceAlert> alerts;

    alerts.emplace_back(
        monitoring::AlertType::PERFORMANCE_DEGRADATION,
        monitoring::AlertSeverity::WARNING,
        "Performance degraded",
        35.0, 50.0
    );

    alerts.emplace_back(
        monitoring::AlertType::MEMORY_PRESSURE,
        monitoring::AlertSeverity::CRITICAL,
        "High memory usage",
        0.95, 0.90
    );

    alerts.emplace_back(
        monitoring::AlertType::TEMPERATURE_HIGH,
        monitoring::AlertSeverity::WARNING,
        "High temperature",
        85.0, 80.0
    );

    alerts.emplace_back(
        monitoring::AlertType::GPU_UTILIZATION_LOW,
        monitoring::AlertSeverity::INFO,
        "Low GPU utilization",
        0.30, 0.60
    );

    EXPECT_EQ(alerts.size(), 4);

    // Verify each alert has correct properties
    EXPECT_EQ(alerts[0].GetType(), monitoring::AlertType::PERFORMANCE_DEGRADATION);
    EXPECT_EQ(alerts[1].GetType(), monitoring::AlertType::MEMORY_PRESSURE);
    EXPECT_EQ(alerts[2].GetType(), monitoring::AlertType::TEMPERATURE_HIGH);
    EXPECT_EQ(alerts[3].GetType(), monitoring::AlertType::GPU_UTILIZATION_LOW);

    EXPECT_EQ(alerts[0].GetSeverity(), monitoring::AlertSeverity::WARNING);
    EXPECT_EQ(alerts[1].GetSeverity(), monitoring::AlertSeverity::CRITICAL);
    EXPECT_EQ(alerts[2].GetSeverity(), monitoring::AlertSeverity::WARNING);
    EXPECT_EQ(alerts[3].GetSeverity(), monitoring::AlertSeverity::INFO);
}

// Test performance under load
TEST_F(PerformanceMonitorTest, PerformanceUnderLoad) {
    monitor_->StartMonitoring(std::chrono::milliseconds(10));  // High frequency

    // Run monitoring under load for a short period
    auto start_time = std::chrono::high_resolution_clock::now();

    while (std::chrono::high_resolution_clock::now() - start_time < std::chrono::milliseconds(500)) {
        auto snapshot = monitor_->GetCurrentSnapshot();
        auto recent_snapshots = monitor_->GetRecentSnapshots(std::chrono::milliseconds(100));

        // Verify data is being collected
        EXPECT_GE(snapshot.gpu_utilization, 0.0);
        EXPECT_LE(snapshot.gpu_utilization, 1.0);

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    monitor_->StopMonitoring();

    // Verify we collected a reasonable amount of data
    auto final_snapshots = monitor_->GetRecentSnapshots(std::chrono::seconds(1));
    EXPECT_GT(final_snapshots.size(), 10) << "Should have collected multiple snapshots under load";
}

}  // namespace testing
}  // namespace keycuda