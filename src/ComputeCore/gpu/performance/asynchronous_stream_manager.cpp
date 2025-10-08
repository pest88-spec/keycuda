#include "ComputeCore/gpu/performance/asynchronous_stream_manager.h"
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <condition_variable>
#include <unordered_set>
#include <iostream>
#include <sstream>

namespace keycuda {
namespace gpu {
namespace performance {

AsynchronousStreamManager::AsynchronousStreamManager(
    int num_streams,
    bool enable_advanced_scheduling,
    bool enable_performance_monitoring)

    : initialized_(false)
    , load_balancing_enabled_(enable_advanced_scheduling)
    , dynamic_priority_enabled_(enable_advanced_scheduling)
    , last_error_(ErrorType::NONE)
    , shutdown_requested_(false) {

    // Initialize configuration
    config_.max_streams = std::min(num_streams, MAX_STREAMS);
    config_.enable_advanced_scheduling = enable_advanced_scheduling;
    config_.enable_profiling = enable_performance_monitoring;
    config_.scheduling_algorithm = enable_advanced_scheduling ? "priority_queue" : "fifo";

    // Initialize batch configuration
    batch_config_.max_batch_size = 16;
    batch_config_.batch_timeout = std::chrono::microseconds(1000);
    batch_config_.enable_batching = enable_advanced_scheduling;

    Initialize();
}

AsynchronousStreamManager::~AsynchronousStreamManager() {
    Cleanup();
}

void AsynchronousStreamManager::Initialize() {
    try {
        // Create default streams
        for (int i = 0; i < config_.max_streams; ++i) {
            StreamType type = static_cast<StreamType>(i % 4);
            StreamPriority priority = (i < 2) ? StreamPriority::HIGH : StreamPriority::NORMAL;

            StreamConfig config;
            config.stream_id = i;
            config.type = type;
            config.priority = priority;
            config.name = "stream_" + std::to_string(i);
            config.is_dedicated = (i == 0); // First stream is dedicated
            config.max_concurrent_operations = 8;
            config.timeout_duration = config_.default_timeout;
            config.enable_auto_priority = config_.enable_advanced_scheduling;

            // Create CUDA stream
            if (cudaStreamCreate(&config.cuda_stream) != cudaSuccess) {
                throw std::runtime_error("Failed to create CUDA stream " + std::to_string(i));
            }

            streams_[i] = config;

            // Initialize metrics
            StreamPerformanceMetrics metrics{};
            metrics.stream_id = i;
            metrics.stream_name = config.name;
            metrics.type = type;
            metrics.total_operations = 0;
            metrics.completed_operations = 0;
            metrics.failed_operations = 0;
            metrics.success_rate = 1.0;
            metrics.last_updated = std::chrono::system_clock::now();

            stream_metrics_[i] = metrics;
        }

        // Start scheduler thread
        if (config_.enable_advanced_scheduling) {
            shutdown_requested_ = false;
            scheduler_thread_ = std::make_unique<std::thread>(
                &AsynchronousStreamManager::SchedulerThreadFunction, this
            );
        }

        if (config_.enable_profiling) {
            StartProfiling();
        }

        initialized_ = true;

    } catch (const std::exception& e) {
        SetError(ErrorType::CUDA_ERROR, "Initialization failed: " + std::string(e.what()));
        CleanupResources();
    }
}

void AsynchronousStreamManager::Cleanup() {
    if (!initialized_) return;

    // Signal shutdown
    shutdown_requested_ = true;
    scheduler_cv_.notify_all();

    // Stop scheduler thread
    if (scheduler_thread_ && scheduler_thread_->joinable()) {
        scheduler_thread_->join();
    }

    // Stop profiling
    if (config_.enable_profiling) {
        StopProfiling();
    }

    CleanupResources();
    initialized_ = false;
}

void AsynchronousStreamManager::CleanupResources() {
    std::lock_guard<std::mutex> lock(streams_mutex_);

    // Destroy CUDA streams
    for (auto& [id, config] : streams_) {
        if (config.cuda_stream != 0) {
            cudaStreamDestroy(config.cuda_stream);
        }
    }
    streams_.clear();

    // Clear operations
    {
        std::lock_guard<std::mutex> ops_lock(operations_mutex_);
        operations_.clear();
        operation_dependencies_.clear();
    }

    stream_metrics_.clear();
}

int AsynchronousStreamManager::CreateStream(
    StreamType type,
    StreamPriority priority,
    const std::string& name,
    bool is_dedicated) {

    if (!initialized_) {
        SetError(ErrorType::INVALID_CONFIGURATION, "Manager not initialized");
        return -1;
    }

    std::lock_guard<std::mutex> lock(streams_mutex_);

    if (streams_.size() >= MAX_STREAMS) {
        SetError(ErrorType::RESOURCE_ERROR, "Maximum stream limit reached");
        return -1;
    }

    try {
        int stream_id = GetNextStreamId();

        StreamConfig config;
        config.stream_id = stream_id;
        config.type = type;
        config.priority = priority;
        config.name = name.empty() ? "stream_" + std::to_string(stream_id) : name;
        config.is_dedicated = is_dedicated;
        config.max_concurrent_operations = 8;
        config.timeout_duration = config_.default_timeout;
        config.enable_auto_priority = config_.enable_advanced_scheduling;

        if (cudaStreamCreate(&config.cuda_stream) != cudaSuccess) {
            SetError(ErrorType::CUDA_ERROR, "Failed to create CUDA stream");
            return -1;
        }

        streams_[stream_id] = config;

        // Initialize metrics
        StreamPerformanceMetrics metrics{};
        metrics.stream_id = stream_id;
        metrics.stream_name = config.name;
        metrics.type = type;
        metrics.total_operations = 0;
        metrics.completed_operations = 0;
        metrics.failed_operations = 0;
        metrics.success_rate = 1.0;
        metrics.last_updated = std::chrono::system_clock::now();

        stream_metrics_[stream_id] = metrics;

        return stream_id;

    } catch (const std::exception& e) {
        SetError(ErrorType::CUDA_ERROR, "Stream creation failed: " + std::string(e.what()));
        return -1;
    }
}

void AsynchronousStreamManager::DestroyStream(int stream_id) {
    if (!initialized_ || stream_id < 0) return;

    std::lock_guard<std::mutex> lock(streams_mutex_);

    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        // Synchronize and destroy CUDA stream
        cudaStreamSynchronize(it->second.cuda_stream);
        cudaStreamDestroy(it->second.cuda_stream);
        streams_.erase(it);
        stream_metrics_.erase(stream_id);
    }
}

bool AsynchronousStreamManager::IsValidStream(int stream_id) const {
    if (!initialized_ || stream_id < 0) return false;

    std::lock_guard<std::mutex> lock(streams_mutex_);
    return streams_.find(stream_id) != streams_.end();
}

cudaStream_t AsynchronousStreamManager::GetCudaStream(int stream_id) const {
    if (!IsValidStream(stream_id)) return 0;

    std::lock_guard<std::mutex> lock(streams_mutex_);
    return streams_.at(stream_id).cuda_stream;
}

const StreamConfig* AsynchronousStreamManager::GetStreamConfig(int stream_id) const {
    if (!IsValidStream(stream_id)) return nullptr;

    std::lock_guard<std::mutex> lock(streams_mutex_);
    return &streams_.at(stream_id);
}

int AsynchronousStreamManager::ScheduleOperation(
    std::function<void()> operation,
    StreamType type,
    StreamPriority priority,
    const std::vector<int>& dependencies,
    const std::string& operation_name) {

    if (!initialized_) {
        SetError(ErrorType::INVALID_CONFIGURATION, "Manager not initialized");
        return -1;
    }

    if (!operation) {
        SetError(ErrorType::INVALID_CONFIGURATION, "Invalid operation function");
        return -1;
    }

    try {
        int operation_id = GetNextOperationId();
        int stream_id = SelectOptimalStream(type, priority);

        StreamOperation op{};
        op.operation_id = operation_id;
        op.operation_type = StreamOperation::Type::CUSTOM;
        op.operation = operation;
        op.dependency_stream_ids = dependencies;
        op.stream_id = stream_id;
        op.priority = priority;
        op.submit_time = std::chrono::system_clock::now();
        op.estimated_duration = std::chrono::microseconds(1000); // Default estimate
        op.is_completed = false;
        op.has_error = false;

        // Create events for timing
        cudaEventCreate(&op.start_event);
        cudaEventCreate(&op.end_event);

        {
            std::lock_guard<std::mutex> lock(operations_mutex_);
            operations_[operation_id] = op;

            // Add dependencies
            for (int dep_id : dependencies) {
                operation_dependencies_[operation_id].push_back(dep_id);
            }

            if (!config_.enable_advanced_scheduling) {
                // Execute immediately for simple mode
                ExecuteOperation(operation_id);
            } else {
                // Queue for scheduler
                pending_operations_.push(operation_id);
                scheduler_cv_.notify_one();
            }
        }

        return operation_id;

    } catch (const std::exception& e) {
        SetError(ErrorType::RESOURCE_ERROR, "Operation scheduling failed: " + std::string(e.what()));
        return -1;
    }
}

int AsynchronousStreamManager::ScheduleKernelLaunch(
    std::function<void()> kernel_func,
    int stream_id,
    StreamPriority priority,
    const std::vector<int>& dependencies) {

    if (!kernel_func) {
        SetError(ErrorType::INVALID_CONFIGURATION, "Invalid kernel function");
        return -1;
    }

    auto operation = [this, kernel_func, stream_id]() {
        cudaStream_t cuda_stream = GetCudaStream(stream_id);
        if (cuda_stream != 0) {
            // Record start event
            cudaEventRecord(streams_.at(stream_id).cuda_stream);

            // Execute kernel
            kernel_func();

            // Record end event
            cudaEventRecord(streams_.at(stream_id).cuda_stream);
        }
    };

    return ScheduleOperation(operation, StreamType::COMPUTE, priority, dependencies, "kernel_launch");
}

int AsynchronousStreamManager::ScheduleMemoryTransfer(
    void* dst,
    const void* src,
    size_t size,
    cudaMemcpyKind kind,
    int stream_id,
    StreamPriority priority,
    const std::vector<int>& dependencies) {

    auto operation = [this, dst, src, size, kind, stream_id]() {
        cudaStream_t cuda_stream = GetCudaStream(stream_id);
        if (cuda_stream != 0) {
            cudaMemcpyAsync(dst, src, size, kind, cuda_stream);
        }
    };

    StreamType type = (kind == cudaMemcpyHostToDevice || kind == cudaMemcpyDeviceToHost)
                     ? StreamType::MEMORY_TRANSFER
                     : StreamType::COMPUTE;

    return ScheduleOperation(operation, type, priority, dependencies, "memory_transfer");
}

int AsynchronousStreamManager::SchedulePrefetch(
    void* ptr,
    size_t size,
    int device,
    int stream_id,
    StreamPriority priority) {

    auto operation = [this, ptr, size, device, stream_id]() {
        cudaStream_t cuda_stream = GetCudaStream(stream_id);
        if (cuda_stream != 0) {
            cudaMemPrefetchAsync(ptr, size, device, cuda_stream);
        }
    };

    return ScheduleOperation(operation, StreamType::PREFETCH, priority, {}, "prefetch");
}

void AsynchronousStreamManager::EnableAutoLoadBalancing(bool enabled) {
    load_balancing_enabled_ = enabled && config_.enable_advanced_scheduling;
}

void AsynchronousStreamManager::EnableDynamicPriorityAdjustment(bool enabled) {
    dynamic_priority_enabled_ = enabled && config_.enable_advanced_scheduling;
}

void AsynchronousStreamManager::SetSchedulingAlgorithm(const std::string& algorithm) {
    if (algorithm == "fifo" || algorithm == "priority_queue" || algorithm == "load_balanced") {
        config_.scheduling_algorithm = algorithm;
    } else {
        SetError(ErrorType::INVALID_CONFIGURATION, "Invalid scheduling algorithm: " + algorithm);
    }
}

void AsynchronousStreamManager::SetMaxConcurrentOperations(int stream_id, int max_ops) {
    if (!IsValidStream(stream_id)) return;

    std::lock_guard<std::mutex> lock(streams_mutex_);
    streams_.at(stream_id).max_concurrent_operations = max_ops;
}

void AsynchronousStreamManager::AddDependency(int operation_id, int dependency_id) {
    if (!initialized_) return;

    std::lock_guard<std::mutex> lock(operations_mutex_);
    operation_dependencies_[operation_id].push_back(dependency_id);
}

void AsynchronousStreamManager::SynchronizeStream(int stream_id) {
    if (!IsValidStream(stream_id)) return;

    cudaStream_t cuda_stream = GetCudaStream(stream_id);
    if (cuda_stream != 0) {
        cudaStreamSynchronize(cuda_stream);
    }
}

void AsynchronousStreamManager::SynchronizeAllStreams() {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    for (const auto& [id, config] : streams_) {
        cudaStreamSynchronize(config.cuda_stream);
    }
}

void AsynchronousStreamManager::WaitForOperation(int operation_id) {
    if (!initialized_) return;

    while (true) {
        {
            std::lock_guard<std::mutex> lock(operations_mutex_);
            auto it = operations_.find(operation_id);
            if (it != operations_.end() && it->second.is_completed) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void AsynchronousStreamManager::WaitForAllOperations() {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(operations_mutex_);
            bool all_completed = true;
            for (const auto& [id, op] : operations_) {
                if (!op.is_completed) {
                    all_completed = false;
                    break;
                }
            }
            if (all_completed) break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

bool AsynchronousStreamManager::IsOperationCompleted(int operation_id) const {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    auto it = operations_.find(operation_id);
    return it != operations_.end() && it->second.is_completed;
}

bool AsynchronousStreamManager::IsStreamIdle(int stream_id) const {
    if (!IsValidStream(stream_id)) return false;

    std::lock_guard<std::mutex> lock(operations_mutex_);
    for (const auto& [id, op] : operations_) {
        if (op.stream_id == stream_id && !op.is_completed) {
            return false;
        }
    }
    return true;
}

bool AsynchronousStreamManager::AreAllStreamsIdle() const {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    return operations_.empty() ||
           std::all_of(operations_.begin(), operations_.end(),
                       [](const auto& pair) { return pair.second.is_completed; });
}

StreamPerformanceMetrics AsynchronousStreamManager::GetStreamMetrics(int stream_id) const {
    if (!IsValidStream(stream_id)) return {};

    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return stream_metrics_.at(stream_id);
}

std::vector<StreamPerformanceMetrics> AsynchronousStreamManager::GetAllStreamMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    std::vector<StreamPerformanceMetrics> metrics;
    for (const auto& [id, metric] : stream_metrics_) {
        metrics.push_back(metric);
    }
    return metrics;
}

json AsynchronousStreamManager::GetPerformanceAnalytics() const {
    json analytics;

    auto all_metrics = GetAllStreamMetrics();

    // Aggregate metrics
    double total_execution_time = 0.0;
    double total_success_rate = 0.0;
    int total_operations = 0;
    int total_completed = 0;

    for (const auto& metrics : all_metrics) {
        total_execution_time += metrics.total_execution_time.count();
        total_success_rate += metrics.success_rate;
        total_operations += metrics.total_operations;
        total_completed += metrics.completed_operations;
    }

    analytics["total_streams"] = streams_.size();
    analytics["total_operations"] = total_operations;
    analytics["completed_operations"] = total_completed;
    analytics["overall_success_rate"] = total_operations > 0 ?
        static_cast<double>(total_completed) / total_operations : 1.0;
    analytics["average_execution_time_us"] =
        all_metrics.empty() ? 0.0 : total_execution_time / all_metrics.size();

    // Per-stream analytics
    json stream_analytics = json::array();
    for (const auto& metrics : all_metrics) {
        json stream_data;
        stream_data["stream_id"] = metrics.stream_id;
        stream_data["stream_name"] = metrics.stream_name;
        stream_data["stream_type"] = static_cast<int>(metrics.type);
        stream_data["operations"] = metrics.total_operations;
        stream_data["success_rate"] = metrics.success_rate;
        stream_data["utilization"] = metrics.stream_utilization;
        stream_data["overlap_ratio"] = metrics.compute_transfer_overlap_ratio;
        stream_analytics.push_back(stream_data);
    }
    analytics["streams"] = stream_analytics;

    // Configuration
    analytics["config"]["scheduling_algorithm"] = config_.scheduling_algorithm;
    analytics["config"]["load_balancing_enabled"] = load_balancing_enabled_;
    analytics["config"]["dynamic_priority_enabled"] = dynamic_priority_enabled_;
    analytics["config"]["profiling_enabled"] = config_.enable_profiling;

    return analytics;
}

std::string AsynchronousStreamManager::GeneratePerformanceReport() const {
    auto analytics = GetPerformanceAnalytics();
    std::ostringstream report;

    report << "=== Asynchronous Stream Manager Performance Report ===\n\n";

    report << "Overall Statistics:\n";
    report << "  Total Streams: " << analytics["total_streams"] << "\n";
    report << "  Total Operations: " << analytics["total_operations"] << "\n";
    report << "  Completed Operations: " << analytics["completed_operations"] << "\n";
    report << "  Overall Success Rate: " << (analytics["overall_success_rate"].get<double>() * 100) << "%\n";
    report << "  Average Execution Time: " << analytics["average_execution_time_us"].get<double>() << " μs\n\n";

    report << "Configuration:\n";
    report << "  Scheduling Algorithm: " << analytics["config"]["scheduling_algorithm"] << "\n";
    report << "  Load Balancing: " << (analytics["config"]["load_balancing_enabled"] ? "Enabled" : "Disabled") << "\n";
    report << "  Dynamic Priority: " << (analytics["config"]["dynamic_priority_enabled"] ? "Enabled" : "Disabled") << "\n";
    report << "  Profiling: " << (analytics["config"]["profiling_enabled"] ? "Enabled" : "Disabled") << "\n\n";

    report << "Per-Stream Details:\n";
    for (const auto& stream_data : analytics["streams"]) {
        report << "  Stream " << stream_data["stream_id"] << " (" << stream_data["stream_name"] << "):\n";
        report << "    Type: " << stream_data["stream_type"] << "\n";
        report << "    Operations: " << stream_data["operations"] << "\n";
        report << "    Success Rate: " << (stream_data["success_rate"].get<double>() * 100) << "%\n";
        report << "    Utilization: " << (stream_data["utilization"].get<double>() * 100) << "%\n";
        report << "    Overlap Ratio: " << (stream_data["overlap_ratio"].get<double>() * 100) << "%\n";
    }

    return report.str();
}

void AsynchronousStreamManager::OptimizeForWorkload(const std::map<StreamType, int>& workload_distribution) {
    if (!load_balancing_enabled_) return;

    // Reconfigure stream priorities based on workload
    std::map<int, StreamPriority> new_priorities;

    for (const auto& [type, count] : workload_distribution) {
        if (count > 0) {
            // Find best stream for this type
            int best_stream = GetLeastLoadedStream(type);
            if (best_stream >= 0) {
                new_priorities[best_stream] = StreamPriority::HIGH;
            }
        }
    }

    AdjustPriorities(new_priorities);
    RebalanceStreams();
}

void AsynchronousStreamManager::RebalanceStreams() {
    if (!load_balancing_enabled_) return;

    // Perform load balancing across streams
    PerformLoadBalancing();
}

void AsynchronousStreamManager::AdjustPriorities(const std::map<int, StreamPriority>& new_priorities) {
    if (!dynamic_priority_enabled_) return;

    std::lock_guard<std::mutex> lock(streams_mutex_);

    for (const auto& [stream_id, priority] : new_priorities) {
        auto it = streams_.find(stream_id);
        if (it != streams_.end()) {
            it->second.priority = priority;
        }
    }
}

void AsynchronousStreamManager::SetBatchConfig(const BatchConfig& config) {
    batch_config_ = config;
}

int AsynchronousStreamManager::ScheduleBatch(const std::vector<std::function<void()>>& operations) {
    if (!batch_config_.enable_batching || operations.empty()) {
        return -1;
    }

    int batch_id = GetNextOperationId();

    // Schedule operations as a batch
    std::vector<int> operation_ids;
    for (size_t i = 0; i < operations.size(); ++i) {
        int op_id = ScheduleOperation(operations[i], StreamType::COMPUTE,
                                    StreamPriority::NORMAL,
                                    i > 0 ? std::vector<int>{operation_ids.back()} : std::vector<int>{},
                                    "batch_operation_" + std::to_string(i));
        if (op_id >= 0) {
            operation_ids.push_back(op_id);
        }
    }

    return batch_id;
}

AsynchronousStreamManager::ErrorType AsynchronousStreamManager::GetLastError() const {
    return last_error_;
}

std::string AsynchronousStreamManager::GetErrorMessage() const {
    return last_error_message_;
}

bool AsynchronousStreamManager::AttemptErrorRecovery() {
    return RecoverFromError(last_error_);
}

void AsynchronousStreamManager::SetErrorCallback(std::function<void(ErrorType, const std::string&)> callback) {
    error_callback_ = callback;
}

void AsynchronousStreamManager::UpdateConfiguration(const ManagerConfig& config) {
    config_ = config;
    load_balancing_enabled_ = config.enable_advanced_scheduling;
    dynamic_priority_enabled_ = config.enable_advanced_scheduling;
}

AsynchronousStreamManager::ManagerConfig AsynchronousStreamManager::GetCurrentConfiguration() const {
    return config_;
}

size_t AsynchronousStreamManager::GetMemoryUsage() const {
    // Estimate memory usage based on streams and operations
    size_t base_usage = sizeof(*this);
    base_usage += streams_.size() * sizeof(StreamConfig);
    base_usage += operations_.size() * sizeof(StreamOperation);
    base_usage += stream_metrics_.size() * sizeof(StreamPerformanceMetrics);

    return base_usage;
}

int AsynchronousStreamManager::GetActiveStreamCount() const {
    return streams_.size();
}

int AsynchronousStreamManager::GetPendingOperationCount() const {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    return pending_operations_.size();
}

void AsynchronousStreamManager::ResetMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    for (auto& [id, metrics] : stream_metrics_) {
        metrics.total_operations = 0;
        metrics.completed_operations = 0;
        metrics.failed_operations = 0;
        metrics.success_rate = 1.0;
        metrics.total_execution_time = std::chrono::microseconds{0};
        metrics.operation_history.clear();
    }
}

// Private methods

int AsynchronousStreamManager::GetNextStreamId() {
    static int next_id = 0;
    return next_id++;
}

int AsynchronousStreamManager::GetNextOperationId() {
    static int next_id = 0;
    return next_id++;
}

void AsynchronousStreamManager::SchedulerThreadFunction() {
    while (!shutdown_requested_) {
        ProcessPendingOperations();

        if (load_balancing_enabled_) {
            PerformLoadBalancing();
        }

        if (dynamic_priority_enabled_) {
            AdjustPrioritiesDynamically();
        }

        std::unique_lock<std::mutex> lock(operations_mutex_);
        scheduler_cv_.wait_for(lock, SCHEDULER_INTERVAL,
                               [this] { return !pending_operations_.empty() || shutdown_requested_; });
    }
}

void AsynchronousStreamManager::ProcessPendingOperations() {
    while (!pending_operations_.empty()) {
        int operation_id = pending_operations_.front();

        if (CanExecuteOperation(operation_id)) {
            pending_operations_.pop();
            ExecuteOperation(operation_id);
        } else {
            break; // Can't execute this operation, wait for dependencies
        }
    }
}

int AsynchronousStreamManager::SelectOptimalStream(StreamType type, StreamPriority priority) {
    if (load_balancing_enabled_) {
        return GetLeastLoadedStream(type);
    } else {
        // Simple selection - find first matching stream
        std::lock_guard<std::mutex> lock(streams_mutex_);
        for (const auto& [id, config] : streams_) {
            if (config.type == type || config.type == StreamType::OVERLAP) {
                return id;
            }
        }
        return 0; // Default stream
    }
}

bool AsynchronousStreamManager::CanExecuteOperation(int operation_id) {
    auto it = operations_.find(operation_id);
    if (it == operations_.end()) return false;

    return AreDependenciesSatisfied(operation_id);
}

void AsynchronousStreamManager::ExecuteOperation(int operation_id) {
    auto it = operations_.find(operation_id);
    if (it == operations_.end()) return;

    auto& op = it->second;
    auto start_time = std::chrono::high_resolution_clock::now();

    try {
        // Record start event
        cudaEventRecord(op.start_event, GetCudaStream(op.stream_id));

        // Execute the operation
        op.operation();

        // Record end event
        cudaEventRecord(op.end_event, GetCudaStream(op.stream_id));
        cudaEventSynchronize(op.end_event);

        // Calculate duration
        float ms = 0.0f;
        cudaEventElapsedTime(&ms, op.start_event, op.end_event);
        auto duration = std::chrono::microseconds(static_cast<int>(ms * 1000));

        MarkOperationCompleted(operation_id, true);
        UpdateStreamMetrics(op.stream_id, op);
        RecordOperationTiming(operation_id, duration);

    } catch (const std::exception& e) {
        MarkOperationCompleted(operation_id, false, e.what());
        HandleOperationError(operation_id, e.what());
    }
}

void AsynchronousStreamManager::MarkOperationCompleted(int operation_id, bool success, const std::string& error) {
    std::lock_guard<std::mutex> lock(operations_mutex_);

    auto it = operations_.find(operation_id);
    if (it != operations_.end()) {
        it->second.is_completed = true;
        it->second.has_error = !success;
        it->second.error_message = error;

        // Update metrics
        auto stream_it = stream_metrics_.find(it->second.stream_id);
        if (stream_it != stream_metrics_.end()) {
            auto& metrics = stream_it->second;
            metrics.completed_operations++;
            if (!success) {
                metrics.failed_operations++;
            }
            metrics.success_rate = static_cast<double>(metrics.completed_operations) /
                                 (metrics.completed_operations + metrics.failed_operations);
        }

        // Process dependent operations
        ProcessCompletedDependencies(operation_id);
    }
}

bool AsynchronousStreamManager::AreDependenciesSatisfied(int operation_id) const {
    auto it = operation_dependencies_.find(operation_id);
    if (it == operation_dependencies_.end() || it->second.empty()) {
        return true; // No dependencies
    }

    for (int dep_id : it->second) {
        auto dep_it = operations_.find(dep_id);
        if (dep_it == operations_.end() || !dep_it->second.is_completed) {
            return false; // Dependency not satisfied
        }
    }

    return true;
}

void AsynchronousStreamManager::RemoveDependency(int operation_id, int dependency_id) {
    auto it = operation_dependencies_.find(operation_id);
    if (it != operation_dependencies_.end()) {
        auto& deps = it->second;
        deps.erase(std::remove(deps.begin(), deps.end(), dependency_id), deps.end());

        if (deps.empty()) {
            operation_dependencies_.erase(it);
        }
    }
}

void AsynchronousStreamManager::ProcessCompletedDependencies(int operation_id) {
    // Remove this operation from other operations' dependencies
    for (auto& [op_id, deps] : operation_dependencies_) {
        RemoveDependency(op_id, operation_id);
    }
}

void AsynchronousStreamManager::PerformLoadBalancing() {
    // Simple load balancing implementation
    std::lock_guard<std::mutex> lock(streams_mutex_);

    // Calculate current load per stream
    std::map<int, int> stream_loads;
    for (const auto& [id, metrics] : stream_metrics_) {
        stream_loads[id] = metrics.total_operations;
    }

    // Find most and least loaded streams
    if (stream_loads.empty()) return;

    auto max_it = std::max_element(stream_loads.begin(), stream_loads.end(),
                                   [](const auto& a, const auto& b) { return a.second < b.second; });
    auto min_it = std::min_element(stream_loads.begin(), stream_loads.end(),
                                   [](const auto& a, const auto& b) { return a.second < b.second; });

    // If load difference is significant, adjust priorities
    if (max_it->second - min_it->second > 10) {
        streams_.at(min_it->first).priority = StreamPriority::HIGH;
        streams_.at(max_it->first).priority = StreamPriority::NORMAL;
    }
}

int AsynchronousStreamManager::GetLeastLoadedStream(StreamType type) const {
    std::lock_guard<std::mutex> lock(streams_mutex_);

    int best_stream = -1;
    int min_load = INT_MAX;

    for (const auto& [id, config] : streams_) {
        if (config.type == type || config.type == StreamType::OVERLAP) {
            auto metrics_it = stream_metrics_.find(id);
            if (metrics_it != stream_metrics_.end()) {
                int current_load = metrics_it->second.total_operations;
                if (current_load < min_load) {
                    min_load = current_load;
                    best_stream = id;
                }
            }
        }
    }

    return best_stream;
}

void AsynchronousStreamManager::UpdateStreamLoad(int stream_id, std::chrono::microseconds duration) {
    auto metrics_it = stream_metrics_.find(stream_id);
    if (metrics_it != stream_metrics_.end()) {
        metrics_it->second.total_execution_time += duration;
    }
}

void AsynchronousStreamManager::AdjustPrioritiesDynamically() {
    // Dynamic priority adjustment based on recent performance
    for (auto& [id, metrics] : stream_metrics_) {
        if (metrics.success_rate < 0.9) {
            // Low success rate - reduce priority
            auto config_it = streams_.find(id);
            if (config_it != streams_.end() && config_it->second.priority > StreamPriority::LOW) {
                config_it->second.priority = static_cast<StreamPriority>(static_cast<int>(config_it->second.priority) - 1);
            }
        } else if (metrics.success_rate > 0.98 && metrics.stream_utilization > 0.8) {
            // High success rate and utilization - increase priority
            auto config_it = streams_.find(id);
            if (config_it != streams_.end() && config_it->second.priority < StreamPriority::HIGH) {
                config_it->second.priority = static_cast<StreamPriority>(static_cast<int>(config_it->second.priority) + 1);
            }
        }
    }
}

StreamPriority AsynchronousStreamManager::CalculateOptimalPriority(int operation_id, StreamType type) {
    // Calculate optimal priority based on operation type and system state
    switch (type) {
        case StreamType::COMPUTE:
            return StreamPriority::HIGH;
        case StreamType::MEMORY_TRANSFER:
            return StreamPriority::NORMAL;
        case StreamType::PREFETCH:
            return StreamPriority::LOW;
        case StreamType::OVERLAP:
            return StreamPriority::HIGH;
        default:
            return StreamPriority::NORMAL;
    }
}

void AsynchronousStreamManager::UpdateStreamMetrics(int stream_id, const StreamOperation& operation) {
    auto metrics_it = stream_metrics_.find(stream_id);
    if (metrics_it != stream_metrics_.end()) {
        auto& metrics = metrics_it->second;
        metrics.total_operations++;
        metrics.last_updated = std::chrono::system_clock::now();

        // Add to operation history
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - operation.submit_time);
        metrics.operation_history.push_back(duration);

        // Keep only recent history
        if (metrics.operation_history.size() > 100) {
            metrics.operation_history.erase(metrics.operation_history.begin());
        }

        CalculateStreamUtilization(stream_id);
    }
}

void AsynchronousStreamManager::RecordOperationTiming(int operation_id, std::chrono::microseconds duration) {
    // Additional timing recording can be added here
}

void AsynchronousStreamManager::CalculateStreamUtilization(int stream_id) {
    auto metrics_it = stream_metrics_.find(stream_id);
    if (metrics_it == stream_metrics_.end() || metrics_it->second.operation_history.empty()) {
        return;
    }

    auto& metrics = metrics_it->second;
    if (metrics.operation_history.size() < 2) return;

    // Calculate average operation time
    auto total_time = std::accumulate(metrics.operation_history.begin(),
                                     metrics.operation_history.end(),
                                     std::chrono::microseconds{0});
    auto avg_time = total_time / metrics.operation_history.size();

    // Estimate utilization (simplified)
    metrics.stream_utilization = std::min(1.0, static_cast<double>(avg_time.count()) / 1000.0);

    // Calculate compute-transfer overlap ratio (simplified)
    metrics.compute_transfer_overlap_ratio = 0.75; // Placeholder
}

void AsynchronousStreamManager::HandleOperationError(int operation_id, const std::string& error) {
    SetError(ErrorType::CUDA_ERROR, "Operation " + std::to_string(operation_id) + " failed: " + error);

    if (error_callback_) {
        error_callback_(last_error_, last_error_message_);
    }
}

void AsynchronousStreamManager::SetError(ErrorType error, const std::string& message) {
    last_error_ = error;
    last_error_message_ = message;
}

bool AsynchronousStreamManager::RecoverFromError(ErrorType error) {
    switch (error) {
        case ErrorType::CUDA_ERROR:
            // Try to reset CUDA state
            cudaGetLastError(); // Clear error
            return true;

        case ErrorType::TIMEOUT_ERROR:
            // Synchronize all streams
            SynchronizeAllStreams();
            return true;

        case ErrorType::DEPENDENCY_ERROR:
            // Clear pending operations with invalid dependencies
            {
                std::lock_guard<std::mutex> lock(operations_mutex_);
                // Simple recovery - clear pending operations
                std::queue<int> empty;
                std::swap(pending_operations_, empty);
            }
            return true;

        default:
            return false;
    }
}

void AsynchronousStreamManager::StartProfiling() {
    // Initialize profiling state
}

void AsynchronousStreamManager::StopProfiling() {
    // Cleanup profiling state
}

void AsynchronousStreamManager::UpdateAnalytics() {
    // Update analytics data
}

// Factory function
std::unique_ptr<AsynchronousStreamManager> CreateAsynchronousStreamManager(
    int num_streams,
    bool enable_advanced_scheduling,
    bool enable_performance_monitoring) {

    return std::make_unique<AsynchronousStreamManager>(
        num_streams,
        enable_advanced_scheduling,
        enable_performance_monitoring
    );
}

} // namespace performance
} // namespace gpu
} // namespace keycuda