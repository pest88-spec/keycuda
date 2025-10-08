#include "memory_prefetch_manager.h"
#include <algorithm>
#include <fstream>
#include <thread>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <random>

namespace keycuda {
namespace gpu {
namespace performance {

MemoryPrefetchManager::MemoryPrefetchManager(int device_id)
    : device_id_(device_id)
    , current_strategy_(PrefetchStrategy::ADAPTIVE_PREFETCH)
    , initialized_(false)
    , prefetching_enabled_(false)
    , l2_cache_size_(0)
    , memory_bandwidth_gb_per_sec_(0)
    , next_request_id_(1)
    , bandwidth_throttling_enabled_(true)
    , current_prefetch_bandwidth_(0)
    , total_prefetched_bytes_(0)
    , cache_aware_prefetching_enabled_(true)
    , l1_cache_size_(128 * 1024)
    , l2_cache_size_(40 * 1024 * 1024)
    , adaptive_prefetching_enabled_(true)
    , adaptation_threshold_(0.1)
    , shutdown_requested_(false)
    , last_error_(ErrorType::NONE)
    , last_error_message_()
{
    bandwidth_measurement_start_ = std::chrono::system_clock::now();
    last_adaptation_time_ = std::chrono::system_clock::now();
    prefetch_thread_ = nullptr;
    pattern_detection_thread_ = nullptr;
}

MemoryPrefetchManager::~MemoryPrefetchManager() {
    Cleanup();
}

bool MemoryPrefetchManager::Initialize(PrefetchStrategy strategy) {
    std::lock_guard<std::mutex> lock(access_mutex_);

    if (initialized_) {
        SetError(ErrorType::INITIALIZATION_FAILED, "Prefetch manager already initialized");
        return false;
    }

    current_strategy_ = strategy;

    // Initialize device properties
    if (!InitializeDeviceProperties()) {
        return false;
    }

    // Initialize prefetch queues
    InitializePrefetchQueues();

    // Start background threads
    prefetch_thread_ = std::make_unique<std::thread>(
        &MemoryPrefetchManager::PrefetchThreadFunction, this
    );

    pattern_detection_thread_ = std::make_unique<std::thread>(
        &MemoryPrefetchManager::PatternDetectionThreadFunction, this
    );

    initialized_ = true;
    return true;
}

void MemoryPrefetchManager::Cleanup() {
    // Stop background threads
    shutdown_requested_ = true;
    queue_cv_.notify_all();

    if (prefetch_thread_ && prefetch_thread_->joinable()) {
        prefetch_thread_->join();
        prefetch_thread_.reset();
    }

    if (pattern_detection_thread_ && pattern_detection_thread_->joinable()) {
        pattern_detection_thread_->join();
        pattern_detection_thread_.reset();
    }

    std::lock_guard<std::mutex> lock(access_mutex_);

    // Cancel all prefetches
    for (auto& pair : prefetch_requests_) {
        CancelPrefetchInternal(pair.first);
    }

    // Clear all data structures
    access_history_.clear();
    detected_patterns_.clear();
    pattern_frequency_.clear();
    prefetch_requests_.clear();
    priority_queues_.clear();
    stream_prefetches_.clear();
    address_to_request_.clear();
    periodic_prefetches_.clear();
    performance_history_.clear();
    prefetch_effectiveness_history_.clear();
    cached_data_.clear();

    initialized_ = false;
}

bool MemoryPrefetchManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(access_mutex_);
    return initialized_;
}

void MemoryPrefetchManager::EnablePrefetching(bool enabled) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    prefetching_enabled_ = enabled;
}

bool MemoryPrefetchManager::IsPrefetchingEnabled() const {
    std::lock_guard<std::mutex> lock(access_mutex_);
    return prefetching_enabled_;
}

void MemoryPrefetchManager::SetPrefetchStrategy(PrefetchStrategy strategy) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    current_strategy_ = strategy;
}

PrefetchStrategy MemoryPrefetchManager::GetPrefetchStrategy() const {
    std::lock_guard<std::mutex> lock(access_mutex_);
    return current_strategy_;
}

void MemoryPrefetchManager::RecordAccess(
    void* address,
    size_t size,
    AccessPatternType pattern_hint
) {
    if (!initialized_ || !config_.enable_pattern_detection) {
        return;
    }

    std::lock_guard<std::mutex> lock(access_mutex_);

    auto now = std::chrono::system_clock::now();
    access_history_[address].emplace_back(now, size);

    // Limit history size
    if (access_history_[address].size() > MAX_ACCESS_HISTORY_SIZE) {
        access_history_[address].erase(
            access_history_[address].begin(),
            access_history_[address].begin() + (access_history_[address].size() - MAX_ACCESS_HISTORY_SIZE)
        );
    }

    // Update pattern frequency
    pattern_frequency_[pattern_hint]++;

    // Check if we should detect patterns for this address
    if (access_history_[address].size() >= config_.min_accesses_for_pattern) {
        auto pattern = AnalyzeAccessPattern(address, size * 10); // Analyze a larger region
        if (pattern.confidence_score >= config_.pattern_confidence_threshold) {
            UpdateDetectedPatterns(pattern);
        }
    }
}

void MemoryPrefetchManager::RecordSequentialAccess(void* address, size_t size) {
    RecordAccess(address, size, AccessPatternType::SEQUENTIAL);
}

void MemoryPrefetchManager::RecordStridedAccess(void* address, size_t size, size_t stride) {
    RecordAccess(address, size, AccessPatternType::STRIDED);
}

void MemoryPrefetchManager::RecordRandomAccess(void* address, size_t size) {
    RecordAccess(address, size, AccessPatternType::RANDOM);
}

void MemoryPrefetchManager::RecordRecurringAccess(void* address, size_t size) {
    RecordAccess(address, size, AccessPatternType::RECURRING);
}

std::vector<AccessPattern> MemoryPrefetchManager::DetectAccessPatterns(
    const std::vector<std::pair<void*, size_t>>& access_history
) {
    std::vector<AccessPattern> patterns;

    // Group accesses by region
    std::map<void*, std::vector<std::pair<std::chrono::system_clock::time_point, size_t>>> region_accesses;

    for (const auto& access : access_history) {
        void* address = access.first;
        size_t size = access.second;

        // Find base address by rounding down to cache line boundary
        void* base_address = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(address) & ~(128 - 1) // 128-byte cache line alignment
        );

        region_accesses[base_address].emplace_back(std::chrono::system_clock::now(), size);
    }

    // Detect patterns for each region
    for (const auto& pair : region_accesses) {
        if (pair.second.size() >= config_.min_accesses_for_pattern) {
            AccessPattern pattern;
            pattern.type = DetectPatternType(pair.second);
            pattern.base_address = pair.first;
            pattern.access_frequency = pair.second.size();
            pattern.confidence_score = CalculateConfidenceScore(pattern);
            pattern.last_access = pair.second.back().first;

            patterns.push_back(pattern);
        }
    }

    return patterns;
}

AccessPattern MemoryPrefetchManager::AnalyzeAccessPattern(
    void* base_address,
    size_t region_size,
    std::chrono::seconds time_window
) {
    AccessPattern pattern;
    pattern.base_address = base_address;
    pattern.region_size = region_size;

    auto now = std::chrono::system_clock::now();
    auto window_start = now - time_window;

    // Collect recent accesses
    std::vector<std::pair<std::chrono::system_clock::time_point, size_t>> recent_accesses;

    for (const auto& access : access_history_[base_address]) {
        if (access.first >= window_start) {
            recent_accesses.push_back(access);
        }
    }

    if (recent_accesses.size() < config_.min_accesses_for_pattern) {
        pattern.confidence_score = 0.0;
        return pattern;
    }

    // Detect pattern type
    pattern.type = DetectPatternType(recent_accesses);
    pattern.access_frequency = recent_accesses.size();
    pattern.last_access = recent_accesses.back().first;

    // Calculate stride if strided pattern
    if (pattern.type == AccessPatternType::STRIDED) {
        pattern.stride_size = DetectStride(recent_accesses);
    }

    // Calculate access intervals
    if (recent_accesses.size() > 1) {
        std::vector<std::chrono::microseconds> intervals;
        for (size_t i = 1; i < recent_accesses.size(); ++i) {
            auto interval = std::chrono::duration_cast<std::chrono::microseconds>(
                recent_accesses[i].first - recent_accesses[i-1].first
            );
            intervals.push_back(interval);
        }

        // Calculate average interval
        auto total_interval = std::accumulate(intervals.begin(), intervals.end(),
                                            std::chrono::microseconds(0));
        pattern.average_interval = total_interval / intervals.size();

        // Predict next access time
        pattern.next_predicted_access = pattern.last_access + pattern.average_interval;
    }

    // Calculate locality scores
    pattern.spatial_locality_score = CalculateSpatialLocality(recent_accesses);
    pattern.temporal_locality_score = CalculateTemporalLocality(recent_accesses);

    pattern.confidence_score = CalculateConfidenceScore(pattern);

    return pattern;
}

size_t MemoryPrefetchManager::PrefetchToGPU(
    void* host_ptr,
    void* device_ptr,
    size_t size,
    PrefetchPriority priority,
    const std::string& tag
) {
    if (!initialized_ || !prefetching_enabled_) {
        return 0;
    }

    // Check bandwidth constraints
    if (!CanExecutePrefetch(size)) {
        SetError(ErrorType::BANDWIDTH_EXCEEDED, "Prefetch bandwidth limit exceeded");
        return 0;
    }

    PrefetchRequest request;
    request.request_id = next_request_id_++;
    request.source_address = host_ptr;
    request.destination_address = device_ptr;
    request.size = size;
    request.priority = priority;
    request.pattern_type = AccessPatternType::RANDOM;
    request.request_time = std::chrono::system_clock::now();
    request.confidence_score = 1.0;
    request.retry_count = 0;
    request.is_async = false;
    request.source_tag = tag;

    EnqueuePrefetch(request);
    return request.request_id;
}

size_t MemoryPrefetchManager::PrefetchToL2Cache(
    void* device_ptr,
    size_t size,
    PrefetchPriority priority
) {
    if (!initialized_ || !prefetching_enabled_) {
        return 0;
    }

    PrefetchRequest request;
    request.request_id = next_request_id_++;
    request.source_address = device_ptr;
    request.destination_address = device_ptr; // Same address for L2 prefetch
    request.size = size;
    request.priority = priority;
    request.pattern_type = AccessPatternType::SPATIAL_LOCALITY;
    request.request_time = std::chrono::system_clock::now();
    request.confidence_score = 1.0;
    request.is_async = true;

    EnqueuePrefetch(request);
    return request.request_id;
}

size_t MemoryPrefetchManager::PrefetchAsync(
    void* source_ptr,
    void* destination_ptr,
    size_t size,
    cudaStream_t stream,
    PrefetchPriority priority
) {
    if (!initialized_ || !prefetching_enabled_) {
        return 0;
    }

    if (!CanExecutePrefetch(size)) {
        return 0;
    }

    PrefetchRequest request;
    request.request_id = next_request_id_++;
    request.source_address = source_ptr;
    request.destination_address = destination_ptr;
    request.size = size;
    request.priority = priority;
    request.pattern_type = AccessPatternType::RANDOM;
    request.request_time = std::chrono::system_clock::now();
    request.confidence_score = 1.0;
    request.is_async = true;

    EnqueuePrefetch(request);

    // Track stream association
    stream_prefetches_[stream].push_back(request.request_id);

    return request.request_id;
}

void MemoryPrefetchManager::PrefetchBasedOnPattern(
    const AccessPattern& pattern,
    PrefetchPriority priority
) {
    if (!initialized_ || !prefetching_enabled_) {
        return;
    }

    // Predict future accesses based on pattern
    std::vector<std::pair<void*, size_t>> predicted_accesses;

    switch (pattern.type) {
        case AccessPatternType::SEQUENTIAL: {
            // Prefetch next sequential elements
            for (int i = 1; i <= 4; ++i) { // Prefetch next 4 elements
                void* next_addr = static_cast<char*>(pattern.base_address) + (i * pattern.region_size);
                predicted_accesses.emplace_back(next_addr, pattern.region_size);
            }
            break;
        }

        case AccessPatternType::STRIDED: {
            // Prefetch next strided elements
            for (int i = 1; i <= 4; ++i) {
                void* next_addr = static_cast<char*>(pattern.base_address) + (i * pattern.stride_size);
                predicted_accesses.emplace_back(next_addr, pattern.region_size);
            }
            break;
        }

        case AccessPatternType::RECURRING: {
            // Prefetch the same location again
            predicted_accesses.emplace_back(pattern.base_address, pattern.region_size);
            break;
        }

        default:
            break;
    }

    // Create prefetch requests for predicted accesses
    for (const auto& access : predicted_accesses) {
        PrefetchRequest request;
        request.request_id = next_request_id_++;
        request.source_address = access.first;
        request.destination_address = access.first; // Assume same destination for now
        request.size = access.second;
        request.priority = priority;
        request.pattern_type = pattern.type;
        request.request_time = std::chrono::system_clock::now();
        request.confidence_score = pattern.confidence_score;
        request.is_async = true;

        EnqueuePrefetch(request);
    }
}

void MemoryPrefetchManager::PrefetchPredictiveAccesses(
    std::chrono::seconds time_horizon
) {
    if (!initialized_ || !prefetching_enabled_ || !config_.enable_predictive_prefetch) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto horizon_end = now + time_horizon;

    // Find patterns that will likely be accessed within the time horizon
    for (const auto& pattern : detected_patterns_) {
        if (pattern.next_predicted_access >= now && pattern.next_predicted_access <= horizon_end) {
            if (pattern.confidence_score >= config_.pattern_confidence_threshold) {
                PrefetchBasedOnPattern(pattern, PrefetchPriority::NORMAL);
            }
        }
    }
}

void MemoryPrefetchManager::CancelPrefetch(size_t request_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    CancelPrefetchInternal(request_id);
}

void MemoryPrefetchManager::CancelPrefetchesByTag(const std::string& tag) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    std::vector<size_t> to_cancel;
    for (const auto& pair : prefetch_requests_) {
        if (pair.second.source_tag == tag || pair.second.destination_tag == tag) {
            to_cancel.push_back(pair.first);
        }
    }

    for (size_t request_id : to_cancel) {
        CancelPrefetchInternal(request_id);
    }
}

void MemoryPrefetchManager::SetPrefetchPriority(size_t request_id, PrefetchPriority priority) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = prefetch_requests_.find(request_id);
    if (it != prefetch_requests_.end()) {
        it->second.priority = priority;
        PrioritizeQueue();
    }
}

PrefetchRequest MemoryPrefetchManager::GetPrefetchRequest(size_t request_id) const {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = prefetch_requests_.find(request_id);
    if (it != prefetch_requests_.end()) {
        return it->second;
    }

    PrefetchRequest empty_request;
    empty_request.request_id = 0;
    return empty_request;
}

std::vector<size_t> MemoryPrefetchManager::BatchPrefetch(
    const std::vector<std::tuple<void*, void*, size_t>>& prefetch_list,
    PrefetchPriority priority
) {
    std::vector<size_t> request_ids;

    for (const auto& prefetch : prefetch_list) {
        void* source = std::get<0>(prefetch);
        void* destination = std::get<1>(prefetch);
        size_t size = std::get<2>(prefetch);

        size_t request_id = PrefetchToGPU(source, destination, size, priority);
        if (request_id > 0) {
            request_ids.push_back(request_id);
        }
    }

    return request_ids;
}

bool MemoryPrefetchManager::IsPrefetchComplete(size_t request_id) const {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = prefetch_requests_.find(request_id);
    if (it != prefetch_requests_.end()) {
        return it->second.completed_successfully;
    }

    return false;
}

std::chrono::microseconds MemoryPrefetchManager::GetPrefetchLatency(size_t request_id) const {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = prefetch_requests_.find(request_id);
    if (it != prefetch_requests_.end() && it->second.completed_successfully) {
        return it->second.prefetch_latency;
    }

    return std::chrono::microseconds(0);
}

bool MemoryPrefetchManager::WasPrefetchUseful(size_t request_id) const {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = prefetch_requests_.find(request_id);
    if (it != prefetch_requests_.end()) {
        return it->second.was_useful;
    }

    return false;
}

PrefetchMetrics MemoryPrefetchManager::GetPrefetchMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_;
}

double MemoryPrefetchManager::GetPrefetchSuccessRate() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    if (metrics_.total_prefetch_requests == 0) {
        return 0.0;
    }

    return (static_cast<double>(metrics_.successful_prefetches) /
            static_cast<double>(metrics_.total_prefetch_requests)) * 100.0;
}

double MemoryPrefetchManager::GetPrefetchUsefulnessRate() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    if (metrics_.successful_prefetches == 0) {
        return 0.0;
    }

    return (static_cast<double>(metrics_.useful_prefetches) /
            static_cast<double>(metrics_.successful_prefetches)) * 100.0;
}

std::chrono::microseconds MemoryPrefetchManager::GetAveragePrefetchLatency() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_.average_prefetch_latency;
}

std::vector<AccessPattern> MemoryPrefetchManager::GetDetectedPatterns() const {
    std::lock_guard<std::mutex> lock(patterns_mutex_);
    return detected_patterns_;
}

std::map<AccessPatternType, int> MemoryPrefetchManager::GetPatternFrequency() const {
    std::lock_guard<std::mutex> lock(patterns_mutex_);
    return pattern_frequency_;
}

AccessPattern MemoryPrefetchManager::GetMostFrequentPattern() const {
    std::lock_guard<std::mutex> lock(patterns_mutex_);

    if (detected_patterns_.empty()) {
        AccessPattern empty_pattern;
        empty_pattern.confidence_score = 0.0;
        return empty_pattern;
    }

    auto most_frequent = std::max_element(
        detected_patterns_.begin(), detected_patterns_.end(),
        [](const AccessPattern& a, const AccessPattern& b) {
            return a.access_frequency < b.access_frequency;
        }
    );

    return *most_frequent;
}

std::vector<PrefetchRecommendation> MemoryPrefetchManager::GetOptimizationRecommendations() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    std::vector<PrefetchRecommendation> recommendations;

    // Check success rate
    double success_rate = GetPrefetchSuccessRate();
    if (success_rate < 80.0) {
        PrefetchRecommendation rec;
        rec.description = "Low prefetch success rate detected";
        rec.recommended_strategy = PrefetchStrategy::CONSERVATIVE_PREFETCH;
        rec.expected_improvement = (80.0 - success_rate) / 100.0;
        rec.confidence_level = 0.8;
        rec.implementation_effort = std::chrono::milliseconds(500);
        rec.priority = static_cast<int>(80.0 - success_rate);
        rec.suggest_priority_adjustment = true;
        rec.action_items.push_back("Reduce prefetch aggressiveness");
        rec.action_items.push_back("Improve pattern detection accuracy");
        recommendations.push_back(rec);
    }

    // Check usefulness rate
    double usefulness_rate = GetPrefetchUsefulnessRate();
    if (usefulness_rate < 60.0) {
        PrefetchRecommendation rec;
        rec.description = "Low prefetch usefulness rate detected";
        rec.recommended_strategy = PrefetchStrategy::ADAPTIVE_PREFETCH;
        rec.expected_improvement = (60.0 - usefulness_rate) / 100.0;
        rec.confidence_level = 0.9;
        rec.implementation_effort = std::chrono::milliseconds(1000);
        rec.priority = static_cast<int>(60.0 - usefulness_rate);
        rec.suggest_adaptive_prefetching = true;
        rec.action_items.push_back("Enable adaptive prefetching");
        rec.action_items.push_back("Adjust prefetch horizon");
        recommendations.push_back(rec);
    }

    // Check bandwidth utilization
    if (metrics_.prefetch_bandwidth_utilization > 0.8) {
        PrefetchRecommendation rec;
        rec.description = "High prefetch bandwidth utilization";
        rec.recommended_strategy = PrefetchStrategy::BANDWIDTH_AWARE;
        rec.expected_improvement = 0.2;
        rec.confidence_level = 0.7;
        rec.implementation_effort = std::chrono::milliseconds(300);
        rec.priority = static_cast<int>(metrics_.prefetch_bandwidth_utilization * 100);
        rec.suggest_bandwidth_throttling = true;
        rec.action_items.push_back("Enable bandwidth throttling");
        rec.action_items.push_back("Reduce prefetch queue depth");
        recommendations.push_back(rec);
    }

    return recommendations;
}

bool MemoryPrefetchManager::OptimizePrefetchStrategy() {
    if (!adaptive_prefetching_enabled_) {
        return false;
    }

    auto now = std::chrono::system_clock::now();
    if (now - last_adaptation_time_ < config_.adaptation_interval) {
        return false;
    }

    AnalyzePrefetchEffectiveness();
    AdaptStrategyBasedOnPerformance();

    last_adaptation_time_ = now;
    return true;
}

void MemoryPrefetchManager::UpdatePrefetchConfiguration() {
    // This would update prefetch configuration based on current performance
    // Implementation depends on specific requirements
}

void MemoryPrefetchManager::SetMaxPrefetchBandwidth(double bandwidth_gb_per_sec) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    config_.max_prefetch_bandwidth_gb_per_sec = bandwidth_gb_per_sec;
}

double MemoryPrefetchManager::GetMaxPrefetchBandwidth() const {
    std::lock_guard<std::mutex> lock(access_mutex_);
    return config_.max_prefetch_bandwidth_gb_per_sec;
}

double MemoryPrefetchManager::GetCurrentPrefetchBandwidth() const {
    std::lock_guard<std::mutex> lock(access_mutex_);
    return current_prefetch_bandwidth_;
}

void MemoryPrefetchManager::EnableBandwidthThrottling(bool enabled) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    bandwidth_throttling_enabled_ = enabled;
}

void MemoryPrefetchManager::EnableCacheAwarePrefetching(bool enabled) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    cache_aware_prefetching_enabled_ = enabled;
}

void MemoryPrefetchManager::SetCacheSize(size_t l1_size, size_t l2_size) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    l1_cache_size_ = l1_size;
    l2_cache_size_ = l2_size;
}

void MemoryPrefetchManager::PrefetchToCacheLevel(
    void* address,
    size_t size,
    int cache_level,
    PrefetchPriority priority
) {
    if (!cache_aware_prefetching_enabled_) {
        return;
    }

    if (!CanFitInCache(size, cache_level)) {
        EvictFromCacheIfNeeded(size);
    }

    PrefetchRequest request;
    request.request_id = next_request_id_++;
    request.source_address = address;
    request.destination_address = address;
    request.size = size;
    request.priority = priority;
    request.pattern_type = AccessPatternType::SPATIAL_LOCALITY;
    request.request_time = std::chrono::system_clock::now();
    request.confidence_score = 1.0;
    request.is_async = true;

    EnqueuePrefetch(request);
    UpdateCacheUsage(address, size);
}

void MemoryPrefetchManager::EnableAdaptivePrefetching(bool enabled) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    adaptive_prefetching_enabled_ = enabled;
}

void MemoryPrefetchManager::SetAdaptationThreshold(double threshold) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    adaptation_threshold_ = threshold;
}

void MemoryPrefetchManager::UpdatePrefetchEffectiveness(size_t request_id, bool was_useful) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto it = prefetch_requests_.find(request_id);
    if (it != prefetch_requests_.end()) {
        it->second.was_useful = was_useful;
    }

    prefetch_effectiveness_history_.emplace_back(request_id, was_useful);

    // Limit history size
    if (prefetch_effectiveness_history_.size() > config_.max_samples_for_adaptation) {
        prefetch_effectiveness_history_.erase(
            prefetch_effectiveness_history_.begin(),
            prefetch_effecteffectiveness_history_.begin() +
            (prefetch_effectiveness_history_.size() - config_.max_samples_for_adaptation)
        );
    }
}

void MemoryPrefetchManager::SchedulePeriodicPrefetch(
    void* address,
    size_t size,
    std::chrono::seconds interval,
    PrefetchPriority priority
) {
    std::lock_guard<std::mutex> lock(access_mutex_);

    PeriodicPrefetch prefetch;
    prefetch.address = address;
    prefetch.size = size;
    prefetch.interval = interval;
    prefetch.priority = priority;
    prefetch.next_prefetch_time = std::chrono::system_clock::now() + interval;
    prefetch.active = true;
    prefetch.request_id = 0;

    periodic_prefetches_.push_back(prefetch);
}

void MemoryPrefetchManager::CancelPeriodicPrefetch(void* address) {
    std::lock_guard<std::mutex> lock(access_mutex_);

    auto it = std::find_if(periodic_prefetches_.begin(), periodic_prefetches_.end(),
        [address](const PeriodicPrefetch& prefetch) {
            return prefetch.address == address;
        });

    if (it != periodic_prefetches_.end()) {
        if (it->request_id > 0) {
            CancelPrefetch(it->request_id);
        }
        it->active = false;
    }
}

void MemoryPrefetchManager::UpdateConfiguration(const PrefetchConfig& config) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    config_ = config;
}

MemoryPrefetchManager::PrefetchConfig MemoryPrefetchManager::GetCurrentConfiguration() const {
    std::lock_guard<std::mutex> lock(access_mutex_);
    return config_;
}

json MemoryPrefetchManager::GetPrefetchAnalytics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    json analytics;

    // Basic metrics
    analytics["total_prefetch_requests"] = metrics_.total_prefetch_requests;
    analytics["successful_prefetches"] = metrics_.successful_prefetches;
    analytics["failed_prefetches"] = metrics_.failed_prefetches;
    analytics["useful_prefetches"] = metrics_.useful_prefetches;
    analytics["wasted_prefetches"] = metrics_.wasted_prefetches;

    // Performance metrics
    analytics["prefetch_success_rate"] = GetPrefetchSuccessRate();
    analytics["prefetch_usefulness_rate"] = GetPrefetchUsefulness_rate();
    analytics["average_prefetch_latency_us"] =
        std::chrono::duration_cast<std::chrono::microseconds>(metrics_.average_prefetch_latency).count();

    // Bandwidth metrics
    analytics["prefetch_bandwidth_utilization"] = metrics_.prefetch_bandwidth_utilization;
    analytics["total_prefetched_bytes"] = metrics_.total_prefetched_bytes;
    analytics["useful_prefetched_bytes"] = metrics_.useful_prefetched_bytes;
    analytics["wasted_prefetched_bytes"] = metrics_.wasted_prefetched_bytes;

    // Pattern breakdown
    analytics["prefetches_by_pattern"] = json::object();
    for (const auto& pair : metrics_.prefetches_by_pattern) {
        analytics["prefetches_by_pattern"][std::to_string(static_cast<int>(pair.first))] = pair.second;
    }

    // Priority breakdown
    analytics["prefetches_by_priority"] = json::object();
    for (const auto& pair : metrics_.prefetches_by_priority) {
        analytics["prefetches_by_priority"][std::to_string(static_cast<int>(pair.first))] = pair.second;
    }

    // Detected patterns
    analytics["detected_patterns"] = json::array();
    auto patterns = GetDetectedPatterns();
    for (const auto& pattern : patterns) {
        json pattern_json = {
            {"type", static_cast<int>(pattern.type)},
            {"base_address", reinterpret_cast<uintptr_t>(pattern.base_address)},
            {"region_size", pattern.region_size},
            {"stride_size", pattern.stride_size},
            {"access_frequency", pattern.access_frequency},
            {"confidence_score", pattern.confidence_score},
            {"spatial_locality_score", pattern.spatial_locality_score},
            {"temporal_locality_score", pattern.temporal_locality_score}
        };
        analytics["detected_patterns"].push_back(pattern_json);
    }

    // Performance impact
    analytics["overall_performance_improvement"] = metrics_.overall_performance_improvement;
    analytics["cache_hit_rate_improvement"] = metrics_.cache_hit_rate_improvement;
    analytics["memory_latency_reduction"] = metrics_.memory_latency_reduction;

    return analytics;
}

std::string MemoryPrefetchManager::GeneratePrefetchReport() const {
    auto analytics = GetPrefetchAnalytics();

    std::stringstream report;
    report << "=== Memory Prefetch Manager Report ===\n\n";

    // Basic metrics
    report << "Prefetch Performance:\n";
    report << "  Total Requests: " << analytics["total_prefetch_requests"].get<int>() << "\n";
    report << "  Successful: " << analytics["successful_prefetches"].get<int>() << "\n";
    report << "  Failed: " << analytics["failed_prefetches"].get<int>() << "\n";
    report << "  Success Rate: " << std::fixed << std::setprecision(1)
           << analytics["prefetch_success_rate"].get<double>() << "%\n";
    report << "  Usefulness Rate: " << std::fixed << std::setprecision(1)
           << analytics["prefetch_usefulness_rate"].get<double>() << "%\n";
    report << "  Average Latency: " << analytics["average_prefetch_latency_us"].get<long long>() << " μs\n\n";

    // Bandwidth metrics
    report << "Bandwidth Usage:\n";
    report << "  Utilization: " << std::fixed << std::setprecision(1)
           << analytics["prefetch_bandwidth_utilization"].get<double>() << "%\n";
    report << "  Total Prefetched: " << analytics["total_prefetched_bytes"].get<double>() / (1024*1024) << " MB\n";
    report << "  Useful Prefetched: " << analytics["useful_prefetched_bytes"].get<double>() / (1024*1024) << " MB\n";
    report << "  Wasted Prefetched: " << analytics["wasted_prefetched_bytes"].get<double>() / (1024*1024) << " MB\n\n";

    // Pattern analysis
    report << "Detected Patterns:\n";
    for (const auto& pattern : analytics["detected_patterns"]) {
        report << "  Type " << pattern["type"].get<int>()
               << " (Confidence: " << std::fixed << std::setprecision(2)
               << pattern["confidence_score"].get<double>() << ")\n";
    }

    // Performance impact
    report << "\nPerformance Impact:\n";
    report << "  Overall Improvement: " << std::fixed << std::setprecision(1)
           << analytics["overall_performance_improvement"].get<double>() << "%\n";
    report << "  Cache Hit Rate Improvement: " << std::fixed << std::setprecision(1)
           << analytics["cache_hit_rate_improvement"].get<double>() << "%\n";
    report << "  Memory Latency Reduction: " << std::fixed << std::setprecision(1)
           << analytics["memory_latency_reduction"].get<double>() << "%\n";

    return report.str();
}

void MemoryPrefetchManager::ExportPrefetchData(const std::string& filename) const {
    auto analytics = GetPrefetchAnalytics();

    std::ofstream file(filename);
    if (file.is_open()) {
        file << analytics.dump(2);
        file.close();
    }
}

void MemoryPrefetchManager::ExportPatternData(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(patterns_mutex_);

    json pattern_data = json::array();
    for (const auto& pattern : detected_patterns_) {
        json pattern_json = {
            {"type", static_cast<int>(pattern.type)},
            {"base_address", reinterpret_cast<uintptr_t>(pattern.base_address)},
            {"region_size", pattern.region_size},
            {"stride_size", pattern.stride_size},
            {"access_frequency", pattern.access_frequency},
            {"confidence_score", pattern.confidence_score},
            {"spatial_locality_score", pattern.spatial_locality_score},
            {"temporal_locality_score", pattern.temporal_locality_score},
            {"last_access", std::chrono::duration_cast<std::chrono::seconds>(
                pattern.last_access.time_since_epoch()).count()},
            {"next_predicted_access", std::chrono::duration_cast<std::chrono::seconds>(
                pattern.next_predicted_access.time_since_epoch()).count()}
        };
        pattern_data.push_back(pattern_json);
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        file << pattern_data.dump(2);
        file.close();
    }
}

MemoryPrefetchManager::ErrorType MemoryPrefetchManager::GetLastError() const {
    std::lock_guard<std::mutex> lock(access_mutex_);
    return last_error_;
}

std::string MemoryPrefetchManager::GetErrorMessage() const {
    std::lock_guard<std::mutex> lock(access_mutex_);
    return last_error_message_;
}

bool MemoryPrefetchManager::AttemptErrorRecovery() {
    std::lock_guard<std::mutex> lock(access_mutex_);

    switch (last_error_) {
        case ErrorType::CUDA_ERROR:
            // Reset CUDA context
            cudaDeviceReset();
            return InitializeDeviceProperties();

        case ErrorType::INITIALIZATION_FAILED:
            // Attempt re-initialization
            return Initialize(current_strategy_);

        case ErrorType::BANDWIDTH_EXCEEDED:
            // Clear prefetch queue
            std::lock_guard<std::mutex> queue_lock(queue_mutex_);
            while (!prefetch_queue_.empty()) {
                prefetch_queue_.pop();
            }
            return true;

        default:
            return false;
    }
}

// Private methods

bool MemoryPrefetchManager::InitializeDeviceProperties() {
    cudaError_t result = cudaGetDeviceProperties(&device_properties_, device_id_);
    if (result != cudaSuccess) {
        SetError(ErrorType::CUDA_ERROR, "Failed to get device properties");
        return false;
    }

    l2_cache_size_ = device_properties_.l2CacheSize;
    // Estimate memory bandwidth based on architecture
    switch (device_properties_.major * 10 + device_properties_.minor) {
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

void MemoryPrefetchManager::InitializePrefetchQueues() {
    // Initialize priority queues
    priority_queues_[PrefetchPriority::CRITICAL];
    priority_queues_[PrefetchPriority::HIGH];
    priority_queues_[PrefetchPriority::NORMAL];
    priority_queues_[PrefetchPriority::LOW];
    priority_queues_[PrefetchPriority::BACKGROUND];
}

AccessPatternType MemoryPrefetchManager::DetectPatternType(
    const std::vector<std::pair<std::chrono::system_clock::time_point, size_t>>& accesses
) {
    if (accesses.size() < 3) {
        return AccessPatternType::RANDOM;
    }

    // Check for sequential pattern
    bool is_sequential = true;
    for (size_t i = 1; i < accesses.size(); ++i) {
        // This is simplified - in reality, we'd need to check address progression
        // For now, we'll base it on timing regularity
    }

    // Check for regular timing intervals (indicative of patterns)
    std::vector<std::chrono::microseconds> intervals;
    for (size_t i = 1; i < accesses.size(); ++i) {
        auto interval = std::chrono::duration_cast<std::chrono::microseconds>(
            accesses[i].first - accesses[i-1].first
        );
        intervals.push_back(interval);
    }

    // Calculate interval variance
    if (!intervals.empty()) {
        auto total_interval = std::accumulate(intervals.begin(), intervals.end(),
                                            std::chrono::microseconds(0));
        auto avg_interval = total_interval / intervals.size();

        double variance = 0.0;
        for (const auto& interval : intervals) {
            double diff = std::chrono::duration_cast<std::chrono::microseconds>(
                interval - avg_interval
            ).count();
            variance += diff * diff;
        }
        variance /= intervals.size();

        // Low variance suggests regular pattern
        if (variance < avg_interval.count() * 0.1) {
            return AccessPatternType::RECURRING;
        }
    }

    return AccessPatternType::RANDOM;
}

size_t MemoryPrefetchManager::DetectStride(
    const std::vector<std::pair<std::chrono::system_clock::time_point, size_t>>& accesses
) {
    // Simplified stride detection
    // In a real implementation, this would analyze address differences
    return 64; // Default 64-byte stride
}

double MemoryPrefetchManager::CalculateConfidenceScore(const AccessPattern& pattern) {
    double score = 0.0;

    // Frequency component
    if (pattern.access_frequency > 0) {
        score += std::min(pattern.access_frequency / 10.0, 1.0) * 0.4;
    }

    // Locality components
    score += pattern.spatial_locality_score * 0.3;
    score += pattern.temporal_locality_score * 0.3;

    return std::min(score, 1.0);
}

void MemoryPrefetchManager::UpdateDetectedPatterns(const AccessPattern& pattern) {
    std::lock_guard<std::mutex> lock(patterns_mutex_);

    // Check if similar pattern already exists
    auto existing = std::find_if(detected_patterns_.begin(), detected_patterns_.end(),
        [&pattern](const AccessPattern& existing) {
            return existing.type == pattern.type &&
                   std::abs(static_cast<long>(existing.base_address) - static_cast<long>(pattern.base_address)) < 1024;
        });

    if (existing != detected_patterns_.end()) {
        // Update existing pattern
        existing->access_frequency += pattern.access_frequency;
        existing->confidence_score = std::max(existing->confidence_score, pattern.confidence_score);
        existing->last_access = pattern.last_access;
        existing->next_predicted_access = pattern.next_predicted_access;
    } else {
        // Add new pattern
        detected_patterns_.push_back(pattern);

        // Limit number of tracked patterns
        if (detected_patterns_.size() > MAX_DETECTED_PATTERNS) {
            detected_patterns_.erase(detected_patterns_.begin());
        }
    }
}

void MemoryPrefetchManager::ExecutePrefetch(PrefetchRequest& request) {
    auto start_time = std::chrono::high_resolution_clock::now();

    if (request.is_async) {
        ExecuteAsyncPrefetch(request, 0); // Use default stream
    } else {
        ExecuteGPUPrefetch(request);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    request.prefetch_latency = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    request.completion_time = std::chrono::system_clock::now();

    UpdatePrefetchMetrics(request);
}

void MemoryPrefetchManager::ExecuteAsyncPrefetch(PrefetchRequest& request, cudaStream_t stream) {
    // For async prefetch, we'd use CUDA streams and events
    // This is a simplified implementation
    cudaError_t result = cudaSuccess;

    if (request.source_address != request.destination_address) {
        // Async memory copy
        result = cudaMemcpyAsync(
            request.destination_address,
            request.source_address,
            request.size,
            cudaMemcpyHostToDevice,
            stream
        );
    } else {
        // Async prefetch to L2 cache
        int device;
        cudaGetDevice(&device);
        result = cudaMemPrefetchAsync(
            request.source_address,
            request.size,
            device,
            stream
        );
    }

    request.completed_successfully = (result == cudaSuccess);
}

void MemoryPrefetchManager::ExecuteGPUPrefetch(PrefetchRequest& request) {
    // Synchronous GPU prefetch
    int device;
    cudaGetDevice(&device);
    cudaError_t result = cudaMemPrefetchAsync(
        request.source_address,
        request.size,
        device,
        0 // Default stream
    );

    request.completed_successfully = (result == cudaSuccess);
}

void MemoryPrefetchManager::ExecuteL2Prefetch(PrefetchRequest& request) {
    // L2 cache prefetch
    int device;
    cudaGetDevice(&device);
    cudaError_t result = cudaMemPrefetchAsync(
        request.source_address,
        request.size,
        device,
        0 // Default stream
    );

    request.completed_successfully = (result == cudaSuccess);
}

void MemoryPrefetchManager::EnqueuePrefetch(const PrefetchRequest& request) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (prefetch_requests_.size() >= MAX_PREFETCH_REQUESTS) {
        SetError(ErrorType::QUEUE_FULL, "Prefetch queue is full");
        return;
    }

    prefetch_requests_[request.request_id] = request;
    priority_queues_[request.priority].push(request.request_id);
    queue_cv_.notify_one();
}

PrefetchRequest MemoryPrefetchManager::DequeuePrefetch() {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    // Check queues in priority order
    std::vector<PrefetchPriority> priority_order = {
        PrefetchPriority::CRITICAL,
        PrefetchPriority::HIGH,
        PrefetchPriority::NORMAL,
        PrefetchPriority::LOW,
        PrefetchPriority::BACKGROUND
    };

    for (PrefetchPriority priority : priority_order) {
        auto& queue = priority_queues_[priority];
        if (!queue.empty()) {
            size_t request_id = queue.front();
            queue.pop();

            auto it = prefetch_requests_.find(request_id);
            if (it != prefetch_requests_.end()) {
                return it->second;
            }
        }
    }

    PrefetchRequest empty_request;
    empty_request.request_id = 0;
    return empty_request;
}

void MemoryPrefetchManager::ProcessPrefetchQueue() {
    while (!shutdown_requested_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] {
            return shutdown_requested_ || HasPendingPrefetches();
        });

        if (shutdown_requested_) {
            break;
        }

        PrefetchRequest request = DequeuePrefetch();
        if (request.request_id > 0) {
            lock.unlock();
            ExecutePrefetch(request);
            lock.lock();
        }
    }
}

bool MemoryPrefetchManager::HasPendingPrefetches() const {
    for (const auto& pair : priority_queues_) {
        if (!pair.second.empty()) {
            return true;
        }
    }
    return false;
}

void MemoryPrefetchManager::CancelPrefetchInternal(size_t request_id) {
    auto it = prefetch_requests_.find(request_id);
    if (it != prefetch_requests_.end()) {
        // Remove from priority queue
        auto& queue = priority_queues_[it->second.priority];
        std::queue<size_t> new_queue;
        while (!queue.empty()) {
            if (queue.front() != request_id) {
                new_queue.push(queue.front());
            }
            queue.pop();
        }
        queue = new_queue;

        prefetch_requests_.erase(it);
    }
}

void MemoryPrefetchManager::PrioritizeQueue() {
    // Rebuild priority queues based on updated priorities
    std::map<PrefetchPriority, std::queue<size_t>> new_queues;

    for (const auto& pair : prefetch_requests_) {
        const PrefetchRequest& request = pair.second;
        new_queues[request.priority].push(request.request_id);
    }

    priority_queues_ = new_queues;
}

void MemoryPrefetchManager::UpdateBandwidthUsage() {
    auto now = std::chrono::system_clock::now();
    auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(
        now - bandwidth_measurement_start_
    );

    if (time_diff.count() > 0) {
        current_prefetch_bandwidth_ = (total_prefetched_bytes_ * BYTES_TO_GB) / time_diff.count();
        bandwidth_measurement_start_ = now;
        total_prefetched_bytes_ = 0;
    }
}

bool MemoryPrefetchManager::CanExecutePrefetch(size_t size) const {
    if (!bandwidth_throttling_enabled_) {
        return true;
    }

    double projected_bandwidth = current_prefetch_bandwidth_ + (size * BYTES_TO_GB);
    return projected_bandwidth <= config_.max_prefetch_bandwidth_gb_per_sec;
}

void MemoryPrefetchManager::ThrottlePrefetchIfNeeded() {
    if (!bandwidth_throttling_enabled_) {
        return;
    }

    if (current_prefetch_bandwidth_ > config_.max_prefetch_bandwidth_gb_per_sec) {
        // Delay prefetch execution
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool MemoryPrefetchManager::CanFitInCache(size_t size, int cache_level) const {
    size_t cache_size = (cache_level == 1) ? l1_cache_size_ : l2_cache_size_;
    size_t current_usage = 0;

    for (const auto& pair : cached_data_) {
        current_usage += pair.second;
    }

    return (current_usage + size) <= (cache_size * config_.cache_utilization_threshold);
}

void MemoryPrefetchManager::UpdateCacheUsage(void* address, size_t size) {
    cached_data_[address] = size;
}

void MemoryPrefetchManager::EvictFromCacheIfNeeded(size_t required_space) {
    // Simple LRU eviction strategy
    while (GetTotalCacheUsage() + required_space > l2_cache_size_) {
        if (!cached_data_.empty()) {
            cached_data_.erase(cached_data_.begin());
        } else {
            break;
        }
    }
}

size_t MemoryPrefetchManager::GetTotalCacheUsage() const {
    size_t total = 0;
    for (const auto& pair : cached_data_) {
        total += pair.second;
    }
    return total;
}

void MemoryPrefetchManager::UpdatePrefetchStrategy() {
    // This would implement adaptive strategy changes based on performance
}

void MemoryPrefetchManager::AnalyzePrefetchEffectiveness() {
    if (prefetch_effectiveness_history_.size() < config_.min_samples_for_adaptation) {
        return;
    }

    int useful_count = 0;
    for (const auto& pair : prefetch_effectiveness_history_) {
        if (pair.second) {
            useful_count++;
        }
    }

    double usefulness_rate = static_cast<double>(useful_count) / prefetch_effectiveness_history_.size();

    // Adapt strategy based on effectiveness
    if (usefulness_rate < config_.prefetch_usefulness_threshold) {
        // Switch to more conservative strategy
        if (current_strategy_ == PrefetchStrategy::AGGRESSIVE_PREFETCH) {
            current_strategy_ = PrefetchStrategy::ADAPTIVE_PREFETCH;
        } else if (current_strategy_ == PrefetchStrategy::ADAPTIVE_PREFETCH) {
            current_strategy_ = PrefetchStrategy::CONSERVATIVE_PREFETCH;
        }
    }
}

void MemoryPrefetchManager::AdaptStrategyBasedOnPerformance() {
    AnalyzePrefetchEffectiveness();
    UpdatePrefetchStrategy();
}

void MemoryPrefetchManager::ProcessPeriodicPrefetches() {
    auto now = std::chrono::system_clock::now();

    for (auto& prefetch : periodic_prefetches_) {
        if (!prefetch.active) {
            continue;
        }

        if (now >= prefetch.next_prefetch_time) {
            // Execute periodic prefetch
            prefetch.request_id = PrefetchToGPU(
                prefetch.address,
                prefetch.address, // Same source and destination for simplicity
                prefetch.size,
                prefetch.priority
            );

            // Schedule next prefetch
            ScheduleNextPeriodicPrefetch(prefetch);
        }
    }
}

void MemoryPrefetchManager::ScheduleNextPeriodicPrefetch(PeriodicPrefetch& prefetch) {
    prefetch.next_prefetch_time = std::chrono::system_clock::now() + prefetch.interval;
}

void MemoryPrefetchManager::PrefetchThreadFunction() {
    while (!shutdown_requested_) {
        ProcessPrefetchQueue();
        ProcessPeriodicPrefetches();
        UpdateBandwidthUsage();
        ThrottlePrefetchIfNeeded();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void MemoryPrefetchManager::PatternDetectionThreadFunction() {
    while (!shutdown_requested_) {
        if (config_.enable_pattern_detection) {
            // Analyze access history for patterns
            for (const auto& pair : access_history_) {
                if (pair.second.size() >= config_.min_accesses_for_pattern) {
                    auto pattern = AnalyzeAccessPattern(pair.first, 1024);
                    if (pattern.confidence_score >= config_.pattern_confidence_threshold) {
                        UpdateDetectedPatterns(pattern);
                    }
                }
            }

            // Trigger prefetches based on detected patterns
            PrefetchPredictiveAccesses();
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void MemoryPrefetchManager::UpdatePrefetchMetrics(const PrefetchRequest& request) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    metrics_.total_prefetch_requests++;

    if (request.completed_successfully) {
        metrics_.successful_prefetches++;
        metrics_.total_prefetched_bytes += request.size;

        if (request.was_useful) {
            metrics_.useful_prefetches++;
            metrics_.useful_prefetched_bytes += request.size;
        } else {
            metrics_.wasted_prefetches++;
            metrics_.wasted_prefetched_bytes += request.size;
        }

        // Update latency metrics
        RecordPrefetchLatency(request.prefetch_latency);
    } else {
        metrics_.failed_prefetches++;
    }

    // Update pattern and priority metrics
    metrics_.prefetches_by_pattern[request.pattern_type]++;
    metrics_.prefetches_by_priority[request.priority]++;

    // Update success and usefulness rates
    if (metrics_.total_prefetch_requests > 0) {
        metrics_.prefetch_success_rate =
            (static_cast<double>(metrics_.successful_prefetches) /
             static_cast<double>(metrics_.total_prefetch_requests)) * 100.0;
    }

    if (metrics_.successful_prefetches > 0) {
        metrics_.prefetch_usefulness_rate =
            (static_cast<double>(metrics_.useful_prefetches) /
             static_cast<double>(metrics_.successful_prefetches)) * 100.0;
    }

    metrics_.last_updated = std::chrono::system_clock::now();
}

void MemoryPrefetchManager::CalculatePerformanceMetrics() {
    // Calculate comprehensive performance metrics
    // Implementation depends on specific requirements
}

void MemoryPrefetchManager::RecordPrefetchLatency(std::chrono::microseconds latency) {
    // Update min/max latency
    if (latency < metrics_.min_prefetch_latency || metrics_.min_prefetch_latency.count() == 0) {
        metrics_.min_prefetch_latency = latency;
    }
    if (latency > metrics_.max_prefetch_latency) {
        metrics_.max_prefetch_latency = latency;
    }

    // Update average latency
    auto total_latency = metrics_.average_prefetch_latency * (metrics_.successful_prefetches - 1) + latency;
    metrics_.average_prefetch_latency = total_latency / metrics_.successful_prefetches;
}

std::string MemoryPrefetchManager::GetPatternTypeName(AccessPatternType type) const {
    switch (type) {
        case AccessPatternType::SEQUENTIAL: return "Sequential";
        case AccessPatternType::STRIDED: return "Strided";
        case AccessPatternType::RANDOM: return "Random";
        case AccessPatternType::RECURRING: return "Recurring";
        default: return "Unknown";
    }
}

std::string MemoryPrefetchManager::GetPriorityName(PrefetchPriority priority) const {
    switch (priority) {
        case PrefetchPriority::CRITICAL: return "Critical";
        case PrefetchPriority::HIGH: return "High";
        case PrefetchPriority::NORMAL: return "Normal";
        case PrefetchPriority::LOW: return "Low";
        case PrefetchPriority::BACKGROUND: return "Background";
        default: return "Unknown";
    }
}

std::string MemoryPrefetchManager::GetStrategyName(PrefetchStrategy strategy) const {
    switch (strategy) {
        case PrefetchStrategy::ALWAYS_PREFETCH: return "Always Prefetch";
        case PrefetchStrategy::ADAPTIVE_PREFETCH: return "Adaptive Prefetch";
        case PrefetchStrategy::BANDWIDTH_AWARE: return "Bandwidth Aware";
        case PrefetchStrategy::CONSERVATIVE_PREFETCH: return "Conservative";
        case PrefetchStrategy::AGGRESSIVE_PREFETCH: return "Aggressive";
        default: return "Unknown";
    }
}

double MemoryPrefetchManager::CalculatePriorityWeight(PrefetchPriority priority) const {
    switch (priority) {
        case PrefetchPriority::CRITICAL: return config_.critical_priority_weight;
        case PrefetchPriority::HIGH: return config_.high_priority_weight;
        case PrefetchPriority::NORMAL: return config_.normal_priority_weight;
        case PrefetchPriority::LOW: return config_.low_priority_weight;
        case PrefetchPriority::BACKGROUND: return config_.background_priority_weight;
        default: return 1.0;
    }
}

void MemoryPrefetchManager::SetError(ErrorType error, const std::string& message) {
    last_error_ = error;
    last_error_message_ = message;
}

bool MemoryPrefetchManager::RecoverFromError(ErrorType error) {
    // Implementation depends on specific error recovery strategies
    return false;
}

double MemoryPrefetchManager::CalculateSpatialLocality(
    const std::vector<std::pair<std::chrono::system_clock::time_point, size_t>>& accesses
) const {
    // Simplified spatial locality calculation
    return 0.8; // Default value
}

double MemoryPrefetchManager::CalculateTemporalLocality(
    const std::vector<std::pair<std::chrono::system_clock::time_point, size_t>>& accesses
) const {
    // Simplified temporal locality calculation
    if (accesses.size() < 2) {
        return 0.0;
    }

    // Calculate time variance
    std::vector<std::chrono::microseconds> intervals;
    for (size_t i = 1; i < accesses.size(); ++i) {
        auto interval = std::chrono::duration_cast<std::chrono::microseconds>(
            accesses[i].first - accesses[i-1].first
        );
        intervals.push_back(interval);
    }

    auto total_interval = std::accumulate(intervals.begin(), intervals.end(),
                                        std::chrono::microseconds(0));
    auto avg_interval = total_interval / intervals.size();

    double variance = 0.0;
    for (const auto& interval : intervals) {
        double diff = std::chrono::duration_cast<std::chrono::microseconds>(
            interval - avg_interval
        ).count();
        variance += diff * diff;
    }
    variance /= intervals.size();

    // Lower variance indicates higher temporal locality
    double max_variance = avg_interval.count() * avg_interval.count();
    if (max_variance > 0) {
        return 1.0 - (variance / max_variance);
    }

    return 0.0;
}

// Factory function
std::unique_ptr<MemoryPrefetchManager> CreateMemoryPrefetchManager(int device_id) {
    return std::make_unique<MemoryPrefetchManager>(device_id);
}

} // namespace performance
} // namespace gpu
} // namespace keycuda