#include "ComputeCore/gpu/performance/double_buffer_manager.h"
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <fstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <sstream>

namespace keycuda {
namespace gpu {
namespace performance {

DoubleBufferManager::DoubleBufferManager(
    BufferStrategy strategy,
    size_t buffer_size,
    int num_buffers,
    bool enable_pinned_memory)

    : initialized_(false)
    , pipeline_running_(false)
    , adaptive_buffering_enabled_(false)
    , last_error_(ErrorType::NONE)
    , shutdown_requested_(false)
    , pipeline_data_offset_(0)
    , total_pipeline_data_(0)
    , mapped_file_ptr_(nullptr)
    , mapped_file_size_(0)
    , memory_mapping_enabled_(false) {

    // Initialize configuration
    config_.strategy = strategy;
    config_.default_buffer_size = buffer_size;
    config_.default_num_buffers = num_buffers;
    config_.enable_pinned_memory = enable_pinned_memory;
    current_strategy_ = strategy;

    Initialize(buffer_size, num_buffers);
}

DoubleBufferManager::~DoubleBufferManager() {
    Cleanup();
}

bool DoubleBufferManager::Initialize(size_t buffer_size, int num_buffers) {
    if (initialized_) {
        Cleanup();
    }

    try {
        if (!AllocateBuffers(buffer_size, num_buffers)) {
            return false;
        }

        if (config_.enable_profiling) {
            // Initialize metrics for all buffers
            for (const auto& [id, buffer] : buffers_) {
                BufferMetrics metrics{};
                metrics.buffer_id = id;
                metrics.buffer_name = buffer.name;
                metrics.total_cycles = 0;
                metrics.successful_cycles = 0;
                metrics.failed_cycles = 0;
                metrics.success_rate = 1.0;
                metrics.total_bytes_transferred = 0;
                metrics.peak_memory_usage = 0;
                metrics.last_updated = std::chrono::system_clock::now();

                buffer_metrics_[id] = metrics;
            }
        }

        initialized_ = true;
        return true;

    } catch (const std::exception& e) {
        SetError(ErrorType::ALLOCATION_FAILED, "Initialization failed: " + std::string(e.what()));
        return false;
    }
}

void DoubleBufferManager::Cleanup() {
    if (!initialized_) return;

    // Stop pipeline if running
    StopPipeline();

    // Deallocate buffers
    DeallocateBuffers();

    // Cleanup memory mapping
    if (memory_mapping_enabled_) {
        CleanupMemoryMapping();
    }

    // Clear metrics
    buffer_metrics_.clear();

    initialized_ = false;
}

bool DoubleBufferManager::IsInitialized() const {
    return initialized_;
}

int DoubleBufferManager::GetAvailableBuffer() {
    if (!initialized_) return -1;

    std::lock_guard<std::mutex> lock(buffers_mutex_);

    if (!available_buffers_.empty()) {
        int buffer_id = available_buffers_.front();
        available_buffers_.pop();
        return buffer_id;
    }

    return -1; // No available buffers
}

int DoubleBufferManager::GetNextBufferForFilling() {
    if (!initialized_) return -1;

    std::lock_guard<std::mutex> lock(buffers_mutex_);

    // Try to get an available buffer
    if (!available_buffers_.empty()) {
        int buffer_id = available_buffers_.front();
        available_buffers_.pop();
        UpdateBufferState(buffer_id, BufferState::FILLING);
        return buffer_id;
    }

    // Try to reuse a completed buffer
    if (!completed_buffers_.empty()) {
        int buffer_id = completed_buffers_.front();
        completed_buffers_.pop();
        UpdateBufferState(buffer_id, BufferState::FILLING);
        return buffer_id;
    }

    return -1;
}

int DoubleBufferManager::GetNextBufferForProcessing() {
    if (!initialized_) return -1;

    std::lock_guard<std::mutex> lock(buffers_mutex_);

    if (!ready_buffers_.empty()) {
        int buffer_id = ready_buffers_.front();
        ready_buffers_.pop();
        UpdateBufferState(buffer_id, BufferState::PROCESSING);
        return buffer_id;
    }

    return -1;
}

void DoubleBufferManager::MarkBufferReady(int buffer_id) {
    if (!initialized_ || !IsValidBufferId(buffer_id)) return;

    std::lock_guard<std::mutex> lock(buffers_mutex_);
    UpdateBufferState(buffer_id, BufferState::READY);
}

void DoubleBufferManager::MarkBufferProcessing(int buffer_id) {
    if (!initialized_ || !IsValidBufferId(buffer_id)) return;

    std::lock_guard<std::mutex> lock(buffers_mutex_);
    UpdateBufferState(buffer_id, BufferState::PROCESSING);
}

void DoubleBufferManager::MarkBufferCompleted(int buffer_id) {
    if (!initialized_ || !IsValidBufferId(buffer_id)) return;

    std::lock_guard<std::mutex> lock(buffers_mutex_);
    UpdateBufferState(buffer_id, BufferState::COMPLETED);

    // Update metrics
    auto it = buffer_metrics_.find(buffer_id);
    if (it != buffer_metrics_.end()) {
        auto& metrics = it->second;
        metrics.total_cycles++;
        metrics.successful_cycles++;
        metrics.success_rate = static_cast<double>(metrics.successful_cycles) / metrics.total_cycles;
        metrics.last_updated = std::chrono::system_clock::now();
    }
}

void DoubleBufferManager::MarkBufferError(int buffer_id, const std::string& error) {
    if (!initialized_ || !IsValidBufferId(buffer_id)) return;

    std::lock_guard<std::mutex> lock(buffers_mutex_);
    UpdateBufferState(buffer_id, BufferState::ERROR);

    // Update metrics
    auto it = buffer_metrics_.find(buffer_id);
    if (it != buffer_metrics_.end()) {
        auto& metrics = it->second;
        metrics.total_cycles++;
        metrics.failed_cycles++;
        metrics.success_rate = static_cast<double>(metrics.successful_cycles) / metrics.total_cycles;
        metrics.last_updated = std::chrono::system_clock::now();
    }

    HandleBufferError(buffer_id, error);
}

bool DoubleBufferManager::FillBufferAsync(
    int buffer_id,
    const void* source_data,
    size_t data_size,
    std::function<void(int)> completion_callback) {

    if (!initialized_ || !IsValidBufferId(buffer_id)) {
        SetError(ErrorType::INVALID_BUFFER_ID, "Invalid buffer ID");
        return false;
    }

    auto buffer_it = buffers_.find(buffer_id);
    if (buffer_it == buffers_.end()) {
        SetError(ErrorType::INVALID_BUFFER_ID, "Buffer not found");
        return false;
    }

    auto& buffer = buffer_it->second;
    if (data_size > buffer.buffer_size) {
        SetError(ErrorType::CONFIGURATION_ERROR, "Data size exceeds buffer capacity");
        return false;
    }

    try {
        auto start_time = std::chrono::high_resolution_clock::now();

        // Record timing
        buffer.fill_time = start_time;

        // Asynchronous memory copy
        cudaMemcpyAsync(buffer.device_ptr, source_data, data_size,
                       cudaMemcpyHostToDevice, buffer.stream);

        // Record event for completion detection
        cudaEventRecord(buffer.ready_event, buffer.stream);

        // Set completion callback if provided
        if (completion_callback) {
            // In a real implementation, this would use CUDA streams and callbacks
            // For now, we'll call it immediately (simplified)
            completion_callback(buffer_id);
        }

        // Update metrics
        UpdateBufferMetrics(buffer_id, "fill", std::chrono::microseconds(1000)); // Estimate

        return true;

    } catch (const std::exception& e) {
        SetError(ErrorType::CUDA_ERROR, "Buffer fill failed: " + std::string(e.what()));
        MarkBufferError(buffer_id, e.what());
        return false;
    }
}

bool DoubleBufferManager::ProcessBufferAsync(
    int buffer_id,
    std::function<void(int, void*, size_t)> process_func,
    std::function<void(int)> completion_callback) {

    if (!initialized_ || !IsValidBufferId(buffer_id) || !process_func) {
        SetError(ErrorType::INVALID_CONFIGURATION, "Invalid parameters for buffer processing");
        return false;
    }

    auto buffer_it = buffers_.find(buffer_id);
    if (buffer_it == buffers_.end()) {
        SetError(ErrorType::INVALID_BUFFER_ID, "Buffer not found");
        return false;
    }

    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        buffer_it->second.process_start_time = start_time;

        // Launch processing function asynchronously
        std::thread([this, buffer_id, process_func, completion_callback, buffer_it]() {
            try {
                process_func(buffer_id, buffer_it->second.device_ptr, buffer_it->second.actual_data_size);

                auto end_time = std::chrono::high_resolution_clock::now();
                buffer_it->second.complete_time = end_time;
                buffer_it->second.process_duration =
                    std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

                if (completion_callback) {
                    completion_callback(buffer_id);
                }

                MarkBufferCompleted(buffer_id);

            } catch (const std::exception& e) {
                MarkBufferError(buffer_id, e.what());
            }
        }).detach();

        // Update metrics
        UpdateBufferMetrics(buffer_id, "process", std::chrono::microseconds(5000)); // Estimate

        return true;

    } catch (const std::exception& e) {
        SetError(ErrorType::PIPELINE_ERROR, "Buffer processing failed: " + std::string(e.what()));
        MarkBufferError(buffer_id, e.what());
        return false;
    }
}

void DoubleBufferManager::WaitForBuffer(int buffer_id) {
    if (!initialized_ || !IsValidBufferId(buffer_id)) return;

    auto buffer_it = buffers_.find(buffer_id);
    if (buffer_it != buffers_.end()) {
        cudaStreamSynchronize(buffer_it->second.stream);
    }
}

void DoubleBufferManager::WaitForAllBuffers() {
    std::lock_guard<std::mutex> lock(buffers_mutex_);

    for (const auto& [id, buffer] : buffers_) {
        cudaStreamSynchronize(buffer.stream);
    }
}

bool DoubleBufferManager::IsBufferReady(int buffer_id) const {
    if (!initialized_ || !IsValidBufferId(buffer_id)) return false;

    std::lock_guard<std::mutex> lock(buffers_mutex_);
    auto it = buffers_.find(buffer_id);
    return it != buffers_.end() && it->second.state == BufferState::READY;
}

bool DoubleBufferManager::IsBufferProcessing(int buffer_id) const {
    if (!initialized_ || !IsValidBufferId(buffer_id)) return false;

    std::lock_guard<std::mutex> lock(buffers_mutex_);
    auto it = buffers_.find(buffer_id);
    return it != buffers_.end() && it->second.state == BufferState::PROCESSING;
}

bool DoubleBufferManager::IsBufferCompleted(int buffer_id) const {
    if (!initialized_ || !IsValidBufferId(buffer_id)) return false;

    std::lock_guard<std::mutex> lock(buffers_mutex_);
    auto it = buffers_.find(buffer_id);
    return it != buffers_.end() && it->second.state == BufferState::COMPLETED;
}

void DoubleBufferManager::StartPipeline(
    std::function<void(int, void*, size_t)> fill_func,
    std::function<void(int, void*, size_t)> process_func,
    size_t total_data_size,
    size_t chunk_size) {

    if (pipeline_running_) {
        SetError(ErrorType::PIPELINE_ERROR, "Pipeline already running");
        return;
    }

    if (!fill_func || !process_func) {
        SetError(ErrorType::INVALID_CONFIGURATION, "Invalid pipeline functions");
        return;
    }

    if (chunk_size == 0) {
        chunk_size = config_.default_buffer_size;
    }

    pipeline_running_ = true;
    total_pipeline_data_ = total_data_size;
    pipeline_data_offset_ = 0;
    pipeline_start_time_ = std::chrono::high_resolution_clock::now();

    // Start pipeline thread
    shutdown_requested_ = false;
    pipeline_thread_ = std::make_unique<std::thread>(
        &DoubleBufferManager::PipelineThreadFunction, this,
        fill_func, process_func, total_data_size, chunk_size
    );
}

void DoubleBufferManager::StopPipeline() {
    if (!pipeline_running_) return;

    shutdown_requested_ = true;
    pipeline_cv_.notify_all();

    if (pipeline_thread_ && pipeline_thread_->joinable()) {
        pipeline_thread_->join();
    }

    pipeline_running_ = false;
}

bool DoubleBufferManager::IsPipelineRunning() const {
    return pipeline_running_;
}

std::chrono::microseconds DoubleBufferManager::GetPipelineThroughput() const {
    if (!pipeline_running_ || total_pipeline_data_ == 0) {
        return std::chrono::microseconds{0};
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - pipeline_start_time_
    );

    if (elapsed.count() > 0) {
        return std::chrono::microseconds(
            (total_pipeline_data_ * 1000000) / elapsed.count()
        );
    }

    return std::chrono::microseconds{0};
}

void DoubleBufferManager::EnableAdaptiveBuffering(bool enabled) {
    adaptive_buffering_enabled_ = enabled && config_.enable_auto_optimization;
}

void DoubleBufferManager::SetBufferStrategy(BufferStrategy strategy) {
    current_strategy_ = strategy;
    config_.strategy = strategy;

    if (initialized_ && adaptive_buffering_enabled_) {
        OptimizeBufferConfiguration();
    }
}

void DoubleBufferManager::OptimizeBufferCount(size_t data_size, std::chrono::microseconds target_latency) {
    if (!adaptive_buffering_enabled_) return;

    int optimal_count = CalculateOptimalBufferCount(data_size, target_latency);
    if (optimal_count > 0 && optimal_count != config_.default_num_buffers) {
        ReconfigureBuffers(config_.default_buffer_size, optimal_count);
    }
}

void DoubleBufferManager::ReconfigureBuffers(size_t new_buffer_size, int new_num_buffers) {
    if (!initialized_) {
        Initialize(new_buffer_size, new_num_buffers);
        return;
    }

    if (new_buffer_size < MIN_BUFFER_SIZE || new_buffer_size > MAX_BUFFER_SIZE ||
        new_num_buffers < 2 || new_num_buffers > MAX_BUFFERS) {
        SetError(ErrorType::CONFIGURATION_ERROR, "Invalid buffer configuration");
        return;
    }

    // Wait for all current operations to complete
    WaitForAllBuffers();

    // Cleanup and reinitialize
    Cleanup();
    Initialize(new_buffer_size, new_num_buffers);
}

bool DoubleBufferManager::EnableMemoryMapping(const std::string& file_path, size_t file_size) {
    if (memory_mapping_enabled_) {
        CleanupMemoryMapping();
    }

    return SetupMemoryMapping(file_path, file_size);
}

void* DoubleBufferManager::GetMappedBuffer(int buffer_id) {
    if (!memory_mapping_enabled_ || !IsValidBufferId(buffer_id)) {
        return nullptr;
    }

    auto buffer_it = buffers_.find(buffer_id);
    if (buffer_it != buffers_.end()) {
        return buffer_it->second.host_ptr;
    }

    return nullptr;
}

size_t DoubleBufferManager::GetMappedBufferSize() const {
    return mapped_file_size_;
}

BufferMetrics DoubleBufferManager::GetBufferMetrics(int buffer_id) const {
    if (!IsValidBufferId(buffer_id)) return {};

    std::lock_guard<std::mutex> lock(metrics_mutex_);
    auto it = buffer_metrics_.find(buffer_id);
    return (it != buffer_metrics_.end()) ? it->second : BufferMetrics{};
}

std::vector<BufferMetrics> DoubleBufferManager::GetAllBufferMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    std::vector<BufferMetrics> metrics;
    for (const auto& [id, metric] : buffer_metrics_) {
        metrics.push_back(metric);
    }
    return metrics;
}

json DoubleBufferManager::GetPerformanceAnalytics() const {
    json analytics;

    auto all_metrics = GetAllBufferMetrics();

    // Aggregate metrics
    double total_fill_time = 0.0;
    double total_process_time = 0.0;
    double total_bandwidth = 0.0;
    int total_cycles = 0;
    int successful_cycles = 0;
    size_t total_bytes = 0;

    for (const auto& metrics : all_metrics) {
        total_fill_time += metrics.average_fill_time.count();
        total_process_time += metrics.average_process_time.count();
        total_bandwidth += metrics.effective_bandwidth_gb_per_sec;
        total_cycles += metrics.total_cycles;
        successful_cycles += metrics.successful_cycles;
        total_bytes += metrics.total_bytes_transferred;
    }

    analytics["total_buffers"] = buffers_.size();
    analytics["total_cycles"] = total_cycles;
    analytics["successful_cycles"] = successful_cycles;
    analytics["overall_success_rate"] = total_cycles > 0 ?
        static_cast<double>(successful_cycles) / total_cycles : 1.0;
    analytics["total_bytes_transferred"] = total_bytes;

    if (!all_metrics.empty()) {
        analytics["average_fill_time_us"] = total_fill_time / all_metrics.size();
        analytics["average_process_time_us"] = total_process_time / all_metrics.size();
        analytics["average_bandwidth_gb_per_sec"] = total_bandwidth / all_metrics.size();
    }

    // Pipeline metrics
    if (pipeline_running_) {
        auto throughput = GetPipelineThroughput();
        analytics["pipeline_running"] = true;
        analytics["pipeline_throughput_bytes_per_sec"] = throughput.count();
        analytics["pipeline_data_processed"] = pipeline_data_offset_;
        analytics["pipeline_total_data"] = total_pipeline_data_;
    } else {
        analytics["pipeline_running"] = false;
    }

    // Configuration
    analytics["config"]["buffer_strategy"] = static_cast<int>(config_.strategy);
    analytics["config"]["buffer_size"] = config_.default_buffer_size;
    analytics["config"]["num_buffers"] = config_.default_num_buffers;
    analytics["config"]["pinned_memory"] = config_.enable_pinned_memory;
    analytics["config"]["adaptive_buffering"] = adaptive_buffering_enabled_;

    // Memory mapping
    analytics["memory_mapping_enabled"] = memory_mapping_enabled_;
    if (memory_mapping_enabled_) {
        analytics["mapped_file_path"] = mapped_file_path_;
        analytics["mapped_file_size"] = mapped_file_size_;
    }

    return analytics;
}

std::string DoubleBufferManager::GeneratePerformanceReport() const {
    auto analytics = GetPerformanceAnalytics();
    std::ostringstream report;

    report << "=== Double Buffer Manager Performance Report ===\n\n";

    report << "Overall Statistics:\n";
    report << "  Total Buffers: " << analytics["total_buffers"] << "\n";
    report << "  Total Cycles: " << analytics["total_cycles"] << "\n";
    report << "  Successful Cycles: " << analytics["successful_cycles"] << "\n";
    report << "  Overall Success Rate: " << (analytics["overall_success_rate"].get<double>() * 100) << "%\n";
    report << "  Total Bytes Transferred: " << analytics["total_bytes_transferred"] << "\n\n";

    report << "Performance Metrics:\n";
    if (analytics.contains("average_fill_time_us")) {
        report << "  Average Fill Time: " << analytics["average_fill_time_us"] << " μs\n";
        report << "  Average Process Time: " << analytics["average_process_time_us"] << " μs\n";
        report << "  Average Bandwidth: " << analytics["average_bandwidth_gb_per_sec"] << " GB/s\n";
    }

    if (analytics["pipeline_running"] == true) {
        report << "  Pipeline Throughput: " << analytics["pipeline_throughput_bytes_per_sec"] << " bytes/sec\n";
        report << "  Pipeline Progress: " << analytics["pipeline_data_processed"] << " / " << analytics["pipeline_total_data"] << " bytes\n";
    }

    report << "\nConfiguration:\n";
    report << "  Buffer Strategy: " << analytics["config"]["buffer_strategy"] << "\n";
    report << "  Buffer Size: " << analytics["config"]["buffer_size"] << " bytes\n";
    report << "  Number of Buffers: " << analytics["config"]["num_buffers"] << "\n";
    report << "  Pinned Memory: " << (analytics["config"]["pinned_memory"] ? "Enabled" : "Disabled") << "\n";
    report << "  Adaptive Buffering: " << (analytics["config"]["adaptive_buffering"] ? "Enabled" : "Disabled") << "\n";

    if (analytics["memory_mapping_enabled"] == true) {
        report << "  Memory Mapping: Enabled\n";
        report << "  Mapped File: " << analytics["mapped_file_path"] << "\n";
        report << "  Mapped Size: " << analytics["mapped_file_size"] << " bytes\n";
    }

    return report.str();
}

void DoubleBufferManager::UpdateConfiguration(const ManagerConfig& config) {
    config_ = config;
    current_strategy_ = config.strategy;
    adaptive_buffering_enabled_ = config.enable_auto_optimization;

    if (initialized_) {
        ReconfigureBuffers(config.default_buffer_size, config.default_num_buffers);
    }
}

DoubleBufferManager::ManagerConfig DoubleBufferManager::GetCurrentConfiguration() const {
    return config_;
}

DoubleBufferManager::ErrorType DoubleBufferManager::GetLastError() const {
    return last_error_;
}

std::string DoubleBufferManager::GetErrorMessage() const {
    return last_error_message_;
}

bool DoubleBufferManager::AttemptErrorRecovery() {
    return RecoverFromError(last_error_);
}

void DoubleBufferManager::SetErrorCallback(std::function<void(ErrorType, const std::string&)> callback) {
    error_callback_ = callback;
}

size_t DoubleBufferManager::GetTotalMemoryUsage() const {
    if (!initialized_) return 0;

    size_t total_usage = sizeof(*this);
    total_usage += buffers_.size() * (config_.default_buffer_size * 2); // host + device
    total_usage += buffer_metrics_.size() * sizeof(BufferMetrics);

    if (memory_mapping_enabled_) {
        total_usage += mapped_file_size_;
    }

    return total_usage;
}

size_t DoubleBufferManager::GetAllocatedMemoryUsage() const {
    if (!initialized_) return 0;

    return buffers_.size() * (config_.default_buffer_size * 2); // host + device
}

int DoubleBufferManager::GetActiveBufferCount() const {
    return buffers_.size();
}

double DoubleBufferManager::GetUtilizationRate() const {
    if (!initialized_) return 0.0;

    int total_buffers = buffers_.size();
    if (total_buffers == 0) return 0.0;

    int active_buffers = 0;
    for (const auto& [id, buffer] : buffers_) {
        if (buffer.state == BufferState::FILLING ||
            buffer.state == BufferState::PROCESSING ||
            buffer.state == BufferState::READY) {
            active_buffers++;
        }
    }

    return static_cast<double>(active_buffers) / total_buffers;
}

void DoubleBufferManager::ResetMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    for (auto& [id, metrics] : buffer_metrics_) {
        metrics.total_cycles = 0;
        metrics.successful_cycles = 0;
        metrics.failed_cycles = 0;
        metrics.success_rate = 1.0;
        metrics.total_bytes_transferred = 0;
        metrics.peak_memory_usage = 0;
        metrics.buffer_switches = 0;
        metrics.last_updated = std::chrono::system_clock::now();
    }
}

// Private methods

bool DoubleBufferManager::AllocateBuffers(size_t buffer_size, int num_buffers) {
    try {
        for (int i = 0; i < num_buffers; ++i) {
            int buffer_id = GetNextBufferId();

            BufferConfig buffer{};
            buffer.buffer_id = buffer_id;
            buffer.buffer_size = buffer_size;
            buffer.state = BufferState::EMPTY;
            buffer.sequence_number = i;
            buffer.name = "buffer_" + std::to_string(buffer_id);
            buffer.is_pinned = config_.enable_pinned_memory;
            buffer.actual_data_size = 0;

            // Allocate host memory
            buffer.host_ptr = AllocateHostMemory(buffer_size, config_.enable_pinned_memory);
            if (!buffer.host_ptr) {
                throw std::runtime_error("Failed to allocate host memory for buffer " + std::to_string(i));
            }

            // Allocate device memory
            buffer.device_ptr = AllocateDeviceMemory(buffer_size);
            if (!buffer.device_ptr) {
                throw std::runtime_error("Failed to allocate device memory for buffer " + std::to_string(i));
            }

            // Create CUDA stream and events
            if (cudaStreamCreate(&buffer.stream) != cudaSuccess) {
                throw std::runtime_error("Failed to create CUDA stream for buffer " + std::to_string(i));
            }

            if (cudaEventCreate(&buffer.ready_event) != cudaSuccess ||
                cudaEventCreate(&buffer.completed_event) != cudaSuccess) {
                throw std::runtime_error("Failed to create CUDA events for buffer " + std::to_string(i));
            }

            buffers_[buffer_id] = buffer;
            available_buffers_.push(buffer_id);
        }

        return true;

    } catch (const std::exception& e) {
        SetError(ErrorType::ALLOCATION_FAILED, "Buffer allocation failed: " + std::string(e.what()));
        DeallocateBuffers();
        return false;
    }
}

void DoubleBufferManager::DeallocateBuffers() {
    for (auto& [id, buffer] : buffers_) {
        if (buffer.host_ptr) {
            DeallocateHostMemory(buffer.host_ptr, buffer.is_pinned);
        }
        if (buffer.device_ptr) {
            DeallocateDeviceMemory(buffer.device_ptr);
        }
        if (buffer.stream) {
            cudaStreamDestroy(buffer.stream);
        }
        if (buffer.ready_event) {
            cudaEventDestroy(buffer.ready_event);
        }
        if (buffer.completed_event) {
            cudaEventDestroy(buffer.completed_event);
        }
    }

    buffers_.clear();

    // Clear queues
    while (!available_buffers_.empty()) available_buffers_.pop();
    while (!ready_buffers_.empty()) ready_buffers_.pop();
    while (!processing_buffers_.empty()) processing_buffers_.pop();
    while (!completed_buffers_.empty()) completed_buffers_.pop();
}

int DoubleBufferManager::GetNextBufferId() {
    static int next_id = 0;
    return next_id++;
}

bool DoubleBufferManager::IsValidBufferId(int buffer_id) const {
    return buffers_.find(buffer_id) != buffers_.end();
}

void DoubleBufferManager::UpdateBufferState(int buffer_id, BufferState new_state) {
    auto it = buffers_.find(buffer_id);
    if (it == buffers_.end()) return;

    BufferState old_state = it->second.state;
    it->second.state = new_state;

    // Update queues based on state transition
    switch (old_state) {
        case BufferState::EMPTY:
            if (std::find(available_buffers_.begin(), available_buffers_.end(), buffer_id) != available_buffers_.end()) {
                std::queue<int> new_queue;
                while (!available_buffers_.empty()) {
                    if (available_buffers_.front() != buffer_id) {
                        new_queue.push(available_buffers_.front());
                    }
                    available_buffers_.pop();
                }
                available_buffers_ = new_queue;
            }
            break;
        case BufferState::FILLING:
            break;
        case BufferState::READY:
            if (std::find(ready_buffers_.begin(), ready_buffers_.end(), buffer_id) != ready_buffers_.end()) {
                std::queue<int> new_queue;
                while (!ready_buffers_.empty()) {
                    if (ready_buffers_.front() != buffer_id) {
                        new_queue.push(ready_buffers_.front());
                    }
                    ready_buffers_.pop();
                }
                ready_buffers_ = new_queue;
            }
            break;
        case BufferState::PROCESSING:
            break;
        case BufferState::COMPLETED:
            if (std::find(completed_buffers_.begin(), completed_buffers_.end(), buffer_id) != completed_buffers_.end()) {
                std::queue<int> new_queue;
                while (!completed_buffers_.empty()) {
                    if (completed_buffers_.front() != buffer_id) {
                        new_queue.push(completed_buffers_.front());
                    }
                    completed_buffers_.pop();
                }
                completed_buffers_ = new_queue;
            }
            break;
        case BufferState::ERROR:
            break;
    }

    // Add to new state queue
    switch (new_state) {
        case BufferState::EMPTY:
            available_buffers_.push(buffer_id);
            break;
        case BufferState::READY:
            ready_buffers_.push(buffer_id);
            break;
        case BufferState::COMPLETED:
            completed_buffers_.push(buffer_id);
            break;
        default:
            break;
    }
}

void* DoubleBufferManager::AllocateHostMemory(size_t size, bool pinned) {
    if (pinned) {
        void* ptr = nullptr;
        if (cudaHostAlloc(&ptr, size, cudaHostAllocDefault) != cudaSuccess) {
            return nullptr;
        }
        return ptr;
    } else {
        return malloc(size);
    }
}

void* DoubleBufferManager::AllocateDeviceMemory(size_t size) {
    void* ptr = nullptr;
    if (cudaMalloc(&ptr, size) != cudaSuccess) {
        return nullptr;
    }
    return ptr;
}

void DoubleBufferManager::DeallocateHostMemory(void* ptr, bool pinned) {
    if (pinned) {
        cudaFreeHost(ptr);
    } else {
        free(ptr);
    }
}

void DoubleBufferManager::DeallocateDeviceMemory(void* ptr) {
    cudaFree(ptr);
}

void DoubleBufferManager::UpdateBufferMetrics(int buffer_id, const std::string& operation, std::chrono::microseconds duration) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto it = buffer_metrics_.find(buffer_id);
    if (it != buffer_metrics_.end()) {
        auto& metrics = it->second;

        if (operation == "fill") {
            if (metrics.average_fill_time.count() == 0) {
                metrics.average_fill_time = duration;
            } else {
                // Simple moving average
                metrics.average_fill_time = std::chrono::microseconds(
                    (metrics.average_fill_time.count() * 3 + duration.count()) / 4
                );
            }
            metrics.total_fill_time += duration;
        } else if (operation == "process") {
            if (metrics.average_process_time.count() == 0) {
                metrics.average_process_time = duration;
            } else {
                metrics.average_process_time = std::chrono::microseconds(
                    (metrics.average_process_time.count() * 3 + duration.count()) / 4
                );
            }
            metrics.total_process_time += duration;
        }

        metrics.last_updated = std::chrono::system_clock::now();
    }
}

void DoubleBufferManager::SetError(ErrorType error, const std::string& message) {
    last_error_ = error;
    last_error_message_ = message;

    if (error_callback_) {
        error_callback_(error, message);
    }
}

void DoubleBufferManager::HandleBufferError(int buffer_id, const std::string& error) {
    SetError(ErrorType::PIPELINE_ERROR, "Buffer " + std::to_string(buffer_id) + " error: " + error);
}

bool DoubleBufferManager::RecoverFromError(ErrorType error) {
    switch (error) {
        case ErrorType::CUDA_ERROR:
            cudaGetLastError(); // Clear CUDA error
            return true;

        case ErrorType::SYNCHRONIZATION_ERROR:
            WaitForAllBuffers();
            return true;

        case ErrorType::ALLOCATION_FAILED:
            // Try to reduce buffer count
            if (buffers_.size() > 2) {
                ReconfigureBuffers(config_.default_buffer_size, buffers_.size() - 1);
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}

void DoubleBufferManager::PipelineThreadFunction(
    std::function<void(int, void*, size_t)> fill_func,
    std::function<void(int, void*, size_t)> process_func,
    size_t total_data_size,
    size_t chunk_size) {

    while (!shutdown_requested_ && pipeline_data_offset_ < total_data_size) {
        // Get buffer for filling
        int fill_buffer = GetNextBufferForFilling();
        if (fill_buffer < 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        // Calculate data size for this chunk
        size_t remaining_data = total_data_size - pipeline_data_offset_;
        size_t current_chunk_size = std::min(chunk_size, remaining_data);

        // Fill buffer
        auto fill_start = std::chrono::high_resolution_clock::now();
        fill_func(fill_buffer, GetMappedBuffer(fill_buffer), current_chunk_size);
        auto fill_end = std::chrono::high_resolution_clock::now();

        // Mark buffer ready
        MarkBufferReady(fill_buffer);

        // Get buffer for processing
        int process_buffer = GetNextBufferForProcessing();
        if (process_buffer >= 0) {
            // Process buffer asynchronously
            ProcessBufferAsync(process_buffer, process_func, [this, process_buffer]() {
                // Mark as completed and return to available pool
                MarkBufferCompleted(process_buffer);
                available_buffers_.push(process_buffer);
            });
        }

        // Update progress
        pipeline_data_offset_ += current_chunk_size;

        // Adaptive delay based on performance
        auto fill_duration = std::chrono::duration_cast<std::chrono::microseconds>(fill_end - fill_start);
        if (fill_duration > std::chrono::microseconds(10000)) { // > 10ms
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    pipeline_running_ = false;
}

bool DoubleBufferManager::SetupMemoryMapping(const std::string& file_path, size_t file_size) {
    try {
        int fd = open(file_path.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd == -1) {
            throw std::runtime_error("Failed to open file: " + file_path);
        }

        // Resize file if necessary
        if (ftruncate(fd, file_size) == -1) {
            close(fd);
            throw std::runtime_error("Failed to resize file: " + file_path);
        }

        // Map file
        void* mapped_ptr = mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);

        if (mapped_ptr == MAP_FAILED) {
            throw std::runtime_error("Failed to map file: " + file_path);
        }

        mapped_file_path_ = file_path;
        mapped_file_ptr_ = mapped_ptr;
        mapped_file_size_ = file_size;
        memory_mapping_enabled_ = true;

        return true;

    } catch (const std::exception& e) {
        SetError(ErrorType::MEMORY_MAPPING_ERROR, "Memory mapping failed: " + std::string(e.what()));
        return false;
    }
}

void DoubleBufferManager::CleanupMemoryMapping() {
    if (memory_mapping_enabled_ && mapped_file_ptr_) {
        munmap(mapped_file_ptr_, mapped_file_size_);
        mapped_file_ptr_ = nullptr;
        mapped_file_size_ = 0;
        memory_mapping_enabled_ = false;
    }
}

void DoubleBufferManager::OptimizeBufferConfiguration() {
    if (!adaptive_buffering_enabled_) return;

    // Simple optimization based on current performance
    auto metrics = GetAllBufferMetrics();
    if (metrics.empty()) return;

    // Calculate average success rate
    double avg_success_rate = 0.0;
    for (const auto& metric : metrics) {
        avg_success_rate += metric.success_rate;
    }
    avg_success_rate /= metrics.size();

    // If success rate is low, try to reduce buffer count
    if (avg_success_rate < 0.8 && buffers_.size() > 3) {
        ReconfigureBuffers(config_.default_buffer_size, buffers_.size() - 1);
    }
}

int DoubleBufferManager::CalculateOptimalBufferCount(size_t data_size, std::chrono::microseconds target_latency) {
    // Simplified calculation - in a real implementation this would be more sophisticated
    size_t buffer_size = config_.default_buffer_size;
    if (buffer_size == 0) return 2;

    int estimated_buffers = static_cast<int>(
        std::ceil(static_cast<double>(data_size) / buffer_size)
    );

    // Adjust for target latency
    if (target_latency.count() < 1000) { // < 1ms - need more buffers
        estimated_buffers = std::min(estimated_buffers * 2, MAX_BUFFERS);
    } else if (target_latency.count() > 10000) { // > 10ms - can use fewer buffers
        estimated_buffers = std::max(estimated_buffers / 2, 2);
    }

    return std::clamp(estimated_buffers, 2, MAX_BUFFERS);
}

// Factory function
std::unique_ptr<DoubleBufferManager> CreateDoubleBufferManager(
    BufferStrategy strategy,
    size_t buffer_size,
    int num_buffers,
    bool enable_pinned_memory) {

    return std::make_unique<DoubleBufferManager>(
        strategy, buffer_size, num_buffers, enable_pinned_memory
    );
}

} // namespace performance
} // namespace gpu
} // namespace keycuda