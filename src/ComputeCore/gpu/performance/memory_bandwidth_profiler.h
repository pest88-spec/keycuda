#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <array>
#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

namespace keycuda {
namespace gpu {
namespace performance {

/**
 * @brief Comprehensive memory bandwidth profiling and metrics collection system
 *
 * Provides advanced memory bandwidth analysis capabilities:
 * - Real-time bandwidth measurement and monitoring
 * - Multi-level memory hierarchy profiling (global, shared, L2, texture)
 * - Transfer type analysis (H2D, D2H, D2D, P2P)
 * - Application-specific bandwidth utilization analysis
 * - Architecture-specific optimization recommendations
 * - Historical performance tracking and trend analysis
 * - Bottleneck identification and resolution suggestions
 */

enum class TransferType {
    HOST_TO_DEVICE,     // H2D transfers
    DEVICE_TO_HOST,     // D2H transfers
    DEVICE_TO_DEVICE,   // D2D transfers
    PEER_TO_PEER,       // P2P transfers
    UNIFIED_MEMORY,     // Unified memory transfers
    ASYNC_HOST_TO_DEVICE, // Async H2D
    ASYNC_DEVICE_TO_HOST, // Async D2H
    ASYNC_DEVICE_TO_DEVICE  // Async D2D
};

enum class MemoryLevel {
    GLOBAL_MEMORY,      // Global GPU memory
    SHARED_MEMORY,      // Shared memory per SM
    L2_CACHE,          // L2 cache
    L1_CACHE,          // L1 cache
    TEXTURE_MEMORY,    // Texture memory
    CONSTANT_MEMORY,   // Constant memory
    REGISTER_FILE,     // Register file
    HBM2,              // High Bandwidth Memory (HBM2)
    HBM3,              // High Bandwidth Memory (HBM3)
    GDDR6              // GDDR6 memory
};

enum class ProfilingMode {
    REAL_TIME,         // Real-time continuous monitoring
    SAMPLING,          // Periodic sampling
    BURST,            // Burst mode for specific operations
    CONTINUOUS,       // Continuous long-term monitoring
    ON_DEMAND,        // On-demand profiling
    AUTOMATIC,        // Automatic bottleneck detection
    BENCHMARK         // Benchmark mode
};

struct TransferMetrics {
    // Basic transfer information
    TransferType type;
    MemoryLevel level;
    size_t transfer_size;
    std::chrono::nanoseconds duration;
    std::chrono::system_clock::time_point timestamp;

    // Bandwidth calculations
    double bandwidth_gb_per_sec;           // Actual bandwidth achieved
    double theoretical_bandwidth_gb_per_sec; // Theoretical maximum
    double efficiency_percentage;          // Efficiency vs theoretical

    // Advanced metrics
    double throughput_mb_per_sec;
    double latency_us;
    double utilization_percentage;
    double overhead_percentage;

    // Transfer characteristics
    bool is_pinned_memory;
    bool is_asynchronous;
    bool is_cached;
    int stream_id;
    cudaStream_t stream_handle;

    // Error information
    bool has_errors;
    std::string error_message;
    int error_code;
};

struct MemoryLevelMetrics {
    MemoryLevel level;
    std::string level_name;

    // Bandwidth metrics
    double read_bandwidth_gb_per_sec;
    double write_bandwidth_gb_per_sec;
    double total_bandwidth_gb_per_sec;
    double peak_bandwidth_gb_per_sec;
    double average_bandwidth_gb_per_sec;

    // Capacity and utilization
    size_t total_capacity_bytes;
    size_t used_capacity_bytes;
    size_t available_capacity_bytes;
    double utilization_percentage;

    // Access patterns
    int total_reads;
    int total_writes;
    int cache_hits;
    int cache_misses;
    double cache_hit_rate;

    // Latency metrics
    std::chrono::nanoseconds average_read_latency;
    std::chrono::nanoseconds average_write_latency;
    std::chrono::nanoseconds min_latency;
    std::chrono::nanoseconds max_latency;

    // Performance characteristics
    double bandwidth_efficiency;
    double access_pattern_efficiency;
    double memory_divergence_rate;

    std::chrono::system_clock::time_point last_updated;
};

struct BandwidthProfile {
    std::string profile_name;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::chrono::milliseconds duration;

    // Aggregate bandwidth metrics
    double total_bandwidth_gb_per_sec;
    double peak_bandwidth_gb_per_sec;
    double average_bandwidth_gb_per_sec;
    double minimum_bandwidth_gb_per_sec;

    // Transfer breakdown
    std::map<TransferType, std::vector<TransferMetrics>> transfers_by_type;
    std::map<MemoryLevel, MemoryLevelMetrics> metrics_by_level;

    // Performance analysis
    double overall_efficiency;
    double memory_utilization_percentage;
    double compute_to_memory_ratio;
    std::vector<std::string> identified_bottlenecks;

    // Architecture-specific metrics
    int compute_capability;
    std::string device_name;
    size_t total_global_memory;
    size_t l2_cache_size;
    size_t shared_memory_per_block;

    // Quality metrics
    bool meets_performance_target;
    double target_bandwidth_gb_per_sec;
    double performance_variance;
    int sample_count;
};

struct BandwidthBenchmark {
    std::string benchmark_name;
    std::vector<TransferType> transfer_types;
    std::vector<size_t> transfer_sizes;
    std::vector<int> block_sizes;

    // Results
    std::map<TransferType, std::map<size_t, double>> bandwidth_results;
    std::map<TransferType, std::map<size_t, double>> latency_results;

    // Analysis
    TransferType optimal_transfer_type;
    size_t optimal_transfer_size;
    int optimal_block_size;
    double peak_performance_bandwidth;

    std::chrono::system_clock::time_point timestamp;
};

class MemoryBandwidthProfiler {
public:
    explicit MemoryBandwidthProfiler(int device_id = 0);
    ~MemoryBandwidthProfiler();

    // Profiling control
    bool Initialize(ProfilingMode mode = ProfilingMode::REAL_TIME);
    void Cleanup();
    bool IsInitialized() const;

    // Profiling operations
    void StartProfiling(const std::string& profile_name = "default");
    void StopProfiling();
    void PauseProfiling();
    void ResumeProfiling();
    bool IsProfiling() const;

    // Transfer monitoring
    void RecordTransfer(
        TransferType type,
        size_t transfer_size,
        std::chrono::nanoseconds duration,
        cudaStream_t stream = 0,
        bool is_pinned = false,
        bool is_async = false
    );

    void RecordAsyncTransferStart(
        TransferType type,
        size_t transfer_size,
        cudaStream_t stream,
        cudaEvent_t start_event
    );

    void RecordAsyncTransferEnd(
        TransferType type,
        cudaStream_t stream,
        cudaEvent_t end_event
    );

    // Memory level profiling
    MemoryLevelMetrics GetMemoryLevelMetrics(MemoryLevel level) const;
    std::vector<MemoryLevelMetrics> GetAllMemoryLevelMetrics() const;
    void UpdateMemoryLevelMetrics(MemoryLevel level);

    // Bandwidth analysis
    double GetCurrentBandwidthGBPerSec(TransferType type = TransferType::HOST_TO_DEVICE) const;
    double GetPeakBandwidthGBPerSec(TransferType type = TransferType::HOST_TO_DEVICE) const;
    double GetAverageBandwidthGBPerSec(TransferType type = TransferType::HOST_TO_DEVICE) const;
    double GetEfficiencyPercentage(TransferType type = TransferType::HOST_TO_DEVICE) const;

    // Profile analysis
    BandwidthProfile GetCurrentProfile() const;
    std::vector<BandwidthProfile> GetHistoricalProfiles() const;
    BandwidthProfile GetProfileByName(const std::string& name) const;

    // Benchmarking
    BandwidthBenchmark RunBandwidthBenchmark(
        const std::string& benchmark_name,
        const std::vector<TransferType>& transfer_types,
        const std::vector<size_t>& transfer_sizes,
        int iterations = 100
    );

    std::vector<BandwidthBenchmark> RunStandardBenchmarks();

    // Performance targets
    void SetPerformanceTarget(double bandwidth_gb_per_sec);
    double GetPerformanceTarget() const;
    bool IsMeetingPerformanceTarget() const;

    // Bottleneck detection
    std::vector<std::string> IdentifyBottlenecks() const;
    std::vector<std::string> GetOptimizationRecommendations() const;

    // Analytics and reporting
    json GetBandwidthAnalytics() const;
    std::string GenerateBandwidthReport() const;
    void ExportProfileData(const std::string& filename) const;
    void ExportBenchmarkData(const std::string& filename) const;

    // Real-time monitoring
    void EnableRealTimeMonitoring(bool enabled);
    void SetMonitoringInterval(std::chrono::milliseconds interval);
    std::chrono::milliseconds GetMonitoringInterval() const;

    // Configuration
    struct ProfilerConfig {
        ProfilingMode mode = ProfilingMode::REAL_TIME;
        bool enable_detailed_profiling = true;
        bool enable_benchmarking = true;
        bool enable_bottleneck_detection = true;
        bool enable_historical_tracking = true;
        bool enable_real_time_monitoring = false;

        std::chrono::milliseconds monitoring_interval{100}; // 100ms
        std::chrono::milliseconds profiling_timeout{300000}; // 5 minutes
        size_t max_profile_history = 1000;
        size_t max_transfer_history = 10000;

        double performance_target_gb_per_sec = 800.0; // 800 GB/s target
        double efficiency_threshold = 0.75; // 75% efficiency threshold
        int min_samples_for_analysis = 10;

        bool enable_memory_level_profiling = true;
        bool enable_transfer_type_analysis = true;
        bool enable_latency_measurement = true;
        bool enable_utilization_tracking = true;
    };

    void UpdateConfiguration(const ProfilerConfig& config);
    ProfilerConfig GetCurrentConfiguration() const;

    // Error handling
    enum class ErrorType {
        NONE = 0,
        INITIALIZATION_FAILED,
        CUDA_ERROR,
        PROFILING_ALREADY_ACTIVE,
        INVALID_PROFILE,
        BENCHMARK_FAILED,
        DATA_CORRUPTION,
        DEVICE_NOT_AVAILABLE
    };

    ErrorType GetLastError() const;
    std::string GetErrorMessage() const;
    bool AttemptErrorRecovery();

private:
    int device_id_;
    ProfilerConfig config_;
    ProfilingMode current_mode_;

    // Device properties
    cudaDeviceProp device_properties_;
    int compute_capability_;
    std::string device_name_;
    size_t total_global_memory_;
    size_t l2_cache_size_;
    size_t shared_memory_per_block_;

    // Profiling state
    bool initialized_;
    bool profiling_active_;
    bool profiling_paused_;
    std::string current_profile_name_;
    std::chrono::system_clock::time_point profiling_start_time_;

    // Data storage
    std::vector<TransferMetrics> transfer_history_;
    std::map<std::string, BandwidthProfile> profiles_;
    std::map<MemoryLevel, MemoryLevelMetrics> memory_level_metrics_;
    std::vector<BandwidthBenchmark> benchmark_history_;

    // Async transfer tracking
    struct AsyncTransferRecord {
        TransferType type;
        size_t transfer_size;
        cudaStream_t stream;
        cudaEvent_t start_event;
        std::chrono::system_clock::time_point start_time;
    };
    std::map<cudaStream_t, std::vector<AsyncTransferRecord>> async_transfers_;

    // Real-time monitoring
    bool real_time_monitoring_enabled_;
    std::unique_ptr<std::thread> monitoring_thread_;
    std::atomic<bool> shutdown_requested_;

    // Performance calculations
    mutable std::mutex data_mutex_;
    std::mutex profile_mutex_;
    std::mutex benchmark_mutex_;

    // Error handling
    ErrorType last_error_;
    std::string last_error_message_;

    // Internal methods
    bool InitializeDeviceProperties();
    void InitializeMemoryLevelMetrics();

    // Profiling implementation
    void UpdateRealTimeMetrics();
    void CalculateDerivedMetrics();
    void UpdateMemoryUtilization();

    // Bandwidth calculations
    double CalculateBandwidthGBPerSec(size_t bytes, std::chrono::nanoseconds duration) const;
    double CalculateEfficiency(double actual_bandwidth, double theoretical_bandwidth) const;
    double GetTheoreticalBandwidthGBPerSec(TransferType type, MemoryLevel level) const;

    // Memory level methods
    std::string GetMemoryLevelName(MemoryLevel level) const;
    std::vector<MemoryLevel> GetAvailableMemoryLevels() const;
    void UpdateMemoryLevelCounters(MemoryLevel level, bool is_read, size_t bytes);

    // Benchmark implementation
    double BenchmarkTransferType(
        TransferType type,
        size_t transfer_size,
        int iterations = 100
    );

    std::chrono::nanoseconds MeasureTransferLatency(
        TransferType type,
        size_t transfer_size,
        int iterations = 100
    );

    // Analysis methods
    std::vector<std::string> AnalyzeTransferPatterns() const;
    std::vector<std::string> AnalyzeMemoryUtilization() const;
    std::vector<std::string> AnalyzePerformanceBottlenecks() const;

    // Monitoring thread
    void MonitoringThreadFunction();
    void ProcessTransferQueue();

    // Data management
    void CleanupOldProfiles();
    void CleanupOldTransfers();
    void CompactDataStorage();

    // Error handling
    void SetError(ErrorType error, const std::string& message);
    bool RecoverFromError(ErrorType error);

    // Architecture-specific methods
    std::vector<TransferType> GetOptimalTransferTypes() const;
    std::vector<size_t> GetOptimalTransferSizes() const;
    bool SupportsP2PTransfers() const;
    bool SupportsUnifiedMemory() const;

    // Constants
    static constexpr double BYTES_TO_GB = 1.0 / (1024.0 * 1024.0 * 1024.0);
    static constexpr double NANOSECONDS_TO_SECONDS = 1.0 / 1e9;
    static constexpr size_t DEFAULT_TRANSFER_SIZE = 1024 * 1024; // 1MB
    static constexpr int DEFAULT_BENCHMARK_ITERATIONS = 100;
    static constexpr std::chrono::milliseconds DEFAULT_MONITORING_INTERVAL{100};
};

/**
 * @brief Factory function to create memory bandwidth profiler instance
 */
std::unique_ptr<MemoryBandwidthProfiler> CreateMemoryBandwidthProfiler(
    int device_id = 0
);

} // namespace performance
} // namespace gpu
} // namespace keycuda