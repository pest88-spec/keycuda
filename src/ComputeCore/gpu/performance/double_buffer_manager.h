#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <mutex>
#include <queue>
#include <functional>
#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

namespace keycuda {
namespace gpu {
namespace performance {

/**
 * @brief Advanced double-buffering system for optimal host-device communication overlap
 *
 * Provides comprehensive double-buffering capabilities for maximum overlap:
 * - Automatic buffer allocation and management
 * - Intelligent buffer switching and synchronization
 * - Multi-buffer support for pipeline depth optimization
 * - Performance monitoring and optimization
 * - Memory-mapped file support for large datasets
 * - Configurable buffer strategies based on workload characteristics
 */

enum class BufferStrategy {
    SINGLE_DOUBLE,     // Two buffers for basic double-buffering
    TRIPLE_BUFFER,     // Three buffers for better pipeline depth
    N_BUFFER,          // N buffers for complex pipelines
    ADAPTIVE,          // Adaptive buffer count based on workload
    MEMORY_MAPPED      // Memory-mapped file buffers
};

enum class BufferState {
    EMPTY,
    FILLING,
    READY,
    PROCESSING,
    COMPLETED,
    ERROR
};

struct BufferConfig {
    int buffer_id;
    size_t buffer_size;
    void* host_ptr;
    void* device_ptr;
    BufferState state;
    cudaStream_t stream;
    cudaEvent_t ready_event;
    cudaEvent_t completed_event;

    // Timing information
    std::chrono::system_clock::time_point fill_time;
    std::chrono::system_clock::time_point process_start_time;
    std::chrono::system_clock::time_point complete_time;
    std::chrono::microseconds fill_duration;
    std::chrono::microseconds process_duration;

    // Metadata
    std::string name;
    int sequence_number;
    bool is_pinned;
    size_t actual_data_size;
};

struct BufferMetrics {
    int buffer_id;
    std::string buffer_name;

    // Performance metrics
    std::chrono::microseconds average_fill_time;
    std::chrono::microseconds average_process_time;
    std::chrono::microseconds total_fill_time;
    std::chrono::microseconds total_process_time;
    double fill_process_overlap_ratio;

    // Throughput metrics
    double effective_bandwidth_gb_per_sec;
    double theoretical_bandwidth_gb_per_sec;
    double utilization_percentage;

    // Operation counts
    int total_cycles;
    int successful_cycles;
    int failed_cycles;
    double success_rate;

    // Pipeline metrics
    std::chrono::microseconds pipeline_depth;
    double pipeline_efficiency;
    int buffer_switches;

    // Memory metrics
    size_t total_bytes_transferred;
    size_t peak_memory_usage;

    std::chrono::system_clock::time_point last_updated;
};

class DoubleBufferManager {
public:
    explicit DoubleBufferManager(
        BufferStrategy strategy = BufferStrategy::SINGLE_DOUBLE,
        size_t buffer_size = 1024 * 1024, // 1MB default
        int num_buffers = 2,
        bool enable_pinned_memory = true
    );

    ~DoubleBufferManager();

    // Buffer management
    bool Initialize(size_t buffer_size, int num_buffers = 2);
    void Cleanup();
    bool IsInitialized() const;

    // Buffer operations
    int GetAvailableBuffer();
    int GetNextBufferForFilling();
    int GetNextBufferForProcessing();
    void MarkBufferReady(int buffer_id);
    void MarkBufferProcessing(int buffer_id);
    void MarkBufferCompleted(int buffer_id);
    void MarkBufferError(int buffer_id, const std::string& error);

    // Data operations
    bool FillBufferAsync(
        int buffer_id,
        const void* source_data,
        size_t data_size,
        std::function<void(int)> completion_callback = nullptr
    );

    bool ProcessBufferAsync(
        int buffer_id,
        std::function<void(int, void*, size_t)> process_func,
        std::function<void(int)> completion_callback = nullptr
    );

    // Synchronization
    void WaitForBuffer(int buffer_id);
    void WaitForAllBuffers();
    bool IsBufferReady(int buffer_id) const;
    bool IsBufferProcessing(int buffer_id) const;
    bool IsBufferCompleted(int buffer_id) const;

    // Pipeline operations
    void StartPipeline(
        std::function<void(int, void*, size_t)> fill_func,
        std::function<void(int, void*, size_t)> process_func,
        size_t total_data_size,
        size_t chunk_size = 0
    );

    void StopPipeline();
    bool IsPipelineRunning() const;
    std::chrono::microseconds GetPipelineThroughput() const;

    // Advanced features
    void EnableAdaptiveBuffering(bool enabled);
    void SetBufferStrategy(BufferStrategy strategy);
    void OptimizeBufferCount(size_t data_size, std::chrono::microseconds target_latency);
    void ReconfigureBuffers(size_t new_buffer_size, int new_num_buffers);

    // Memory-mapped file support
    bool EnableMemoryMapping(const std::string& file_path, size_t file_size);
    void* GetMappedBuffer(int buffer_id);
    size_t GetMappedBufferSize() const;

    // Performance monitoring
    BufferMetrics GetBufferMetrics(int buffer_id) const;
    std::vector<BufferMetrics> GetAllBufferMetrics() const;
    json GetPerformanceAnalytics() const;
    std::string GeneratePerformanceReport() const;

    // Configuration
    struct ManagerConfig {
        BufferStrategy strategy = BufferStrategy::SINGLE_DOUBLE;
        size_t default_buffer_size = 1024 * 1024;
        int default_num_buffers = 2;
        bool enable_pinned_memory = true;
        bool enable_profiling = true;
        bool enable_auto_optimization = true;
        std::chrono::microseconds pipeline_timeout{5000000}; // 5 seconds
        double target_utilization = 0.85;
        int max_buffer_reallocations = 4;
    };

    void UpdateConfiguration(const ManagerConfig& config);
    ManagerConfig GetCurrentConfiguration() const;

    // Error handling
    enum class ErrorType {
        NONE = 0,
        ALLOCATION_FAILED,
        CUDA_ERROR,
        SYNCHRONIZATION_ERROR,
        INVALID_BUFFER_ID,
        PIPELINE_ERROR,
        CONFIGURATION_ERROR,
        MEMORY_MAPPING_ERROR
    };

    ErrorType GetLastError() const;
    std::string GetErrorMessage() const;
    bool AttemptErrorRecovery();
    void SetErrorCallback(std::function<void(ErrorType, const std::string&)> callback);

    // Resource management
    size_t GetTotalMemoryUsage() const;
    size_t GetAllocatedMemoryUsage() const;
    int GetActiveBufferCount() const;
    double GetUtilizationRate() const;
    void ResetMetrics();

private:
    // Core data structures
    std::map<int, BufferConfig> buffers_;
    std::queue<int> available_buffers_;
    std::queue<int> ready_buffers_;
    std::queue<int> processing_buffers_;
    std::queue<int> completed_buffers_;

    // Configuration
    ManagerConfig config_;
    BufferStrategy current_strategy_;

    // State management
    bool initialized_;
    bool pipeline_running_;
    bool adaptive_buffering_enabled_;
    ErrorType last_error_;
    std::string last_error_message_;

    // Pipeline management
    std::unique_ptr<std::thread> pipeline_thread_;
    std::atomic<bool> shutdown_requested_;
    size_t pipeline_data_offset_;
    size_t total_pipeline_data_;
    std::chrono::high_resolution_clock::time_point pipeline_start_time_;

    // Memory-mapped file support
    std::string mapped_file_path_;
    void* mapped_file_ptr_;
    size_t mapped_file_size_;
    bool memory_mapping_enabled_;

    // Performance tracking
    std::map<int, BufferMetrics> buffer_metrics_;
    mutable std::mutex metrics_mutex_;

    // Synchronization
    mutable std::mutex buffers_mutex_;
    mutable std::mutex pipeline_mutex_;
    std::condition_variable pipeline_cv_;

    // Callbacks
    std::function<void(ErrorType, const std::string&)> error_callback_;

    // Internal methods
    bool AllocateBuffers(size_t buffer_size, int num_buffers);
    void DeallocateBuffers();
    int GetNextBufferId();

    // Buffer state management
    void UpdateBufferState(int buffer_id, BufferState new_state);
    void AdvanceBufferPipeline(int buffer_id);
    std::vector<int> GetBuffersByState(BufferState state) const;

    // Pipeline implementation
    void PipelineThreadFunction(
        std::function<void(int, void*, size_t)> fill_func,
        std::function<void(int, void*, size_t)> process_func,
        size_t total_data_size,
        size_t chunk_size
    );

    // Memory management
    void* AllocateHostMemory(size_t size, bool pinned);
    void* AllocateDeviceMemory(size_t size);
    void DeallocateHostMemory(void* ptr, bool pinned);
    void DeallocateDeviceMemory(void* ptr);

    // Memory mapping
    bool SetupMemoryMapping(const std::string& file_path, size_t file_size);
    void CleanupMemoryMapping();

    // Performance optimization
    void OptimizeBufferConfiguration();
    int CalculateOptimalBufferCount(size_t data_size, std::chrono::microseconds target_latency);
    void AdjustBufferSizesBasedOnPerformance();

    // Metrics collection
    void UpdateBufferMetrics(int buffer_id, const std::string& operation, std::chrono::microseconds duration);
    void CalculatePipelineMetrics();
    void UpdateUtilizationMetrics();

    // Error handling
    void SetError(ErrorType error, const std::string& message);
    void HandleBufferError(int buffer_id, const std::string& error);
    bool RecoverFromError(ErrorType error);

    // Constants
    static constexpr int MAX_BUFFERS = 32;
    static constexpr size_t MIN_BUFFER_SIZE = 1024; // 1KB
    static constexpr size_t MAX_BUFFER_SIZE = 1024ull * 1024 * 1024; // 1GB
    static constexpr std::chrono::microseconds PIPELINE_INTERVAL{100}; // 100 microseconds
};

/**
 * @brief Factory function to create double buffer manager instance
 */
std::unique_ptr<DoubleBufferManager> CreateDoubleBufferManager(
    BufferStrategy strategy = BufferStrategy::SINGLE_DOUBLE,
    size_t buffer_size = 1024 * 1024,
    int num_buffers = 2,
    bool enable_pinned_memory = true
);

} // namespace performance
} // namespace gpu
} // namespace keycuda