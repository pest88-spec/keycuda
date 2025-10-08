#include "memory_transfer_batcher.h"
#include <algorithm>
#include <fstream>
#include <thread>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace keycuda {
namespace gpu {
namespace performance {

MemoryTransferBatcher::MemoryTransferBatcher(int device_id)
    : device_id_(device_id)
    , current_strategy_(BatchingStrategy::ADAPTIVE_BATCHING)
    , initialized_(false)
    , batching_enabled_(true)
    , batch_processing_active_(false)
    , memory_bandwidth_gb_per_sec_(0)
    , next_request_id_(1)
    , next_batch_id_(1)
    , auto_optimization_enabled_(true)
    , optimization_interval_(std::chrono::seconds(60))
    , shutdown_requested_(false)
    , adaptive_batching_enabled_(true)
    , adaptation_threshold_(0.1)
    , last_error_(ErrorType::NONE)
    , last_error_message_()
{
    batch_processing_thread_ = nullptr;
    optimization_thread_ = nullptr;
    last_optimization_time_ = std::chrono::system_clock::now();
}

MemoryTransferBatcher::~MemoryTransferBatcher() {
    Cleanup();
}

bool MemoryTransferBatcher::Initialize(BatchingStrategy strategy) {
    std::lock_guard<std::mutex> lock(batch_mutex_);

    if (initialized_) {
        SetError(ErrorType::INITIALIZATION_FAILED, "Transfer batcher already initialized");
        return false;
    }

    current_strategy_ = strategy;

    // Initialize device properties
    if (!InitializeDeviceProperties()) {
        return false;
    }

    // Initialize CUDA streams
    if (!InitializeStreams()) {
        return false;
    }

    // Start batch processing thread
    batch_processing_thread_ = std::make_unique<std::thread>(
        &MemoryTransferBatcher::BatchProcessingThreadFunction, this
    );

    // Start optimization thread if enabled
    if (auto_optimization_enabled_) {
        optimization_thread_ = std::make_unique<std::thread>(
            &MemoryTransferBatcher::OptimizationThreadFunction, this
        );
    }

    initialized_ = true;
    return true;
}

void MemoryTransferBatcher::Cleanup() {
    // Stop batch processing
    StopBatchProcessing();

    // Stop background threads
    shutdown_requested_ = true;
    batch_cv_.notify_all();

    if (batch_processing_thread_ && batch_processing_thread_->joinable()) {
        batch_processing_thread_->join();
        batch_processing_thread_.reset();
    }

    if (optimization_thread_ && optimization_thread_->joinable()) {
        optimization_thread_->join();
        optimization_thread_.reset();
    }

    std::lock_guard<std::mutex> lock(batch_mutex_);

    // Cancel all pending transfers
    for (auto& queue_pair : pending_transfers_) {
        while (!queue_pair.second.empty()) {
            queue_pair.second.pop();
        }
    }

    // Cleanup streams
    CleanupStreams();

    // Clear all data structures
    transfer_requests_.clear();
    transfer_batches_.clear();
    active_batches_.clear();
    completed_batches_.clear();
    performance_history_.clear();
    batch_performance_history_.clear();

    initialized_ = false;
}

bool MemoryTransferBatcher::IsInitialized() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return initialized_;
}

void MemoryTransferBatcher::EnableBatching(bool enabled) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    batching_enabled_ = enabled;
}

bool MemoryTransferBatcher::IsBatchingEnabled() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return batching_enabled_;
}

void MemoryTransferBatcher::SetBatchingStrategy(BatchingStrategy strategy) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    current_strategy_ = strategy;
}

BatchingStrategy MemoryTransferBatcher::GetBatchingStrategy() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return current_strategy_;
}

size_t MemoryTransferBatcher::SubmitTransfer(
    void* source,
    void* destination,
    size_t size,
    TransferDirection direction,
    BatchPriority priority,
    const std::string& tag
) {
    if (!initialized_ || !batching_enabled_) {
        return 0;
    }

    TransferRequest request;
    request.request_id = next_request_id_++;
    request.source_address = source;
    request.destination_address = destination;
    request.size = size;
    request.direction = direction;
    request.priority = priority;
    request.request_time = std::chrono::system_clock::now();
    request.tag = tag;
    request.completed_successfully = false;
    request.was_merged = false;
    request.bandwidth_achieved = 0.0;
    request.efficiency_percentage = 0.0;
    request.retry_count = 0;
    request.batch_id = 0;
    request.position_in_batch = -1;

    std::lock_guard<std::mutex> lock(pending_mutex_);
    transfer_requests_[request.request_id] = request;
    AddToPendingQueue(request);
    batch_cv_.notify_one();

    return request.request_id;
}

size_t MemoryTransferBatcher::SubmitTransferWithCallback(
    void* source,
    void* destination,
    size_t size,
    TransferDirection direction,
    std::function<void(size_t, bool)> callback,
    BatchPriority priority,
    const std::string& tag
) {
    size_t request_id = SubmitTransfer(source, destination, size, direction, priority, tag);

    if (request_id > 0) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = transfer_requests_.find(request_id);
        if (it != transfer_requests_.end()) {
            it->second.completion_callback = callback;
        }
    }

    return request_id;
}

size_t MemoryTransferBatcher::SubmitBatch(
    const std::vector<std::tuple<void*, void*, size_t, TransferDirection>>& transfers,
    BatchType type,
    BatchPriority priority
) {
    if (!initialized_ || !batching_enabled_ || transfers.empty()) {
        return 0;
    }

    std::vector<TransferRequest> transfer_requests;

    for (const auto& transfer_tuple : transfers) {
        TransferRequest request;
        request.request_id = next_request_id_++;
        request.source_address = std::get<0>(transfer_tuple);
        request.destination_address = std::get<1>(transfer_tuple);
        request.size = std::get<2>(transfer_tuple);
        request.direction = std::get<3>(transfer_tuple);
        request.priority = priority;
        request.request_time = std::chrono::system_clock::now();
        request.completed_successfully = false;
        request.was_merged = false;
        request.bandwidth_achieved = 0.0;
        request.efficiency_percentage = 0.0;
        request.retry_count = 0;
        request.batch_id = 0;
        request.position_in_batch = -1;

        transfer_requests.push_back(request);
        transfer_requests_[request.request_id] = request;
    }

    return CreateBatch(transfer_requests, type, priority);
}

void MemoryTransferBatcher::StartBatchProcessing() {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    batch_processing_active_ = true;
    batch_cv_.notify_all();
}

void MemoryTransferBatcher::StopBatchProcessing() {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    batch_processing_active_ = false;
}

void MemoryTransferBatcher::PauseBatchProcessing() {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    batch_processing_active_ = false;
}

void MemoryTransferBatcher::ResumeBatchProcessing() {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    batch_processing_active_ = true;
    batch_cv_.notify_all();
}

bool MemoryTransferBatcher::IsBatchProcessingActive() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return batch_processing_active_;
}

bool MemoryTransferBatcher::OptimizeBatch(size_t batch_id) {
    std::lock_guard<std::mutex> lock(active_mutex_);

    auto it = active_batches_.find(batch_id);
    if (it == active_batches_.end()) {
        SetError(ErrorType::BATCH_CREATION_FAILED, "Batch ID not found");
        return false;
    }

    TransferBatch& batch = it->second;
    bool optimized = false;

    // Optimize batch size
    optimized |= OptimizeBatchSize(batch);

    // Optimize transfer ordering
    optimized |= OptimizeBatchOrdering(batch);

    // Merge contiguous transfers
    if (config_.enable_merging) {
        optimized |= MergeContiguousTransfers(batch);
    }

    // Optimize stream assignment
    if (config_.enable_concurrent_execution) {
        optimized |= OptimizeStreamAssignment(batch);
    }

    batch.was_optimized = optimized;
    return optimized;
}

bool MemoryTransferBatcher::OptimizeAllBatches() {
    std::lock_guard<std::mutex> lock(active_mutex_);

    bool optimized = false;
    for (auto& pair : active_batches_) {
        optimized |= OptimizeBatch(pair.first);
    }

    return optimized;
}

void MemoryTransferBatcher::EnableAutoOptimization(bool enabled) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    auto_optimization_enabled_ = enabled;

    if (enabled && !optimization_thread_) {
        optimization_thread_ = std::make_unique<std::thread>(
            &MemoryTransferBatcher::OptimizationThreadFunction, this
        );
    } else if (!enabled && optimization_thread_) {
        shutdown_requested_ = true;
        if (optimization_thread_->joinable()) {
            optimization_thread_->join();
        }
        optimization_thread_.reset();
        shutdown_requested_ = false;
    }
}

void MemoryTransferBatcher::SetOptimizationInterval(std::chrono::seconds interval) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    optimization_interval_ = interval;
}

void MemoryTransferBatcher::SetMaxBatchSize(size_t max_size_bytes) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    config_.max_batch_size_bytes = max_size_bytes;
}

void MemoryTransferBatcher::SetMaxTransfersPerBatch(int max_transfers) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    config_.max_transfers_per_batch = max_transfers;
}

void MemoryTransferBatcher::SetBatchTimeout(std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    config_.batch_timeout = timeout;
}

void MemoryTransferBatcher::SetMaxConcurrentBatches(int max_batches) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    config_.max_concurrent_batches = max_batches;
}

size_t MemoryTransferBatcher::GetMaxBatchSize() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return config_.max_batch_size_bytes;
}

int MemoryTransferBatcher::GetMaxTransfersPerBatch() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return config_.max_transfers_per_batch;
}

std::chrono::milliseconds MemoryTransferBatcher::GetBatchTimeout() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return config_.batch_timeout;
}

int MemoryTransferBatcher::GetMaxConcurrentBatches() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return config_.max_concurrent_batches;
}

void MemoryTransferBatcher::SetStreamCount(int stream_count) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    config_.stream_count = stream_count;
    CleanupStreams();
    InitializeStreams();
}

int MemoryTransferBatcher::GetStreamCount() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return config_.stream_count;
}

std::vector<cudaStream_t> MemoryTransferBatcher::GetStreams() const {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    return streams_;
}

TransferBatch MemoryTransferBatcher::GetBatchInfo(size_t batch_id) const {
    std::lock_guard<std::mutex> lock(active_mutex_);

    auto it = active_batches_.find(batch_id);
    if (it != active_batches_.end()) {
        return it->second;
    }

    // Check completed batches
    std::lock_guard<std::mutex> completed_lock(completed_mutex_);
    auto completed_it = completed_batches_.find(batch_id);
    if (completed_it != completed_batches_.end()) {
        return completed_it->second;
    }

    TransferBatch empty_batch;
    empty_batch.batch_id = 0;
    return empty_batch;
}

std::vector<TransferBatch> MemoryTransferBatcher::GetActiveBatches() const {
    std::lock_guard<std::mutex> lock(active_mutex_);

    std::vector<TransferBatch> active_batches;
    for (const auto& pair : active_batches_) {
        active_batches.push_back(pair.second);
    }

    return active_batches;
}

std::vector<TransferBatch> MemoryTransferBatcher::GetCompletedBatches() const {
    std::lock_guard<std::mutex> lock(completed_mutex_);

    std::vector<TransferBatch> completed_batches;
    for (const auto& pair : completed_batches_) {
        completed_batches.push_back(pair.second);
    }

    return completed_batches;
}

std::vector<TransferRequest> MemoryTransferBatcher::GetPendingTransfers() const {
    std::lock_guard<std::mutex> lock(pending_mutex_);

    std::vector<TransferRequest> pending_transfers;
    for (const auto& queue_pair : pending_transfers_) {
        std::queue<TransferRequest> queue_copy = queue_pair.second;
        while (!queue_copy.empty()) {
            pending_transfers.push_back(queue_copy.front());
            queue_copy.pop();
        }
    }

    return pending_transfers;
}

BatchingMetrics MemoryTransferBatcher::GetBatchingMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_;
}

double MemoryTransferBatcher::GetAverageBatchEfficiency() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_.overall_efficiency_percentage;
}

std::chrono::microseconds MemoryTransferBatcher::GetAverageBatchDuration() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_.average_batch_duration;
}

double MemoryTransferBatcher::GetBandwidthUtilization() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_.bandwidth_utilization;
}

TransferRequest MemoryTransferBatcher::GetTransferInfo(size_t request_id) const {
    std::lock_guard<std::mutex> lock(pending_mutex_);

    auto it = transfer_requests_.find(request_id);
    if (it != transfer_requests_.end()) {
        return it->second;
    }

    TransferRequest empty_request;
    empty_request.request_id = 0;
    return empty_request;
}

bool MemoryTransferBatcher::IsTransferComplete(size_t request_id) const {
    std::lock_guard<std::mutex> lock(pending_mutex_);

    auto it = transfer_requests_.find(request_id);
    if (it != transfer_requests_.end()) {
        return it->second.completed_successfully;
    }

    return false;
}

std::chrono::microseconds MemoryTransferBatcher::GetTransferDuration(size_t request_id) const {
    std::lock_guard<std::mutex> lock(pending_mutex_);

    auto it = transfer_requests_.find(request_id);
    if (it != transfer_requests_.end() && it->second.completed_successfully) {
        return it->second.transfer_duration;
    }

    return std::chrono::microseconds(0);
}

double MemoryTransferBatcher::GetTransferEfficiency(size_t request_id) const {
    std::lock_guard<std::mutex> lock(pending_mutex_);

    auto it = transfer_requests_.find(request_id);
    if (it != transfer_requests_.end()) {
        return it->second.efficiency_percentage;
    }

    return 0.0;
}

void MemoryTransferBatcher::CancelTransfer(size_t request_id) {
    std::lock_guard<std::mutex> lock(pending_mutex_);

    // Remove from pending queues
    for (auto& queue_pair : pending_transfers_) {
        std::queue<TransferRequest> new_queue;
        while (!queue_pair.second.empty()) {
            if (queue_pair.second.front().request_id != request_id) {
                new_queue.push(queue_pair.second.front());
            }
            queue_pair.second.pop();
        }
        queue_pair.second = new_queue;
    }

    // Remove from transfer requests
    transfer_requests_.erase(request_id);
}

void MemoryTransferBatcher::CancelBatch(size_t batch_id) {
    std::lock_guard<std::mutex> lock(active_mutex_);

    auto it = active_batches_.find(batch_id);
    if (it != active_batches_.end()) {
        // Return transfers to pending queue
        for (const auto& transfer : it->second.transfers) {
            if (!transfer.completed_successfully) {
                AddToPendingQueue(transfer);
            }
        }

        active_batches_.erase(it);
    }
}

void MemoryTransferBatcher::SetTransferPriority(size_t request_id, BatchPriority priority) {
    std::lock_guard<std::mutex> lock(pending_mutex_);

    auto it = transfer_requests_.find(request_id);
    if (it != transfer_requests_.end()) {
        it->second.priority = priority;
        PrioritizeQueues();
    }
}

void MemoryTransferBatcher::SetBatchPriority(size_t batch_id, BatchPriority priority) {
    std::lock_guard<std::mutex> lock(active_mutex_);

    auto it = active_batches_.find(batch_id);
    if (it != active_batches_.end()) {
        it->second.priority = priority;
    }
}

void MemoryTransferBatcher::EnableMerging(bool enabled) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    config_.enable_merging = enabled;
}

void MemoryTransferBatcher::EnablePipelining(bool enabled) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    config_.enable_pipelining = enabled;
}

void MemoryTransferBatcher::EnableConcurrentExecution(bool enabled) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    config_.enable_concurrent_execution = enabled;
}

void MemoryTransferBatcher::EnableAdaptiveBatching(bool enabled) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    adaptive_batching_enabled_ = enabled;
}

void MemoryTransferBatcher::SetAdaptationThreshold(double threshold) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    adaptation_threshold_ = threshold;
}

void MemoryTransferBatcher::UpdateBatchingBasedOnPerformance() {
    if (!adaptive_batching_enabled_) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    if (now - last_optimization_time_ < config_.adaptation_interval) {
        return;
    }

    AnalyzeBatchPerformance();
    AdaptBatchParameters();

    last_optimization_time_ = now;
}

json MemoryTransferBatcher::GetBatchingAnalytics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    json analytics;

    // Basic metrics
    analytics["total_batches_created"] = metrics_.total_batches_created;
    analytics["total_batches_completed"] = metrics_.total_batches_completed;
    analytics["total_transfers_batched"] = metrics_.total_transfers_batched;
    analytics["total_bytes_batched"] = metrics_.total_bytes_batched;

    // Performance metrics
    analytics["average_batch_size_bytes"] = metrics_.average_batch_size_bytes;
    analytics["average_transfers_per_batch"] = metrics_.average_transfers_per_batch;
    analytics["average_batch_duration_us"] =
        std::chrono::duration_cast<std::chrono::microseconds>(metrics_.average_batch_duration).count();
    analytics["overall_efficiency_percentage"] = metrics_.overall_efficiency_percentage;
    analytics["bandwidth_utilization"] = metrics_.bandwidth_utilization;

    // Breakdown by type
    analytics["batches_by_type"] = json::object();
    for (const auto& pair : metrics_.batches_by_type) {
        analytics["batches_by_type"][std::to_string(static_cast<int>(pair.first))] = pair.second;
    }

    analytics["batches_by_direction"] = json::object();
    for (const auto& pair : metrics_.batches_by_direction) {
        analytics["batches_by_direction"][std::to_string(static_cast<int>(pair.first))] = pair.second;
    }

    analytics["batches_by_priority"] = json::object();
    for (const auto& pair : metrics_.batches_by_priority) {
        analytics["batches_by_priority"][std::to_string(static_cast<int>(pair.first))] = pair.second;
    }

    // Optimization impact
    analytics["performance_improvement_percentage"] = metrics_.performance_improvement_percentage;
    analytics["memory_saved_bytes"] = metrics_.memory_saved_bytes;
    analytics["transfers_merged"] = metrics_.transfers_merged;
    analytics["latency_reduction_percentage"] = metrics_.latency_reduction_percentage;

    return analytics;
}

std::string MemoryTransferBatcher::GenerateBatchingReport() const {
    auto analytics = GetBatchingAnalytics();

    std::stringstream report;
    report << "=== Memory Transfer Batching Report ===\n\n";

    // Basic metrics
    report << "Batching Performance:\n";
    report << "  Total Batches Created: " << analytics["total_batches_created"].get<size_t>() << "\n";
    report << "  Total Batches Completed: " << analytics["total_batches_completed"].get<size_t>() << "\n";
    report << "  Total Transfers Batched: " << analytics["total_transfers_batched"].get<size_t>() << "\n";
    report << "  Total Bytes Batched: " << analytics["total_bytes_batched"].get<size_t>() / (1024*1024) << " MB\n\n";

    // Performance metrics
    report << "Performance Metrics:\n";
    report << "  Average Batch Size: " << std::fixed << std::setprecision(1)
           << analytics["average_batch_size_bytes"].get<double>() / (1024*1024) << " MB\n";
    report << "  Average Transfers per Batch: " << std::fixed << std::setprecision(1)
           << analytics["average_transfers_per_batch"].get<double>() << "\n";
    report << "  Average Batch Duration: " << analytics["average_batch_duration_us"].get<long long>() << " μs\n";
    report << "  Overall Efficiency: " << std::fixed << std::setprecision(1)
           << analytics["overall_efficiency_percentage"].get<double>() << "%\n";
    report << "  Bandwidth Utilization: " << std::fixed << std::setprecision(1)
           << analytics["bandwidth_utilization"].get<double>() << "%\n\n";

    // Optimization impact
    report << "Optimization Impact:\n";
    report << "  Performance Improvement: " << std::fixed << std::setprecision(1)
           << analytics["performance_improvement_percentage"].get<double>() << "%\n";
    report << "  Memory Saved: " << analytics["memory_saved_bytes"].get<size_t>() / (1024*1024) << " MB\n";
    report << "  Transfers Merged: " << analytics["transfers_merged"].get<size_t>() << "\n";
    report << "  Latency Reduction: " << std::fixed << std::setprecision(1)
           << analytics["latency_reduction_percentage"].get<double>() << "%\n";

    return report.str();
}

void MemoryTransferBatcher::ExportBatchingData(const std::string& filename) const {
    auto analytics = GetBatchingAnalytics();

    std::ofstream file(filename);
    if (file.is_open()) {
        file << analytics.dump(2);
        file.close();
    }
}

void MemoryTransferBatcher::ExportTransferHistory(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(pending_mutex_);

    json history = json::array();
    for (const auto& pair : transfer_requests_) {
        const TransferRequest& request = pair.second;
        json request_json = {
            {"request_id", request.request_id},
            {"source_address", reinterpret_cast<uintptr_t>(request.source_address)},
            {"destination_address", reinterpret_cast<uintptr_t>(request.destination_address)},
            {"size", request.size},
            {"direction", static_cast<int>(request.direction)},
            {"priority", static_cast<int>(request.priority)},
            {"request_time", std::chrono::duration_cast<std::chrono::seconds>(
                request.request_time.time_since_epoch()).count()},
            {"completed_successfully", request.completed_successfully},
            {"transfer_duration_us", request.transfer_duration.count()},
            {"bandwidth_achieved", request.bandwidth_achieved},
            {"efficiency_percentage", request.efficiency_percentage},
            {"batch_id", request.batch_id},
            {"tag", request.tag}
        };
        history.push_back(request_json);
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        file << history.dump(2);
        file.close();
    }
}

std::vector<BatchingRecommendation> MemoryTransferBatcher::GetOptimizationRecommendations() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    std::vector<BatchingRecommendation> recommendations;

    // Check efficiency
    if (metrics_.overall_efficiency_percentage < config_.target_efficiency * 100.0) {
        BatchingRecommendation rec;
        rec.description = "Low batch efficiency detected";
        rec.recommended_strategy = BatchingStrategy::ADAPTIVE_BATCHING;
        rec.optimal_batch_type = BatchType::MERGED_BATCH;
        rec.expected_improvement = (config_.target_efficiency - metrics_.overall_efficiency_percentage / 100.0);
        rec.implementation_effort = std::chrono::milliseconds(1000);
        rec.priority = static_cast<int>((config_.target_efficiency - metrics_.overall_efficiency_percentage / 100.0) * 100);
        rec.suggest_batch_size_optimization = true;
        rec.suggest_merging_optimization = true;
        rec.action_items.push_back("Optimize batch sizes");
        rec.action_items.push_back("Enable transfer merging");
        recommendations.push_back(rec);
    }

    // Check bandwidth utilization
    if (metrics_.bandwidth_utilization < config_.target_bandwidth_utilization * 100.0) {
        BatchingRecommendation rec;
        rec.description = "Low bandwidth utilization";
        rec.recommended_strategy = BatchingStrategy::BANDWIDTH_OPTIMIZED;
        rec.optimal_batch_type = BatchType::CONCURRENT_BATCH;
        rec.expected_improvement = (config_.target_bandwidth_utilization - metrics_.bandwidth_utilization / 100.0);
        rec.implementation_effort = std::chrono::milliseconds(500);
        rec.priority = static_cast<int>((config_.target_bandwidth_utilization - metrics_.bandwidth_utilization / 100.0) * 100);
        rec.suggest_stream_count_optimization = true;
        rec.suggest_concurrent_optimization = true;
        rec.action_items.push_back("Increase stream count");
        rec.action_items.push_back("Enable concurrent execution");
        recommendations.push_back(rec);
    }

    return recommendations;
}

bool MemoryTransferBatcher::ApplyOptimizationRecommendations() {
    auto recommendations = GetOptimizationRecommendations();
    bool applied = false;

    for (const auto& rec : recommendations) {
        if (rec.priority > 50) { // Only apply high-priority recommendations
            if (rec.suggest_batch_size_optimization) {
                // Adjust batch size based on recommendation
                config_.max_batch_size_bytes = static_cast<size_t>(rec.optimal_batch_size);
                applied = true;
            }

            if (rec.suggest_stream_count_optimization) {
                // Adjust stream count
                config_.stream_count = std::min(config_.stream_count + 2, 16);
                applied = true;
            }

            if (rec.suggest_merging_optimization) {
                config_.enable_merging = true;
                applied = true;
            }

            if (rec.suggest_concurrent_optimization) {
                config_.enable_concurrent_execution = true;
                applied = true;
            }
        }
    }

    return applied;
}

void MemoryTransferBatcher::UpdateConfiguration(const BatcherConfig& config) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    config_ = config;
}

MemoryTransferBatcher::BatcherConfig MemoryTransferBatcher::GetCurrentConfiguration() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return config_;
}

MemoryTransferBatcher::ErrorType MemoryTransferBatcher::GetLastError() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return last_error_;
}

std::string MemoryTransferBatcher::GetErrorMessage() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return last_error_message_;
}

bool MemoryTransferBatcher::AttemptErrorRecovery() {
    std::lock_guard<std::mutex> lock(batch_mutex_);

    switch (last_error_) {
        case ErrorType::CUDA_ERROR:
            // Reset CUDA context
            cudaDeviceReset();
            return InitializeDeviceProperties();

        case ErrorType::INITIALIZATION_FAILED:
            // Attempt re-initialization
            return Initialize(current_strategy_);

        case ErrorType::STREAM_CREATION_FAILED:
            // Recreate streams
            CleanupStreams();
            return InitializeStreams();

        default:
            return false;
    }
}

// Private methods

bool MemoryTransferBatcher::InitializeDeviceProperties() {
    cudaError_t result = cudaGetDeviceProperties(&device_properties_, device_id_);
    if (result != cudaSuccess) {
        SetError(ErrorType::CUDA_ERROR, "Failed to get device properties");
        return false;
    }

    // Estimate memory bandwidth based on architecture
    int compute_capability = device_properties_.major * 10 + device_properties_.minor;
    switch (compute_capability) {
        case 90: // Hopper
            memory_bandwidth_gb_per_sec_ = 3500.0;
            break;
        case 89: // Ada
            memory_bandwidth_gb_per_sec_ = 1000.0;
            break;
        case 86: // Ampere
            memory_bandwidth_gb_per_sec_ = 768.0;
            break;
        case 75: // Turing
            memory_bandwidth_gb_per_sec_ = 616.0;
            break;
        default:
            memory_bandwidth_gb_per_sec_ = 500.0;
            break;
    }

    return true;
}

bool MemoryTransferBatcher::InitializeStreams() {
    std::lock_guard<std::mutex> lock(streams_mutex_);

    for (int i = 0; i < config_.stream_count; ++i) {
        cudaStream_t stream;
        cudaError_t result = cudaStreamCreate(&stream);
        if (result != cudaSuccess) {
            SetError(ErrorType::STREAM_CREATION_FAILED, "Failed to create CUDA stream");
            CleanupStreams();
            return false;
        }
        streams_.push_back(stream);
        available_streams_.push(stream);
    }

    return true;
}

size_t MemoryTransferBatcher::CreateBatch(
    const std::vector<TransferRequest>& transfers,
    BatchType type,
    BatchPriority priority
) {
    TransferBatch batch = CreateBatchInternal(transfers, type, priority);

    std::lock_guard<std::mutex> lock(active_mutex_);
    active_batches_[batch.batch_id] = batch;

    // Start processing if not already active
    if (!batch_processing_active_) {
        StartBatchProcessing();
    }

    return batch.batch_id;
}

TransferBatch MemoryTransferBatcher::CreateBatchInternal(
    const std::vector<TransferRequest>& transfers,
    BatchType type,
    BatchPriority priority
) {
    TransferBatch batch;
    batch.batch_id = next_batch_id_++;
    batch.type = type;
    batch.priority = priority;
    batch.direction = transfers.empty() ? TransferDirection::HOST_TO_DEVICE : transfers[0].direction;
    batch.creation_time = std::chrono::system_clock::now();
    batch.transfers = transfers;
    batch.total_size = 0;
    batch.total_transfers = transfers.size();

    for (const auto& transfer : transfers) {
        batch.total_size += transfer.size;
        transfer.batch_id = batch.batch_id;
    }

    batch.allow_merging = config_.enable_merging;
    batch.allow_pipelining = config_.enable_pipelining;
    batch.allow_concurrent_execution = config_.enable_concurrent_execution;
    batch.max_concurrent_streams = std::min(config_.stream_count, static_cast<int>(transfers.size()));
    batch.was_optimized = false;

    return batch;
}

void MemoryTransferBatcher::ProcessBatch(TransferBatch& batch) {
    auto start_time = std::chrono::high_resolution_clock::now();
    batch.start_time = start_time;

    switch (batch.type) {
        case BatchType::SEQUENTIAL_BATCH:
            ExecuteSequentialBatch(batch);
            break;
        case BatchType::CONCURRENT_BATCH:
            ExecuteConcurrentBatch(batch);
            break;
        case BatchType::MERGED_BATCH:
            ExecuteMergedBatch(batch);
            break;
        case BatchType::PIPELINED_BATCH:
            ExecutePipelinedBatch(batch);
            break;
        default:
            ExecuteSequentialBatch(batch);
            break;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    batch.completion_time = end_time;
    batch.batch_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    CompleteBatch(batch);
}

void MemoryTransferBatcher::CompleteBatch(TransferBatch& batch) {
    // Calculate batch performance metrics
    if (batch.batch_duration.count() > 0) {
        batch.effective_bandwidth = (batch.total_size * 1e-6) / (batch.batch_duration.count() * 1e-6); // GB/s
        batch.theoretical_bandwidth = memory_bandwidth_gb_per_sec_;
        batch.efficiency_percentage = (batch.effective_bandwidth / batch.theoretical_bandwidth) * 100.0;
    }

    // Update transfer completion info
    for (auto& transfer : batch.transfers) {
        transfer.completed_successfully = true;
        transfer.completion_time = batch.completion_time;
        transfer.bandwidth_achieved = batch.effective_bandwidth;
        transfer.efficiency_percentage = batch.efficiency_percentage;

        // Call completion callback if provided
        if (transfer.completion_callback) {
            transfer.completion_callback(transfer.request_id, true);
        }
    }

    // Update metrics
    UpdateBatchingMetrics(batch);

    // Move batch to completed
    std::lock_guard<std::mutex> active_lock(active_mutex_);
    std::lock_guard<std::mutex> completed_lock(completed_mutex_);

    auto it = active_batches_.find(batch.batch_id);
    if (it != active_batches_.end()) {
        completed_batches_[batch.batch_id] = it->second;
        active_batches_.erase(it);
    }
}

void MemoryTransferBatcher::ExecuteSequentialBatch(TransferBatch& batch) {
    for (auto& transfer : batch.transfers) {
        ExecuteTransfer(transfer);
    }
}

void MemoryTransferBatcher::ExecuteConcurrentBatch(TransferBatch& batch) {
    std::vector<cudaStream_t> streams;
    for (int i = 0; i < batch.max_concurrent_streams && i < batch.transfers.size(); ++i) {
        cudaStream_t stream = GetAvailableStream();
        if (stream) {
            streams.push_back(stream);
        }
    }

    // Execute transfers concurrently on available streams
    for (size_t i = 0; i < batch.transfers.size(); ++i) {
        cudaStream_t stream = streams[i % streams.size()];
        ExecuteTransferAsync(batch.transfers[i], stream);
    }

    // Synchronize all streams
    for (cudaStream_t stream : streams) {
        cudaStreamSynchronize(stream);
        ReturnStream(stream);
    }
}

void MemoryTransferBatcher::ExecuteMergedBatch(TransferBatch& batch) {
    // Merge contiguous transfers
    std::vector<TransferRequest> optimized_transfers = OptimizeTransferList(batch.transfers);
    batch.transfers = optimized_transfers;

    // Execute merged transfers
    ExecuteSequentialBatch(batch);
}

void MemoryTransferBatcher::ExecutePipelinedBatch(TransferBatch& batch) {
    // Simple pipelining - overlap execution with preparation
    const int pipeline_depth = config_.pipeline_depth;

    for (int stage = 0; stage < pipeline_depth; ++stage) {
        // Process transfers in pipeline stages
        for (size_t i = stage; i < batch.transfers.size(); i += pipeline_depth) {
            ExecuteTransfer(batch.transfers[i]);
        }
    }
}

bool MemoryTransferBatcher::ExecuteTransfer(TransferRequest& request, cudaStream_t stream) {
    auto start_time = std::chrono::high_resolution_clock::now();

    cudaError_t result = cudaSuccess;
    cudaMemcpyKind kind;

    switch (request.direction) {
        case TransferDirection::HOST_TO_DEVICE:
            kind = cudaMemcpyHostToDevice;
            break;
        case TransferDirection::DEVICE_TO_HOST:
            kind = cudaMemcpyDeviceToHost;
            break;
        case TransferDirection::DEVICE_TO_DEVICE:
            kind = cudaMemcpyDeviceToDevice;
            break;
        default:
            kind = cudaMemcpyHostToDevice;
            break;
    }

    if (stream != 0) {
        result = cudaMemcpyAsync(request.destination_address, request.source_address,
                               request.size, kind, stream);
        cudaStreamSynchronize(stream);
    } else {
        result = cudaMemcpy(request.destination_address, request.source_address,
                          request.size, kind);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    request.transfer_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    if (result == cudaSuccess) {
        // Calculate bandwidth
        if (request.transfer_duration.count() > 0) {
            request.bandwidth_achieved = (request.size * 1e-6) / (request.transfer_duration.count() * 1e-6);
        }
        return true;
    } else {
        SetError(ErrorType::TRANSFER_FAILED, "CUDA memory transfer failed");
        return false;
    }
}

bool MemoryTransferBatcher::ExecuteTransferAsync(TransferRequest& request, cudaStream_t stream) {
    cudaMemcpyKind kind;

    switch (request.direction) {
        case TransferDirection::HOST_TO_DEVICE:
            kind = cudaMemcpyHostToDevice;
            break;
        case TransferDirection::DEVICE_TO_HOST:
            kind = cudaMemcpyDeviceToHost;
            break;
        case TransferDirection::DEVICE_TO_DEVICE:
            kind = cudaMemcpyDeviceToDevice;
            break;
        default:
            kind = cudaMemcpyHostToDevice;
            break;
    }

    cudaError_t result = cudaMemcpyAsync(request.destination_address, request.source_address,
                                       request.size, kind, stream);

    return result == cudaSuccess;
}

bool MemoryTransferBatcher::OptimizeBatchSize(TransferBatch& batch) {
    // This would optimize batch size based on performance history
    // Simplified implementation
    return false;
}

bool MemoryTransferBatcher::OptimizeBatchOrdering(TransferBatch& batch) {
    // Sort transfers by size (largest first) for better efficiency
    std::sort(batch.transfers.begin(), batch.transfers.end(),
        [](const TransferRequest& a, const TransferRequest& b) {
            return a.size > b.size;
        });
    return true;
}

bool MemoryTransferBatcher::MergeContiguousTransfers(TransferBatch& batch) {
    if (!config_.enable_merging || batch.transfers.empty()) {
        return false;
    }

    std::vector<TransferRequest> merged_transfers;
    std::vector<bool> merged(batch.transfers.size(), false);

    for (size_t i = 0; i < batch.transfers.size(); ++i) {
        if (merged[i]) continue;

        TransferRequest current = batch.transfers[i];
        merged[i] = true;

        // Look for contiguous transfers to merge
        for (size_t j = i + 1; j < batch.transfers.size(); ++j) {
            if (merged[j]) continue;

            if (CanMergeTransfers(current, batch.transfers[j])) {
                current = MergeTransfers(current, batch.transfers[j]);
                merged[j] = true;
            }
        }

        merged_transfers.push_back(current);
    }

    if (merged_transfers.size() < batch.transfers.size()) {
        batch.transfers = merged_transfers;
        batch.merging_ratio = static_cast<double>(batch.transfers.size()) / merged_transfers.size();
        return true;
    }

    return false;
}

bool MemoryTransferBatcher::OptimizeStreamAssignment(TransferBatch& batch) {
    // Optimize assignment of transfers to streams
    // Simplified implementation
    return false;
}

std::vector<TransferRequest> MemoryTransferBatcher::CreateBatchBySize(BatchPriority priority) {
    std::vector<TransferRequest> batch_transfers;
    size_t current_batch_size = 0;

    auto& queue = pending_transfers_[priority];
    while (!queue.empty() && batch_transfers.size() < config_.max_transfers_per_batch &&
           current_batch_size < config_.max_batch_size_bytes) {

        TransferRequest transfer = queue.front();
        queue.pop();

        if (current_batch_size + transfer.size <= config_.max_batch_size_bytes) {
            batch_transfers.push_back(transfer);
            current_batch_size += transfer.size;
        } else {
            // Put it back if it doesn't fit
            queue.push(transfer);
            break;
        }
    }

    return batch_transfers;
}

std::vector<TransferRequest> MemoryTransferBatcher::CreateBatchByTime(BatchPriority priority) {
    // Time-based batching - collect transfers within timeout window
    auto now = std::chrono::system_clock::now();
    auto deadline = now + config_.batch_timeout;

    std::vector<TransferRequest> batch_transfers;
    auto& queue = pending_transfers_[priority];

    while (!queue.empty() && batch_transfers.size() < config_.max_transfers_per_batch &&
           std::chrono::system_clock::now() < deadline) {

        TransferRequest transfer = queue.front();
        queue.pop();
        batch_transfers.push_back(transfer);
    }

    return batch_transfers;
}

std::vector<TransferRequest> MemoryTransferBatcher::CreateBatchByVolume(BatchPriority priority) {
    // Volume-based batching - aim for target volume
    const size_t target_volume = config_.max_batch_size_bytes / 2; // 50% of max
    std::vector<TransferRequest> batch_transfers;
    size_t current_volume = 0;

    auto& queue = pending_transfers_[priority];
    while (!queue.empty() && batch_transfers.size() < config_.max_transfers_per_batch &&
           current_volume < target_volume) {

        TransferRequest transfer = queue.front();
        queue.pop();

        batch_transfers.push_back(transfer);
        current_volume += transfer.size;
    }

    return batch_transfers;
}

std::vector<TransferRequest> MemoryTransferBatcher::CreateAdaptiveBatch(BatchPriority priority) {
    // Adaptive batching based on current performance metrics
    if (metrics_.overall_efficiency_percentage < config_.target_efficiency * 100.0) {
        // Low efficiency - use smaller batches
        return CreateBatchBySize(priority);
    } else {
        // Good efficiency - use larger batches for better bandwidth
        return CreateBatchByVolume(priority);
    }
}

bool MemoryTransferBatcher::CanMergeTransfers(const TransferRequest& a, const TransferRequest& b) const {
    if (a.direction != b.direction) {
        return false;
    }

    // Check if transfers are contiguous
    uintptr_t a_end = reinterpret_cast<uintptr_t>(a.source_address) + a.size;
    uintptr_t b_start = reinterpret_cast<uintptr_t>(b.source_address);

    return (b_start - a_end) <= config_.max_merge_gap;
}

TransferRequest MemoryTransferBatcher::MergeTransfers(const TransferRequest& a, const TransferRequest& b) const {
    TransferRequest merged = a;
    merged.size = a.size + b.size;
    merged.was_merged = true;
    return merged;
}

std::vector<TransferRequest> MemoryTransferBatcher::OptimizeTransferList(std::vector<TransferRequest> transfers) {
    if (!config_.enable_merging) {
        return transfers;
    }

    std::vector<TransferRequest> optimized;
    std::vector<bool> processed(transfers.size(), false);

    for (size_t i = 0; i < transfers.size(); ++i) {
        if (processed[i]) continue;

        TransferRequest current = transfers[i];
        processed[i] = true;

        // Try to merge with subsequent transfers
        for (size_t j = i + 1; j < transfers.size(); ++j) {
            if (processed[j]) continue;

            if (CanMergeTransfers(current, transfers[j])) {
                current = MergeTransfers(current, transfers[j]);
                processed[j] = true;
            }
        }

        optimized.push_back(current);
    }

    return optimized;
}

cudaStream_t MemoryTransferBatcher::GetAvailableStream() {
    std::lock_guard<std::mutex> lock(streams_mutex_);

    if (!available_streams_.empty()) {
        cudaStream_t stream = available_streams_.front();
        available_streams_.pop();
        return stream;
    }

    return 0; // No available streams
}

void MemoryTransferBatcher::ReturnStream(cudaStream_t stream) {
    if (stream != 0) {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        available_streams_.push(stream);
    }
}

void MemoryTransferBatcher::CleanupStreams() {
    std::lock_guard<std::mutex> lock(streams_mutex_);

    for (cudaStream_t stream : streams_) {
        cudaStreamDestroy(stream);
    }

    streams_.clear();
    while (!available_streams_.empty()) {
        available_streams_.pop();
    }
}

void MemoryTransferBatcher::UpdateBatchingMetrics(const TransferBatch& batch) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    metrics_.total_batches_completed++;
    metrics_.total_transfers_batched += batch.total_transfers;
    metrics_.total_bytes_batched += batch.total_size;

    // Update average batch size
    if (metrics_.total_batches_completed > 0) {
        metrics_.average_batch_size_bytes =
            static_cast<double>(metrics_.total_bytes_batched) / metrics_.total_batches_completed;
        metrics_.average_transfers_per_batch =
            static_cast<double>(metrics_.total_transfers_batched) / metrics_.total_batches_completed;
    }

    // Update duration metrics
    if (metrics_.average_batch_duration.count() == 0) {
        metrics_.average_batch_duration = batch.batch_duration;
        metrics_.min_batch_duration = batch.batch_duration;
        metrics_.max_batch_duration = batch.batch_duration;
    } else {
        auto total_duration = metrics_.average_batch_duration * (metrics_.total_batches_completed - 1) + batch.batch_duration;
        metrics_.average_batch_duration = total_duration / metrics_.total_batches_completed;

        if (batch.batch_duration < metrics_.min_batch_duration) {
            metrics_.min_batch_duration = batch.batch_duration;
        }
        if (batch.batch_duration > metrics_.max_batch_duration) {
            metrics_.max_batch_duration = batch.batch_duration;
        }
    }

    // Update efficiency
    metrics_.overall_efficiency_percentage = batch.efficiency_percentage;
    metrics_.bandwidth_utilization = (batch.effective_bandwidth / memory_bandwidth_gb_per_sec_) * 100.0;

    // Update breakdowns
    metrics_.batches_by_type[batch.type]++;
    metrics_.batches_by_direction[batch.direction]++;
    metrics_.batches_by_priority[batch.priority]++;

    metrics_.last_updated = std::chrono::system_clock::now();
}

void MemoryTransferBatcher::CalculatePerformanceMetrics() {
    // Calculate comprehensive performance metrics
    // Implementation depends on specific requirements
}

void MemoryTransferBatcher::RecordBatchPerformance(size_t batch_id, double efficiency) {
    batch_performance_history_.emplace_back(batch_id, efficiency);

    // Limit history size
    if (batch_performance_history_.size() > config_.min_samples_for_adaptation) {
        batch_performance_history_.erase(
            batch_performance_history_.begin(),
            batch_performance_history_.begin() + (batch_performance_history_.size() - config_.min_samples_for_adaptation)
        );
    }
}

void MemoryTransferBatcher::UpdateBatchingStrategy() {
    // Update strategy based on performance
    if (metrics_.overall_efficiency_percentage < config_.target_efficiency * 100.0) {
        current_strategy_ = BatchingStrategy::ADAPTIVE_BATCHING;
    } else {
        current_strategy_ = BatchingStrategy::BANDWIDTH_OPTIMIZED;
    }
}

void MemoryTransferBatcher::AnalyzeBatchPerformance() {
    if (batch_performance_history_.size() < config_.min_samples_for_adaptation) {
        return;
    }

    // Analyze recent performance trends
    double recent_avg = 0.0;
    for (const auto& pair : batch_performance_history_) {
        recent_avg += pair.second;
    }
    recent_avg /= batch_performance_history_.size();

    // Check if performance has changed significantly
    if (std::abs(recent_avg - metrics_.overall_efficiency_percentage) > adaptation_threshold_ * 100.0) {
        UpdateBatchingStrategy();
        AdaptBatchParameters();
    }
}

void MemoryTransferBatcher::AdaptBatchParameters() {
    // Adapt batch parameters based on performance analysis
    if (metrics_.overall_efficiency_percentage < config_.target_efficiency * 100.0) {
        // Reduce batch size
        config_.max_batch_size_bytes = static_cast<size_t>(config_.max_batch_size_bytes * 0.8);
        config_.max_transfers_per_batch = static_cast<int>(config_.max_transfers_per_batch * 0.8);
    } else {
        // Increase batch size
        config_.max_batch_size_bytes = static_cast<size_t>(config_.max_batch_size_bytes * 1.2);
        config_.max_transfers_per_batch = std::min(static_cast<int>(config_.max_transfers_per_batch * 1.2), 64);
    }
}

void MemoryTransferBatcher::BatchProcessingThreadFunction() {
    while (!shutdown_requested_) {
        std::unique_lock<std::mutex> lock(batch_mutex_);
        batch_cv_.wait(lock, [this] {
            return shutdown_requested_ || (batch_processing_active_ && HasPendingTransfers());
        });

        if (shutdown_requested_) {
            break;
        }

        if (!batch_processing_active_) {
            continue;
        }

        // Create batches from pending transfers
        std::vector<TransferRequest> batch_transfers;

        // Process by priority
        std::vector<BatchPriority> priorities = {
            BatchPriority::CRITICAL,
            BatchPriority::HIGH,
            BatchPriority::NORMAL,
            BatchPriority::LOW,
            BatchPriority::BACKGROUND
        };

        for (BatchPriority priority : priorities) {
            if (batch_transfers.size() >= config_.max_transfers_per_batch) {
                break;
            }

            std::vector<TransferRequest> priority_transfers;

            switch (current_strategy_) {
                case BatchingStrategy::FIXED_SIZE_BATCHES:
                    priority_transfers = CreateBatchBySize(priority);
                    break;
                case BatchingStrategy::TIME_BASED_BATCHING:
                    priority_transfers = CreateBatchByTime(priority);
                    break;
                case BatchingStrategy::VOLUME_BASED_BATCHING:
                    priority_transfers = CreateBatchByVolume(priority);
                    break;
                case BatchingStrategy::ADAPTIVE_BATCHING:
                    priority_transfers = CreateAdaptiveBatch(priority);
                    break;
                default:
                    priority_transfers = CreateBatchBySize(priority);
                    break;
            }

            for (const auto& transfer : priority_transfers) {
                if (batch_transfers.size() < config_.max_transfers_per_batch) {
                    batch_transfers.push_back(transfer);
                }
            }
        }

        lock.unlock();

        // Create and process batch if we have transfers
        if (!batch_transfers.empty()) {
            BatchType batch_type = BatchType::SEQUENTIAL_BATCH;
            if (config_.enable_concurrent_execution && batch_transfers.size() > 1) {
                batch_type = BatchType::CONCURRENT_BATCH;
            }

            size_t batch_id = CreateBatch(batch_transfers, batch_type, batch_transfers[0].priority);

            // Process the batch
            std::lock_guard<std::mutex> active_lock(active_mutex_);
            auto it = active_batches_.find(batch_id);
            if (it != active_batches_.end()) {
                ProcessBatch(it->second);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void MemoryTransferBatcher::OptimizationThreadFunction() {
    while (!shutdown_requested_) {
        std::this_thread::sleep_for(optimization_interval_);

        if (!shutdown_requested_ && auto_optimization_enabled_) {
            // Optimize all active batches
            OptimizeAllBatches();

            // Update batching based on performance
            UpdateBatchingBasedOnPerformance();

            // Clean up old completed batches
            std::lock_guard<std::mutex> completed_lock(completed_mutex_);
            if (completed_batches_.size() > 1000) {
                auto it = completed_batches_.begin();
                std::advance(it, 500); // Keep last 500 batches
                completed_batches_.erase(completed_batches_.begin(), it);
            }
        }
    }
}

void MemoryTransferBatcher::AddToPendingQueue(const TransferRequest& request) {
    pending_transfers_[request.priority].push(request);
}

bool MemoryTransferBatcher::HasPendingTransfers() const {
    for (const auto& queue_pair : pending_transfers_) {
        if (!queue_pair.second.empty()) {
            return true;
        }
    }
    return false;
}

void MemoryTransferBatcher::PrioritizeQueues() {
    // Re-organize transfers based on updated priorities
    std::map<BatchPriority, std::queue<TransferRequest>> new_queues;

    for (auto& pair : transfer_requests_) {
        if (!pair.second.completed_successfully) {
            new_queues[pair.second.priority].push(pair.second);
        }
    }

    pending_transfers_ = new_queues;
}

std::string MemoryTransferBatcher::GetDirectionName(TransferDirection direction) const {
    switch (direction) {
        case TransferDirection::HOST_TO_DEVICE: return "Host to Device";
        case TransferDirection::DEVICE_TO_HOST: return "Device to Host";
        case TransferDirection::DEVICE_TO_DEVICE: return "Device to Device";
        case TransferDirection::PEER_TO_PEER: return "Peer to Peer";
        default: return "Unknown";
    }
}

std::string MemoryTransferBatcher::GetBatchTypeName(BatchType type) const {
    switch (type) {
        case BatchType::SEQUENTIAL_BATCH: return "Sequential";
        case BatchType::CONCURRENT_BATCH: return "Concurrent";
        case BatchType::MERGED_BATCH: return "Merged";
        case BatchType::PIPELINED_BATCH: return "Pipelined";
        default: return "Unknown";
    }
}

std::string MemoryTransferBatcher::GetPriorityName(BatchPriority priority) const {
    switch (priority) {
        case BatchPriority::CRITICAL: return "Critical";
        case BatchPriority::HIGH: return "High";
        case BatchPriority::NORMAL: return "Normal";
        case BatchPriority::LOW: return "Low";
        case BatchPriority::BACKGROUND: return "Background";
        default: return "Unknown";
    }
}

std::string MemoryTransferBatcher::GetStrategyName(BatchingStrategy strategy) const {
    switch (strategy) {
        case BatchingStrategy::FIXED_SIZE_BATCHES: return "Fixed Size";
        case BatchingStrategy::TIME_BASED_BATCHING: return "Time Based";
        case BatchingStrategy::VOLUME_BASED_BATCHING: return "Volume Based";
        case BatchingStrategy::ADAPTIVE_BATCHING: return "Adaptive";
        default: return "Unknown";
    }
}

double MemoryTransferBatcher::CalculatePriorityWeight(BatchPriority priority) const {
    switch (priority) {
        case BatchPriority::CRITICAL: return config_.critical_priority_weight;
        case BatchPriority::HIGH: return config_.high_priority_weight;
        case BatchPriority::NORMAL: return config_.normal_priority_weight;
        case BatchPriority::LOW: return config_.low_priority_weight;
        case BatchPriority::BACKGROUND: return config_.background_priority_weight;
        default: return 1.0;
    }
}

void MemoryTransferBatcher::SetError(ErrorType error, const std::string& message) {
    last_error_ = error;
    last_error_message_ = message;
}

bool MemoryTransferBatcher::RecoverFromError(ErrorType error) {
    // Implementation depends on specific error recovery strategies
    return false;
}

// Factory function
std::unique_ptr<MemoryTransferBatcher> CreateMemoryTransferBatcher(int device_id) {
    return std::make_unique<MemoryTransferBatcher>(device_id);
}

} // namespace performance
} // namespace gpu
} // namespace keycuda