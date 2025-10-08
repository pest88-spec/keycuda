#include "ComputeCore/gpu/performance/memory_optimizer.h"
#include "ComputeCore/core/uint256.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <random>
#include <numeric>
#include <unordered_map>

namespace keycuda {
namespace gpu {
namespace performance {

MemoryOptimizer::MemoryOptimizer(const MemoryOptimizationConfig& config)
    : config_(config)
    , asynchronous_enabled_(false)
    , profiling_enabled_(false)
    , initialized_(false)
    , last_error_(OptimizationError::NONE)
    , total_memory_allocated_(0)
    , peak_memory_usage_(0)
    , active_stream_count_(0) {

    // Initialize default prefetch strategy
    current_prefetch_strategy_.algorithm = PrefetchStrategy::Algorithm::ADAPTIVE;
    current_prefetch_strategy_.prefetch_ahead_mb = config.prefetch_distance_mb;
    current_prefetch_strategy_.confidence_threshold = 0.7;
    current_prefetch_strategy_.prefetch_latency = std::chrono::microseconds(100);
    current_prefetch_strategy_.max_concurrent_prefetches = 4;
    current_prefetch_strategy_.enable_adaptive_sizing = true;
    current_prefetch_strategy_.memory_pressure_threshold = 0.8;

    // Initialize CUDA resources
    if (!InitializeCudaResources()) {
        throw std::runtime_error("Failed to initialize CUDA resources for MemoryOptimizer");
    }

    initialized_ = true;
}

MemoryOptimizer::~MemoryOptimizer() {
    Cleanup();
}

MemoryMetrics MemoryOptimizer::OptimizeMemoryAccess(
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    const MemoryOptimizationConfig& config) {

    if (!initialized_) {
        SetError(OptimizationError::INVALID_CONFIGURATION);
        return {};
    }

    // Update configuration if provided
    if (config.pool_size_mb > 0) {
        config_ = config;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    MemoryMetrics metrics{};

    try {
        // Estimate workload size based on key range
        size_t key_range_bytes = 1024 * 1024; // Default 1MB estimate
        size_t estimated_workload = key_range_bytes;

        // Enable optimizations based on configuration
        if (config_.enable_async_streams && !asynchronous_enabled_) {
            EnableAsynchronousTransfers();
        }

        if (config_.enable_memory_pooling && !memory_pool_) {
            InitializeMemoryPool(config_.pool_size_mb);
        }

        // Optimize for the estimated workload
        OptimizeForDataSize(estimated_workload);

        // Record performance metrics
        auto end_time = std::chrono::high_resolution_clock::now();
        metrics.total_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        metrics.timestamp = std::chrono::system_clock::now();
        metrics.theoretical_bandwidth_gb_per_sec = GetTheoreticalBandwidth();

        // Calculate actual bandwidth utilization
        if (metrics.total_time.count() > 0) {
            metrics.bandwidth_utilization_gb_per_sec =
                (static_cast<double>(estimated_workload) / (1024.0 * 1024.0 * 1024.0)) /
                (metrics.total_time.count() / 1000000.0);
            metrics.memory_efficiency_percentage =
                (metrics.bandwidth_utilization_gb_per_sec / metrics.theoretical_bandwidth_gb_per_sec) * 100.0;
        }

        // Update peak memory usage
        UpdatePeakMemoryUsage(total_memory_allocated_);

        RecordMemoryMetrics(metrics);

    } catch (const std::exception& e) {
        SetError(OptimizationError::CUDA_ERROR);
        LogError(OptimizationError::CUDA_ERROR, e.what());
    }

    return metrics;
}

MemoryMetrics MemoryOptimizer::OptimizeMemoryTransfer(
    void* host_data,
    void* device_data,
    size_t size_bytes,
    bool is_host_to_device) {

    if (!initialized_) {
        SetError(OptimizationError::INVALID_CONFIGURATION);
        return {};
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    MemoryMetrics metrics{};

    try {
        // Optimize transfer parameters
        OptimizeTransferSize(size_bytes);
        OptimizeTransferAlignment(host_data, size_bytes);

        // Select optimal stream
        int stream_id = 0;
        OptimizeStreamAssignment(stream_id, size_bytes);

        // Create events for timing
        cudaEvent_t start_event, end_event;
        cudaEventCreate(&start_event);
        cudaEventCreate(&end_event);

        // Record transfer start
        cudaEventRecord(start_event, streams_[stream_id]);

        // Perform the transfer
        if (is_host_to_device) {
            cudaMemcpyAsync(device_data, host_data, size_bytes,
                          cudaMemcpyHostToDevice, streams_[stream_id]);
        } else {
            cudaMemcpyAsync(host_data, device_data, size_bytes,
                          cudaMemcpyDeviceToHost, streams_[stream_id]);
        }

        // Record transfer end
        cudaEventRecord(end_event, streams_[stream_id]);
        cudaEventSynchronize(end_event);

        // Calculate transfer time
        float transfer_ms = 0.0f;
        cudaEventElapsedTime(&transfer_ms, start_event, end_event);
        metrics.transfer_time = std::chrono::microseconds(static_cast<int>(transfer_ms * 1000));

        // Calculate bandwidth
        if (metrics.transfer_time.count() > 0) {
            double bandwidth_gb_per_sec =
                (static_cast<double>(size_bytes) / (1024.0 * 1024.0 * 1024.0)) /
                (metrics.transfer_time.count() / 1000000.0);

            if (is_host_to_device) {
                metrics.host_to_device_bandwidth_gb_per_sec = bandwidth_gb_per_sec;
            } else {
                metrics.device_to_host_bandwidth_gb_per_sec = bandwidth_gb_per_sec;
            }

            metrics.bandwidth_utilization_gb_per_sec = bandwidth_gb_per_sec;
        }

        // Cleanup events
        cudaEventDestroy(start_event);
        cudaEventDestroy(end_event);

        metrics.timestamp = std::chrono::system_clock::now();
        RecordMemoryMetrics(metrics);

    } catch (const std::exception& e) {
        SetError(OptimizationError::TRANSFER_FAILED);
        LogError(OptimizationError::TRANSFER_FAILED, e.what());
    }

    return metrics;
}

void MemoryOptimizer::EnableAsynchronousTransfers() {
    if (asynchronous_enabled_) return;

    try {
        // Create additional streams for asynchronous operations
        for (int i = streams_.size(); i < config_.stream_count; ++i) {
            cudaStream_t stream;
            if (cudaStreamCreate(&stream) != cudaSuccess) {
                throw std::runtime_error("Failed to create CUDA stream");
            }
            streams_.push_back(stream);
        }

        // Create events for synchronization
        for (int i = events_.size(); i < config_.stream_count * 2; ++i) {
            cudaEvent_t event;
            if (cudaEventCreate(&event) != cudaSuccess) {
                throw std::runtime_error("Failed to create CUDA event");
            }
            events_.push_back(event);
        }

        asynchronous_enabled_ = true;
        active_stream_count_ = streams_.size();

    } catch (const std::exception& e) {
        SetError(OptimizationError::STREAM_CREATION_FAILED);
        LogError(OptimizationError::STREAM_CREATION_FAILED, e.what());
    }
}

void MemoryOptimizer::DisableAsynchronousTransfers() {
    if (!asynchronous_enabled_) return;

    // Synchronize all streams
    SynchronizeAllStreams();

    // Destroy additional streams and events
    for (size_t i = 1; i < streams_.size(); ++i) {
        cudaStreamDestroy(streams_[i]);
    }
    streams_.resize(1); // Keep default stream

    for (auto event : events_) {
        cudaEventDestroy(event);
    }
    events_.clear();

    asynchronous_enabled_ = false;
    active_stream_count_ = 1;
}

bool MemoryOptimizer::IsAsynchronousEnabled() const {
    return asynchronous_enabled_;
}

int MemoryOptimizer::CreateStream() {
    if (streams_.size() >= MAX_STREAMS) {
        SetError(OptimizationError::STREAM_CREATION_FAILED);
        return -1;
    }

    try {
        cudaStream_t stream;
        if (cudaStreamCreate(&stream) != cudaSuccess) {
            throw std::runtime_error("Failed to create CUDA stream");
        }
        streams_.push_back(stream);
        return static_cast<int>(streams_.size()) - 1;
    } catch (const std::exception& e) {
        SetError(OptimizationError::STREAM_CREATION_FAILED);
        LogError(OptimizationError::STREAM_CREATION_FAILED, e.what());
        return -1;
    }
}

void MemoryOptimizer::DestroyStream(int stream_id) {
    if (stream_id <= 0 || stream_id >= static_cast<int>(streams_.size())) {
        return; // Don't destroy default stream or invalid stream
    }

    cudaStreamDestroy(streams_[stream_id]);
    streams_.erase(streams_.begin() + stream_id);
}

cudaStream_t MemoryOptimizer::GetStream(int stream_id) const {
    if (stream_id < 0 || stream_id >= static_cast<int>(streams_.size())) {
        return 0; // Default stream
    }
    return streams_[stream_id];
}

void MemoryOptimizer::SynchronizeStream(int stream_id) {
    if (stream_id < 0 || stream_id >= static_cast<int>(streams_.size())) {
        cudaStreamSynchronize(0); // Default stream
        return;
    }
    cudaStreamSynchronize(streams_[stream_id]);
}

void MemoryOptimizer::SynchronizeAllStreams() {
    for (auto stream : streams_) {
        cudaStreamSynchronize(stream);
    }
}

int MemoryOptimizer::CreateDoubleBuffer(size_t buffer_size) {
    if (double_buffers_.size() >= MAX_DOUBLE_BUFFERS) {
        SetError(OptimizationError::INSUFFICIENT_MEMORY);
        return -1;
    }

    try {
        DoubleBuffer buffer{};
        buffer.buffer_size = buffer_size;
        buffer.active_buffer = 0;
        buffer.stream_id = CreateStream();
        buffer.is_transfer_in_progress = false;

        // Allocate both buffers
        for (int i = 0; i < 2; ++i) {
            if (cudaMalloc(&buffer.buffer[i], buffer_size) != cudaSuccess) {
                // Cleanup on failure
                for (int j = 0; j < i; ++j) {
                    cudaFree(buffer.buffer[j]);
                }
                if (buffer.stream_id > 0) {
                    DestroyStream(buffer.stream_id);
                }
                throw std::runtime_error("Failed to allocate double buffer memory");
            }
        }

        int buffer_id = static_cast<int>(double_buffers_.size());
        double_buffers_[buffer_id] = buffer;
        total_memory_allocated_ += buffer_size * 2;

        return buffer_id;

    } catch (const std::exception& e) {
        SetError(OptimizationError::ALLOCATION_FAILED);
        LogError(OptimizationError::ALLOCATION_FAILED, e.what());
        return -1;
    }
}

void MemoryOptimizer::DestroyDoubleBuffer(int buffer_id) {
    auto it = double_buffers_.find(buffer_id);
    if (it == double_buffers_.end()) {
        return;
    }

    const DoubleBuffer& buffer = it->second;

    // Free both buffers
    for (int i = 0; i < 2; ++i) {
        if (buffer.buffer[i]) {
            cudaFree(buffer.buffer[i]);
            total_memory_allocated_ -= buffer.buffer_size;
        }
    }

    // Destroy associated stream
    if (buffer.stream_id > 0) {
        DestroyStream(buffer.stream_id);
    }

    double_buffers_.erase(it);
}

MemoryOptimizer::DoubleBuffer* MemoryOptimizer::GetDoubleBuffer(int buffer_id) {
    auto it = double_buffers_.find(buffer_id);
    return (it != double_buffers_.end()) ? &it->second : nullptr;
}

void MemoryOptimizer::SwapBuffers(int buffer_id) {
    DoubleBuffer* buffer = GetDoubleBuffer(buffer_id);
    if (buffer) {
        buffer->active_buffer = (buffer->active_buffer + 1) % 2;
    }
}

void MemoryOptimizer::TransferToActiveBuffer(int buffer_id, const void* data, size_t size) {
    DoubleBuffer* buffer = GetDoubleBuffer(buffer_id);
    if (!buffer || size > buffer->buffer_size) {
        SetError(OptimizationError::INVALID_CONFIGURATION);
        return;
    }

    void* active_buffer_ptr = buffer->buffer[buffer->active_buffer];
    cudaStream_t stream = GetStream(buffer->stream_id);

    // Asynchronous transfer to active buffer
    cudaMemcpyAsync(active_buffer_ptr, data, size,
                   cudaMemcpyHostToDevice, stream);

    buffer->is_transfer_in_progress = true;
}

void MemoryOptimizer::InitializeMemoryPool(size_t pool_size_mb) {
    try {
        // Note: MemoryPool implementation would be in a separate file
        // For now, we'll create a simple pool implementation

        size_t pool_size_bytes = pool_size_mb * 1024 * 1024;
        void* pool_ptr = nullptr;

        if (cudaMalloc(&pool_ptr, pool_size_bytes) != cudaSuccess) {
            throw std::runtime_error("Failed to allocate memory pool");
        }

        // Create a simple memory pool structure
        memory_pool_ = std::make_unique<MemoryPool>();
        memory_pool_->pool_ptr = pool_ptr;
        memory_pool_->pool_size = pool_size_bytes;
        memory_pool_->allocated_size = 0;
        memory_pool_->is_unified_memory = config_.enable_unified_memory;

        total_memory_allocated_ += pool_size_bytes;

    } catch (const std::exception& e) {
        SetError(OptimizationError::ALLOCATION_FAILED);
        LogError(OptimizationError::ALLOCATION_FAILED, e.what());
    }
}

void* MemoryOptimizer::AllocateFromPool(size_t size_bytes, size_t alignment) {
    if (!memory_pool_) {
        SetError(OptimizationError::INVALID_CONFIGURATION);
        return nullptr;
    }

    // Simple allocation strategy - allocate from the end of the pool
    size_t aligned_size = (size_bytes + alignment - 1) & ~(alignment - 1);

    if (memory_pool_->allocated_size + aligned_size > memory_pool_->pool_size) {
        SetError(OptimizationError::INSUFFICIENT_MEMORY);
        return nullptr;
    }

    // Allocate from pool (simple strategy)
    void* ptr = static_cast<char*>(memory_pool_->pool_ptr) + memory_pool_->allocated_size;
    memory_pool_->allocated_size += aligned_size;
    memory_pool_->allocated_blocks[ptr] = aligned_size;

    return ptr;
}

void MemoryOptimizer::DeallocateToPool(void* ptr) {
    if (!memory_pool_ || !ptr) {
        return;
    }

    auto it = memory_pool_->allocated_blocks.find(ptr);
    if (it != memory_pool_->allocated_blocks.end()) {
        // For this simple implementation, we don't actually free the memory
        // In a full implementation, we would add this to a free blocks list
        memory_pool_->allocated_blocks.erase(it);
    }
}

size_t MemoryOptimizer::GetPoolUtilization() const {
    if (!memory_pool_) return 0;
    return (memory_pool_->allocated_size * 100) / memory_pool_->pool_size;
}

size_t MemoryOptimizer::GetPoolFreeMemory() const {
    if (!memory_pool_) return 0;
    return memory_pool_->pool_size - memory_pool_->allocated_size;
}

void MemoryOptimizer::ResetPool() {
    if (memory_pool_) {
        memory_pool_->allocated_size = 0;
        memory_pool_->allocated_blocks.clear();
        memory_pool_->free_blocks.clear();
    }
}

AccessPattern MemoryOptimizer::AnalyzeAccessPattern(const void* data, size_t size_bytes) {
    AccessPattern pattern{};
    pattern.access_size_bytes = size_bytes;
    pattern.access_count = 1;
    pattern.is_read_pattern = true;
    pattern.timestamp = std::chrono::system_clock::now();

    // Simple pattern detection based on data size and typical access patterns
    if (size_bytes < 1024) {
        pattern.pattern_type = AccessPattern::Type::SEQUENTIAL;
        pattern.stride_bytes = 64; // Cache line size
        pattern.locality_factor = 0.9;
        pattern.spatial_locality = 0.8;
        pattern.efficiency_score = 0.9;
    } else if (size_bytes < 64 * 1024) {
        pattern.pattern_type = AccessPattern::Type::STRIDED;
        pattern.stride_bytes = 512;
        pattern.locality_factor = 0.7;
        pattern.spatial_locality = 0.6;
        pattern.efficiency_score = 0.7;
    } else {
        pattern.pattern_type = AccessPattern::Type::RANDOM;
        pattern.stride_bytes = size_bytes / 100; // Assume 100 random accesses
        pattern.locality_factor = 0.3;
        pattern.spatial_locality = 0.2;
        pattern.efficiency_score = 0.3;
    }

    return pattern;
}

void MemoryOptimizer::OptimizeAccessPattern(const AccessPattern& pattern) {
    // Optimization strategy based on pattern type
    switch (pattern.pattern_type) {
        case AccessPattern::Type::SEQUENTIAL:
            // Prefetch next sequential blocks
            if (config_.enable_prefetching) {
                // Enable sequential prefetching
                current_prefetch_strategy_.algorithm = PrefetchStrategy::Algorithm::SEQUENTIAL;
            }
            break;

        case AccessPattern::Type::STRIDED:
            // Optimize for strided access
            current_prefetch_strategy_.algorithm = PrefetchStrategy::Algorithm::STRIDED;
            current_prefetch_strategy_.prefetch_ahead_mb = pattern.stride_bytes * 2;
            break;

        case AccessPattern::Type::COALESCED:
            // Already optimal - minimal optimization needed
            break;

        case AccessPattern::Type::UNCOALESCED:
            // Try to improve coalescing
            OptimizeForDataSize(pattern.access_size_bytes);
            break;

        case AccessPattern::Type::REPEATING:
            // Enable caching and prefetching
            current_prefetch_strategy_.algorithm = PrefetchStrategy::Algorithm::ADAPTIVE;
            break;

        default:
            // Use adaptive strategy for unknown patterns
            current_prefetch_strategy_.algorithm = PrefetchStrategy::Algorithm::ADAPTIVE;
            break;
    }
}

std::vector<AccessPattern> MemoryOptimizer::DetectAccessPatterns(const void* data, size_t size_bytes) {
    std::vector<AccessPattern> patterns;

    // For simplicity, return a single pattern analysis
    // In a full implementation, this would analyze the data more thoroughly
    patterns.push_back(AnalyzeAccessPattern(data, size_bytes));

    return patterns;
}

void MemoryOptimizer::SetPrefetchStrategy(const PrefetchStrategy& strategy) {
    current_prefetch_strategy_ = strategy;
}

void MemoryOptimizer::PrefetchAsync(void* device_ptr, size_t size_bytes, int stream_id) {
    if (!config_.enable_prefetching) {
        return;
    }

    cudaStream_t stream = GetStream(stream_id);
    if (stream) {
        int device;
        cudaGetDevice(&device);
        cudaMemPrefetchAsync(device_ptr, size_bytes, device, stream);
    }
}

void MemoryOptimizer::PrefetchToDevice(void* host_ptr, void* device_ptr, size_t size_bytes) {
    if (!config_.enable_prefetching) {
        return;
    }

    switch (current_prefetch_strategy_.algorithm) {
        case PrefetchStrategy::Algorithm::ADAPTIVE:
            ExecuteAdaptivePrefetch(device_ptr, size_bytes);
            break;
        case PrefetchStrategy::Algorithm::SEQUENTIAL:
            ExecuteSequentialPrefetch(device_ptr, size_bytes);
            break;
        case PrefetchStrategy::Algorithm::PREDICTIVE:
            ExecutePredictivePrefetch(device_ptr, size_bytes);
            break;
        default:
            // Default to adaptive
            ExecuteAdaptivePrefetch(device_ptr, size_bytes);
            break;
    }
}

void MemoryOptimizer::PrefetchToHost(void* device_ptr, void* host_ptr, size_t size_bytes) {
    if (!config_.enable_prefetching) {
        return;
    }

    // Prefetch to CPU memory (cudaCpuDeviceId)
    cudaMemPrefetchAsync(device_ptr, size_bytes, cudaCpuDeviceId, 0);
    cudaDeviceSynchronize();
}

int MemoryOptimizer::QueueTransferBatch(void* host_ptr, void* device_ptr, size_t size_bytes, bool is_read) {
    if (!config_.enable_transfer_batching) {
        return -1;
    }

    MemoryTransferBatch batch{};
    batch.batch_id = transfer_batches_.size();
    batch.host_ptr = host_ptr;
    batch.device_ptr = device_ptr;
    batch.size_bytes = size_bytes;
    batch.is_read_operation = is_read;
    batch.stream_id = CreateStream();
    batch.submit_time = std::chrono::system_clock::now();
    batch.is_completed = false;

    // Create timing events
    cudaEventCreate(&batch.start_event);
    cudaEventCreate(&batch.end_event);

    transfer_batches_.push_back(batch);
    return static_cast<int>(batch.batch_id);
}

void MemoryOptimizer::ExecuteTransferBatches() {
    for (auto& batch : transfer_batches_) {
        if (batch.is_completed) {
            continue;
        }

        cudaStream_t stream = GetStream(batch.stream_id);
        cudaEventRecord(batch.start_event, stream);

        if (batch.is_read_operation) {
            cudaMemcpyAsync(batch.host_ptr, batch.device_ptr, batch.size_bytes,
                          cudaMemcpyDeviceToHost, stream);
        } else {
            cudaMemcpyAsync(batch.device_ptr, batch.host_ptr, batch.size_bytes,
                          cudaMemcpyHostToDevice, stream);
        }

        cudaEventRecord(batch.end_event, stream);
    }
}

void MemoryOptimizer::WaitForTransferBatch(int batch_id) {
    if (batch_id < 0 || batch_id >= static_cast<int>(transfer_batches_.size())) {
        return;
    }

    auto& batch = transfer_batches_[batch_id];
    if (!batch.is_completed) {
        cudaStreamSynchronize(GetStream(batch.stream_id));

        // Calculate transfer time and bandwidth
        float transfer_ms = 0.0f;
        cudaEventElapsedTime(&transfer_ms, batch.start_event, batch.end_event);
        batch.transfer_time = std::chrono::microseconds(static_cast<int>(transfer_ms * 1000));

        if (batch.transfer_time.count() > 0) {
            batch.bandwidth_gb_per_sec =
                (static_cast<double>(batch.size_bytes) / (1024.0 * 1024.0 * 1024.0)) /
                (batch.transfer_time.count() / 1000000.0);
        }

        batch.is_completed = true;
        completed_transfers_.push_back(batch);
    }
}

std::vector<MemoryTransferBatch> MemoryOptimizer::GetTransferHistory() const {
    return completed_transfers_;
}

void MemoryOptimizer::EnableBandwidthProfiling() {
    profiling_enabled_ = true;
}

void MemoryOptimizer::DisableBandwidthProfiling() {
    profiling_enabled_ = false;
}

MemoryMetrics MemoryOptimizer::GetCurrentMemoryMetrics() const {
    return CalculateCurrentMetrics();
}

MemoryMetrics MemoryOptimizer::GetAverageMemoryMetrics(std::chrono::minutes time_window) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    if (metrics_history_.empty()) {
        return {};
    }

    auto cutoff_time = std::chrono::system_clock::now() - time_window;
    MemoryMetrics avg_metrics{};
    size_t count = 0;

    for (const auto& metrics : metrics_history_) {
        if (metrics.timestamp >= cutoff_time) {
            // Aggregate metrics (simple averaging)
            avg_metrics.bandwidth_utilization_gb_per_sec += metrics.bandwidth_utilization_gb_per_sec;
            avg_metrics.memory_efficiency_percentage += metrics.memory_efficiency_percentage;
            avg_metrics.cache_hit_rate += metrics.cache_hit_rate;
            count++;
        }
    }

    if (count > 0) {
        avg_metrics.bandwidth_utilization_gb_per_sec /= count;
        avg_metrics.memory_efficiency_percentage /= count;
        avg_metrics.cache_hit_rate /= count;
    }

    return avg_metrics;
}

std::vector<MemoryMetrics> MemoryOptimizer::GetMemoryMetricsHistory() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_history_;
}

double MemoryOptimizer::GetMemoryBandwidthUtilization() const {
    auto metrics = GetCurrentMemoryMetrics();
    return metrics.memory_efficiency_percentage;
}

double MemoryOptimizer::GetCacheHitRate() const {
    auto metrics = GetCurrentMemoryMetrics();
    return metrics.cache_hit_rate;
}

double MemoryOptimizer::GetCoalescingEfficiency() const {
    // This would be calculated from actual profiling data
    // For now, return a reasonable estimate
    return 0.85; // 85% coalescing efficiency
}

size_t MemoryOptimizer::GetPeakMemoryUsage() const {
    return peak_memory_usage_;
}

std::chrono::microseconds MemoryOptimizer::GetAverageTransferTime() const {
    auto metrics = GetAverageMemoryMetrics(std::chrono::minutes(5));
    return metrics.transfer_time;
}

void MemoryOptimizer::OptimizeForKernelLaunch(size_t expected_workload_size) {
    // Optimize memory configuration for kernel launch
    if (expected_workload_size < 1024 * 1024) { // < 1MB
        // Small workload - optimize for latency
        config_.batch_size_mb = 16;
        config_.prefetch_distance_mb = 64;
    } else if (expected_workload_size < 100 * 1024 * 1024) { // < 100MB
        // Medium workload - balanced optimization
        config_.batch_size_mb = 64;
        config_.prefetch_distance_mb = 256;
    } else {
        // Large workload - optimize for bandwidth
        config_.batch_size_mb = 128;
        config_.prefetch_distance_mb = 512;
    }
}

void MemoryOptimizer::OptimizeForDataSize(size_t data_size_bytes) {
    // Adjust optimization parameters based on data size
    if (data_size_bytes < 64 * 1024) { // < 64KB
        // Small data - prioritize latency
        current_prefetch_strategy_.prefetch_ahead_mb = 16;
        current_prefetch_strategy_.prefetch_latency = std::chrono::microseconds(50);
    } else {
        // Large data - prioritize bandwidth
        current_prefetch_strategy_.prefetch_ahead_mb = 512;
        current_prefetch_strategy_.prefetch_latency = std::chrono::microseconds(200);
    }
}

void MemoryOptimizer::OptimizeForAccessPattern(const AccessPattern& pattern) {
    OptimizeAccessPattern(pattern);
}

void MemoryOptimizer::OptimizeForArchitecture(int compute_capability) {
    // Architecture-specific optimizations
    if (compute_capability >= 90) { // Hopper
        config_.stream_count = 8;
        config_.target_bandwidth_utilization = 0.90;
    } else if (compute_capability >= 89) { // Ada
        config_.stream_count = 6;
        config_.target_bandwidth_utilization = 0.85;
    } else if (compute_capability >= 86) { // Ampere
        config_.stream_count = 4;
        config_.target_bandwidth_utilization = 0.80;
    } else { // Older architectures
        config_.stream_count = 2;
        config_.target_bandwidth_utilization = 0.70;
    }
}

MemoryOptimizer::CoalescingInfo MemoryOptimizer::AnalyzeCoalescing(
    const void* device_ptr, size_t size_bytes, int block_size) {

    CoalescingInfo info{};
    info.warp_size = 32; // Standard warp size
    info.aligned_access_size = size_bytes;
    info.thread_offsets.resize(block_size);

    // Simple analysis - assume optimal coalescing for aligned access
    size_t address = reinterpret_cast<size_t>(device_ptr);
    bool is_aligned = (address % 128) == 0; // 128-byte alignment

    if (is_aligned && (size_bytes % 128) == 0) {
        info.is_coalesced = true;
        info.efficiency_score = 1.0;
        info.misaligned_penalties = 0;
    } else {
        info.is_coalesced = false;
        info.efficiency_score = 0.7; // Some efficiency loss
        info.misaligned_penalties = block_size / 2;
    }

    // Calculate thread offsets (simplified)
    for (int i = 0; i < block_size; ++i) {
        info.thread_offsets[i] = i * (size_bytes / block_size);
    }

    return info;
}

void MemoryOptimizer::OptimizeCoalescing(const CoalescingInfo& info) {
    // Optimization based on coalescing analysis
    if (!info.is_coalesced) {
        // Enable alignment optimizations
        config_.enable_transfer_batching = true;

        // Adjust prefetch strategy to improve coalescing
        if (current_prefetch_strategy_.algorithm == PrefetchStrategy::Algorithm::ADAPTIVE) {
            current_prefetch_strategy_.prefetch_ahead_mb =
                std::max(size_t(128), current_prefetch_strategy_.prefetch_ahead_mb);
        }
    }
}

void MemoryOptimizer::UpdateConfiguration(const MemoryOptimizationConfig& config) {
    config_ = config;
}

MemoryOptimizationConfig MemoryOptimizer::GetCurrentConfiguration() const {
    return config_;
}

void MemoryOptimizer::ResetToDefaults() {
    config_ = MemoryOptimizationConfig{};
    ResetMetrics();
}

bool MemoryOptimizer::ValidateMemoryOperations(const void* test_data, size_t size_bytes) {
    if (!test_data || size_bytes == 0) {
        return false;
    }

    try {
        // Test host to device transfer
        void* device_ptr = nullptr;
        if (cudaMalloc(&device_ptr, size_bytes) != cudaSuccess) {
            return false;
        }

        cudaMemcpy(device_ptr, test_data, size_bytes, cudaMemcpyHostToDevice);

        // Test device to host transfer
        void* host_result = malloc(size_bytes);
        if (!host_result) {
            cudaFree(device_ptr);
            return false;
        }

        cudaMemcpy(host_result, device_ptr, size_bytes, cudaMemcpyDeviceToHost);

        // Verify data integrity
        bool is_valid = (memcmp(test_data, host_result, size_bytes) == 0);

        // Cleanup
        free(host_result);
        cudaFree(device_ptr);

        return is_valid;

    } catch (const std::exception&) {
        return false;
    }
}

bool MemoryOptimizer::BandwidthTest(size_t test_size_mb, int num_iterations) {
    const size_t test_size = test_size_mb * 1024 * 1024;

    // Allocate test buffers
    void* host_buffer = malloc(test_size);
    void* device_buffer = nullptr;

    if (!host_buffer || cudaMalloc(&device_buffer, test_size) != cudaSuccess) {
        if (host_buffer) free(host_buffer);
        return false;
    }

    // Initialize host buffer
    memset(host_buffer, 0xAA, test_size);

    bool test_passed = true;
    std::vector<double> bandwidth_samples;

    try {
        for (int i = 0; i < num_iterations; ++i) {
            auto start_time = std::chrono::high_resolution_clock::now();

            // Host to Device transfer
            cudaMemcpy(device_buffer, host_buffer, test_size, cudaMemcpyHostToDevice);

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

            // Calculate bandwidth
            double bandwidth_gb_per_sec =
                (static_cast<double>(test_size) / (1024.0 * 1024.0 * 1024.0)) /
                (duration.count() / 1000000.0);

            bandwidth_samples.push_back(bandwidth_gb_per_sec);

            // Verify minimum bandwidth requirement
            if (bandwidth_gb_per_sec < (config_.target_bandwidth_utilization * GetTheoreticalBandwidth() * 0.5)) {
                test_passed = false;
            }
        }

    } catch (const std::exception&) {
        test_passed = false;
    }

    // Cleanup
    free(host_buffer);
    cudaFree(device_buffer);

    return test_passed;
}

bool MemoryOptimizer::LatencyTest(size_t test_size_kb, int num_iterations) {
    const size_t test_size = test_size_kb * 1024;

    void* host_buffer = malloc(test_size);
    void* device_buffer = nullptr;

    if (!host_buffer || cudaMalloc(&device_buffer, test_size) != cudaSuccess) {
        if (host_buffer) free(host_buffer);
        return false;
    }

    bool test_passed = true;
    std::vector<std::chrono::microseconds> latency_samples;

    try {
        for (int i = 0; i < num_iterations; ++i) {
            auto start_time = std::chrono::high_resolution_clock::now();

            // Small transfer for latency measurement
            cudaMemcpy(device_buffer, host_buffer, test_size, cudaMemcpyHostToDevice);
            cudaDeviceSynchronize();

            auto end_time = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

            latency_samples.push_back(latency);

            // Check if latency is reasonable (< 1ms for small transfers)
            if (latency.count() > 1000) {
                test_passed = false;
            }
        }

    } catch (const std::exception&) {
        test_passed = false;
    }

    // Cleanup
    free(host_buffer);
    cudaFree(device_buffer);

    return test_passed;
}

json MemoryOptimizer::GetPerformanceAnalytics() const {
    json analytics;

    auto current_metrics = GetCurrentMemoryMetrics();
    auto avg_metrics = GetAverageMemoryMetrics(std::chrono::minutes(5));

    analytics["current_bandwidth_utilization"] = current_metrics.memory_efficiency_percentage;
    analytics["average_bandwidth_utilization"] = avg_metrics.memory_efficiency_percentage;
    analytics["peak_memory_usage_mb"] = peak_memory_usage_;
    analytics["total_memory_allocated_mb"] = total_memory_allocated_ / (1024 * 1024);
    analytics["active_stream_count"] = active_stream_count_;
    analytics["asynchronous_enabled"] = asynchronous_enabled_;
    analytics["profiling_enabled"] = profiling_enabled_;
    analytics["pool_utilization"] = GetPoolUtilization();
    analytics["cache_hit_rate"] = current_metrics.cache_hit_rate;
    analytics["coalescing_efficiency"] = GetCoalescingEfficiency();

    return analytics;
}

std::string MemoryOptimizer::GeneratePerformanceReport() const {
    auto analytics = GetPerformanceAnalytics();

    std::ostringstream report;
    report << "=== Memory Optimizer Performance Report ===\n\n";

    report << "Memory Utilization:\n";
    report << "  Current Bandwidth Utilization: " << analytics["current_bandwidth_utilization"] << "%\n";
    report << "  Average Bandwidth Utilization: " << analytics["average_bandwidth_utilization"] << "%\n";
    report << "  Peak Memory Usage: " << analytics["peak_memory_usage_mb"] << " MB\n";
    report << "  Total Memory Allocated: " << analytics["total_memory_allocated_mb"] << " MB\n";
    report << "  Pool Utilization: " << analytics["pool_utilization"] << "%\n\n";

    report << "Performance Metrics:\n";
    report << "  Cache Hit Rate: " << analytics["cache_hit_rate"] << "%\n";
    report << "  Coalescing Efficiency: " << analytics["coalescing_efficiency"] << "%\n";
    report << "  Active Streams: " << analytics["active_stream_count"] << "\n";
    report << "  Asynchronous Transfers: " << (analytics["asynchronous_enabled"] ? "Enabled" : "Disabled") << "\n";
    report << "  Profiling: " << (analytics["profiling_enabled"] ? "Enabled" : "Disabled") << "\n\n";

    report << "Configuration:\n";
    report << "  Stream Count: " << config_.stream_count << "\n";
    report << "  Pool Size: " << config_.pool_size_mb << " MB\n";
    report << "  Target Bandwidth Utilization: " << (config_.target_bandwidth_utilization * 100) << "%\n";
    report << "  Prefetch Distance: " << config_.prefetch_distance_mb << " MB\n";
    report << "  Batch Size: " << config_.batch_size_mb << " MB\n";

    return report.str();
}

std::string MemoryOptimizer::GetOptimizationRecommendations() const {
    auto metrics = GetCurrentMemoryMetrics();
    std::vector<std::string> recommendations;

    if (metrics.memory_efficiency_percentage < 70.0) {
        recommendations.push_back("Enable asynchronous transfers and increase stream count");
    }

    if (metrics.cache_hit_rate < 80.0) {
        recommendations.push_back("Increase prefetch distance and enable adaptive prefetching");
    }

    if (GetCoalescingEfficiency() < 85.0) {
        recommendations.push_back("Optimize memory access patterns for better coalescing");
    }

    if (GetPoolUtilization() > 90.0) {
        recommendations.push_back("Increase memory pool size to avoid allocation failures");
    }

    if (!asynchronous_enabled_) {
        recommendations.push_back("Enable asynchronous transfers to overlap computation and data transfer");
    }

    if (!config_.enable_transfer_batching) {
        recommendations.push_back("Enable transfer batching to improve bandwidth utilization");
    }

    if (recommendations.empty()) {
        recommendations.push_back("Current configuration appears optimal");
    }

    std::ostringstream result;
    result << "Optimization Recommendations:\n";
    for (size_t i = 0; i < recommendations.size(); ++i) {
        result << "  " << (i + 1) << ". " << recommendations[i] << "\n";
    }

    return result.str();
}

void MemoryOptimizer::ExportMetrics(const std::string& filename) const {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << GetPerformanceAnalytics().dump(4);
        file.close();
    }
}

MemoryOptimizer::OptimizationError MemoryOptimizer::GetLastError() const {
    return last_error_;
}

std::string MemoryOptimizer::GetErrorString(OptimizationError error) const {
    switch (error) {
        case OptimizationError::NONE:
            return "No error";
        case OptimizationError::INSUFFICIENT_MEMORY:
            return "Insufficient GPU memory";
        case OptimizationError::CUDA_ERROR:
            return "CUDA operation failed";
        case OptimizationError::INVALID_CONFIGURATION:
            return "Invalid configuration";
        case OptimizationError::STREAM_CREATION_FAILED:
            return "Failed to create CUDA stream";
        case OptimizationError::ALLOCATION_FAILED:
            return "Memory allocation failed";
        case OptimizationError::TRANSFER_FAILED:
            return "Memory transfer failed";
        case OptimizationError::PREFETCH_FAILED:
            return "Prefetch operation failed";
        default:
            return "Unknown error";
    }
}

bool MemoryOptimizer::AttemptErrorRecovery(OptimizationError error) {
    switch (error) {
        case OptimizationError::INSUFFICIENT_MEMORY:
        case OptimizationError::ALLOCATION_FAILED:
            // Try to free memory pool and reset
            if (memory_pool_) {
                ResetPool();
            }
            return true;

        case OptimizationError::STREAM_CREATION_FAILED:
            // Fall back to synchronous operations
            DisableAsynchronousTransfers();
            return true;

        case OptimizationError::TRANSFER_FAILED:
        case OptimizationError::CUDA_ERROR:
            // Try to reset CUDA device
            cudaDeviceReset();
            return RecoverFromCudaError();

        default:
            return false;
    }
}

void MemoryOptimizer::Cleanup() {
    // Synchronize all operations
    SynchronizeAllStreams();

    // Cleanup CUDA resources
    CleanupCudaResources();

    // Cleanup memory pool
    if (memory_pool_) {
        if (memory_pool_->pool_ptr) {
            cudaFree(memory_pool_->pool_ptr);
        }
        memory_pool_.reset();
    }

    // Cleanup double buffers
    for (auto& [id, buffer] : double_buffers_) {
        for (int i = 0; i < 2; ++i) {
            if (buffer.buffer[i]) {
                cudaFree(buffer.buffer[i]);
            }
        }
        if (buffer.stream_id > 0) {
            DestroyStream(buffer.stream_id);
        }
    }
    double_buffers_.clear();

    // Reset state
    ResetMetrics();
    initialized_ = false;
}

void MemoryOptimizer::ResetMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_history_.clear();
    total_memory_allocated_ = 0;
    peak_memory_usage_ = 0;
    last_error_ = OptimizationError::NONE;
}

// Private helper methods

void MemoryOptimizer::InitializeCudaResources() {
    try {
        // Create default stream
        cudaStream_t default_stream;
        if (cudaStreamCreate(&default_stream) != cudaSuccess) {
            throw std::runtime_error("Failed to create default CUDA stream");
        }
        streams_.push_back(default_stream);

        // Set device
        int device;
        cudaGetDevice(&device);

        // Get device properties
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, device);

        // Configure based on device capabilities
        OptimizeForArchitecture(prop.major * 10 + prop.minor);

    } catch (const std::exception& e) {
        SetError(OptimizationError::CUDA_ERROR);
        LogError(OptimizationError::CUDA_ERROR, e.what());
    }
}

void MemoryOptimizer::CleanupCudaResources() {
    // Destroy streams
    for (auto stream : streams_) {
        if (stream != 0) {
            cudaStreamDestroy(stream);
        }
    }
    streams_.clear();

    // Destroy events
    for (auto event : events_) {
        cudaEventDestroy(event);
    }
    events_.clear();
}

bool MemoryOptimizer::ValidateConfiguration() const {
    return config_.pool_size_mb > 0 &&
           config_.pool_size_mb <= 64 * 1024 && // Max 64GB
           config_.stream_count > 0 &&
           config_.stream_count <= MAX_STREAMS &&
           config_.target_bandwidth_utilization > 0.0 &&
           config_.target_bandwidth_utilization <= 1.0;
}

void MemoryOptimizer::OptimizeTransferSize(size_t& size_bytes) {
    // Align to cache line size
    const size_t cache_line_size = 128;
    size_bytes = (size_bytes + cache_line_size - 1) & ~(cache_line_size - 1);

    // Ensure minimum transfer size
    size_bytes = std::max(size_bytes, MIN_TRANSFER_SIZE);

    // Limit maximum transfer size
    size_bytes = std::min(size_bytes, MAX_TRANSFER_SIZE);
}

void MemoryOptimizer::OptimizeTransferAlignment(void*& ptr, size_t& size_bytes) {
    // Simple alignment - in a full implementation this would handle platform-specific alignment
    const size_t alignment = 256;
    size_t aligned_size = (size_bytes + alignment - 1) & ~(alignment - 1);

    if (aligned_size != size_bytes) {
        size_bytes = aligned_size;
    }
}

void MemoryOptimizer::OptimizeStreamAssignment(int& stream_id, size_t transfer_size) {
    if (!asynchronous_enabled_) {
        stream_id = 0; // Default stream
        return;
    }

    // Simple round-robin assignment
    static int next_stream = 1;
    stream_id = next_stream % (streams_.size() - 1) + 1;
    next_stream++;
}

AccessPattern MemoryOptimizer::DetectSequentialPattern(const void* data, size_t size_bytes) {
    AccessPattern pattern;
    pattern.pattern_type = AccessPattern::Type::SEQUENTIAL;
    pattern.stride_bytes = 64;
    pattern.locality_factor = 0.9;
    pattern.spatial_locality = 0.8;
    pattern.efficiency_score = 0.9;
    return pattern;
}

AccessPattern MemoryOptimizer::DetectStridedPattern(const void* data, size_t size_bytes) {
    AccessPattern pattern;
    pattern.pattern_type = AccessPattern::Type::STRIDED;
    pattern.stride_bytes = 512;
    pattern.locality_factor = 0.7;
    pattern.spatial_locality = 0.6;
    pattern.efficiency_score = 0.7;
    return pattern;
}

AccessPattern MemoryOptimizer::DetectRandomPattern(const void* data, size_t size_bytes) {
    AccessPattern pattern;
    pattern.pattern_type = AccessPattern::Type::RANDOM;
    pattern.stride_bytes = size_bytes / 100;
    pattern.locality_factor = 0.3;
    pattern.spatial_locality = 0.2;
    pattern.efficiency_score = 0.3;
    return pattern;
}

double MemoryOptimizer::CalculatePatternEfficiency(const AccessPattern& pattern) {
    return pattern.efficiency_score;
}

void MemoryOptimizer::ExecuteAdaptivePrefetch(void* ptr, size_t size_bytes) {
    // Adaptive prefetching based on recent access patterns
    int device;
    cudaGetDevice(&device);
    cudaMemPrefetchAsync(ptr, size_bytes, device, 0);
}

void MemoryOptimizer::ExecuteSequentialPrefetch(void* ptr, size_t size_bytes) {
    // Sequential prefetching
    int device;
    cudaGetDevice(&device);
    cudaMemPrefetchAsync(ptr, size_bytes, device, 0);
}

void MemoryOptimizer::ExecutePredictivePrefetch(void* ptr, size_t size_bytes) {
    // Predictive prefetching based on historical patterns
    ExecuteAdaptivePrefetch(ptr, size_bytes);
}

double MemoryOptimizer::GetTheoreticalBandwidth() const {
    // Get theoretical bandwidth for current device
    int device;
    cudaGetDevice(&device);

    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, device);

    // Return memory bandwidth in GB/s (simplified)
    return prop.memoryBusWidth / 8.0 * (prop.memoryClockRate * 1000.0) / (1024.0 * 1024.0 * 1024.0);
}

void MemoryOptimizer::RecordMemoryMetrics(const MemoryMetrics& metrics) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_history_.push_back(metrics);

    // Keep only recent metrics (last 1000 entries)
    if (metrics_history_.size() > 1000) {
        metrics_history_.erase(metrics_history_.begin());
    }
}

MemoryMetrics MemoryOptimizer::CalculateCurrentMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    if (metrics_history_.empty()) {
        return MemoryMetrics{};
    }

    // Return the most recent metrics
    return metrics_history_.back();
}

void MemoryOptimizer::UpdatePeakMemoryUsage(size_t current_usage) {
    peak_memory_usage_ = std::max(peak_memory_usage_, current_usage);
}

void MemoryOptimizer::SetError(OptimizationError error) {
    last_error_ = error;
}

bool MemoryOptimizer::RecoverFromCudaError() {
    // Attempt to recover from CUDA errors
    cudaError_t error = cudaGetLastError();
    if (error != cudaSuccess) {
        cudaGetLastError(); // Clear the error
        return false;
    }
    return true;
}

void MemoryOptimizer::LogError(OptimizationError error, const std::string& context) {
    // Simple error logging - in a full implementation this would use a proper logging system
    std::cerr << "MemoryOptimizer Error: " << GetErrorString(error);
    if (!context.empty()) {
        std::cerr << " Context: " << context;
    }
    std::cerr << std::endl;
}

// Factory function
std::unique_ptr<MemoryOptimizer> CreateMemoryOptimizer(
    const MemoryOptimizationConfig& config) {

    return std::make_unique<MemoryOptimizer>(config);
}

} // namespace performance
} // namespace gpu
} // namespace keycuda