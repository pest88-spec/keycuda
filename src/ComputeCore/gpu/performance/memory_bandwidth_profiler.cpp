#include "memory_bandwidth_profiler.h"
#include <algorithm>
#include <fstream>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace keycuda {
namespace gpu {
namespace performance {

MemoryBandwidthProfiler::MemoryBandwidthProfiler(int device_id)
    : device_id_(device_id)
    , current_mode_(ProfilingMode::REAL_TIME)
    , initialized_(false)
    , profiling_active_(false)
    , profiling_paused_(false)
    , current_profile_name_()
    , real_time_monitoring_enabled_(false)
    , shutdown_requested_(false)
    , last_error_(ErrorType::NONE)
    , last_error_message_()
{
    monitoring_thread_ = nullptr;
}

MemoryBandwidthProfiler::~MemoryBandwidthProfiler() {
    Cleanup();
}

bool MemoryBandwidthProfiler::Initialize(ProfilingMode mode) {
    std::lock_guard<std::mutex> lock(data_mutex_);

    if (initialized_) {
        SetError(ErrorType::INITIALIZATION_FAILED, "Profiler already initialized");
        return false;
    }

    current_mode_ = mode;

    // Initialize device properties
    if (!InitializeDeviceProperties()) {
        return false;
    }

    // Initialize memory level metrics
    InitializeMemoryLevelMetrics();

    // Start monitoring thread if real-time monitoring is enabled
    if (config_.enable_real_time_monitoring) {
        EnableRealTimeMonitoring(true);
    }

    initialized_ = true;
    return true;
}

void MemoryBandwidthProfiler::Cleanup() {
    std::lock_guard<std::mutex> lock(data_mutex_);

    if (profiling_active_) {
        StopProfiling();
    }

    // Stop monitoring thread
    if (monitoring_thread_ && monitoring_thread_->joinable()) {
        shutdown_requested_ = true;
        monitoring_thread_->join();
        monitoring_thread_.reset();
    }

    // Clear data
    transfer_history_.clear();
    profiles_.clear();
    memory_level_metrics_.clear();
    benchmark_history_.clear();
    async_transfers_.clear();

    initialized_ = false;
}

bool MemoryBandwidthProfiler::IsInitialized() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return initialized_;
}

void MemoryBandwidthProfiler::StartProfiling(const std::string& profile_name) {
    std::lock_guard<std::mutex> lock(profile_mutex_);

    if (!initialized_) {
        SetError(ErrorType::INITIALIZATION_FAILED, "Profiler not initialized");
        return;
    }

    if (profiling_active_) {
        SetError(ErrorType::PROFILING_ALREADY_ACTIVE, "Profiling already active");
        return;
    }

    current_profile_name_ = profile_name;
    profiling_start_time_ = std::chrono::system_clock::now();
    profiling_active_ = true;
    profiling_paused_ = false;

    // Create new profile entry
    BandwidthProfile profile;
    profile.profile_name = profile_name;
    profile.start_time = profiling_start_time_;
    profile.compute_capability = compute_capability_;
    profile.device_name = device_name_;
    profile.total_global_memory = total_global_memory_;
    profile.l2_cache_size = l2_cache_size_;
    profile.shared_memory_per_block = shared_memory_per_block_;
    profile.target_bandwidth_gb_per_sec = config_.performance_target_gb_per_sec;
    profile.meets_performance_target = false;
    profile.sample_count = 0;

    profiles_[profile_name] = profile;
}

void MemoryBandwidthProfiler::StopProfiling() {
    std::lock_guard<std::mutex> lock(profile_mutex_);

    if (!profiling_active_) {
        return;
    }

    profiling_active_ = false;
    profiling_paused_ = false;

    // Finalize current profile
    auto it = profiles_.find(current_profile_name_);
    if (it != profiles_.end()) {
        BandwidthProfile& profile = it->second;
        profile.end_time = std::chrono::system_clock::now();
        profile.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            profile.end_time - profile.start_time
        );

        // Calculate final metrics
        CalculateDerivedMetrics();
        profile = GetCurrentProfile();
    }

    current_profile_name_.clear();
}

void MemoryBandwidthProfiler::PauseProfiling() {
    std::lock_guard<std::mutex> lock(profile_mutex_);
    if (profiling_active_) {
        profiling_paused_ = true;
    }
}

void MemoryBandwidthProfiler::ResumeProfiling() {
    std::lock_guard<std::mutex> lock(profile_mutex_);
    if (profiling_active_) {
        profiling_paused_ = false;
    }
}

bool MemoryBandwidthProfiler::IsProfiling() const {
    std::lock_guard<std::mutex> lock(profile_mutex_);
    return profiling_active_;
}

void MemoryBandwidthProfiler::RecordTransfer(
    TransferType type,
    size_t transfer_size,
    std::chrono::nanoseconds duration,
    cudaStream_t stream,
    bool is_pinned,
    bool is_async
) {
    if (!initialized_ || (profiling_active_ && profiling_paused_)) {
        return;
    }

    std::lock_guard<std::mutex> lock(data_mutex_);

    TransferMetrics metrics;
    metrics.type = type;
    metrics.level = MemoryLevel::GLOBAL_MEMORY;
    metrics.transfer_size = transfer_size;
    metrics.duration = duration;
    metrics.timestamp = std::chrono::system_clock::now();
    metrics.is_pinned_memory = is_pinned;
    metrics.is_asynchronous = is_async;
    metrics.stream_id = reinterpret_cast<uintptr_t>(stream);
    metrics.stream_handle = stream;
    metrics.has_errors = false;

    // Calculate bandwidth
    metrics.bandwidth_gb_per_sec = CalculateBandwidthGBPerSec(transfer_size, duration);
    metrics.theoretical_bandwidth_gb_per_sec = GetTheoreticalBandwidthGBPerSec(type, metrics.level);
    metrics.efficiency_percentage = CalculateEfficiency(
        metrics.bandwidth_gb_per_sec,
        metrics.theoretical_bandwidth_gb_per_sec
    );

    // Calculate additional metrics
    metrics.throughput_mb_per_sec = metrics.bandwidth_gb_per_sec * 1024.0;
    metrics.latency_us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    metrics.utilization_percentage = metrics.efficiency_percentage;
    metrics.overhead_percentage = 100.0 - metrics.efficiency_percentage;

    transfer_history_.push_back(metrics);

    // Update memory level metrics
    UpdateMemoryLevelCounters(metrics.level, true, transfer_size);

    // Update current profile if active
    if (profiling_active_) {
        std::lock_guard<std::mutex> profile_lock(profile_mutex_);
        auto it = profiles_.find(current_profile_name_);
        if (it != profiles_.end()) {
            it->second.transfers_by_type[type].push_back(metrics);
            it->second.sample_count++;
        }
    }

    // Cleanup old transfers if needed
    if (transfer_history_.size() > config_.max_transfer_history) {
        CleanupOldTransfers();
    }
}

void MemoryBandwidthProfiler::RecordAsyncTransferStart(
    TransferType type,
    size_t transfer_size,
    cudaStream_t stream,
    cudaEvent_t start_event
) {
    if (!initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(data_mutex_);

    AsyncTransferRecord record;
    record.type = type;
    record.transfer_size = transfer_size;
    record.stream = stream;
    record.start_event = start_event;
    record.start_time = std::chrono::system_clock::now();

    async_transfers_[stream].push_back(record);
}

void MemoryBandwidthProfiler::RecordAsyncTransferEnd(
    TransferType type,
    cudaStream_t stream,
    cudaEvent_t end_event
) {
    if (!initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(data_mutex_);

    auto it = async_transfers_.find(stream);
    if (it == async_transfers_.end() || it->second.empty()) {
        return;
    }

    // Find the matching start record
    auto& records = it->second;
    auto start_record = std::find_if(records.begin(), records.end(),
        [type](const AsyncTransferRecord& record) {
            return record.type == type;
        });

    if (start_record != records.end()) {
        // Calculate transfer duration
        float milliseconds = 0.0f;
        cudaError_t result = cudaEventElapsedTime(&milliseconds, start_record->start_event, end_event);

        if (result == cudaSuccess) {
            std::chrono::nanoseconds duration(static_cast<int64_t>(milliseconds * 1e6));
            RecordTransfer(type, start_record->transfer_size, duration, stream, false, true);
        }

        records.erase(start_record);
    }
}

MemoryLevelMetrics MemoryBandwidthProfiler::GetMemoryLevelMetrics(MemoryLevel level) const {
    std::lock_guard<std::mutex> lock(data_mutex_);

    auto it = memory_level_metrics_.find(level);
    if (it != memory_level_metrics_.end()) {
        return it->second;
    }

    // Return empty metrics if not found
    MemoryLevelMetrics empty_metrics;
    empty_metrics.level = level;
    empty_metrics.level_name = GetMemoryLevelName(level);
    return empty_metrics;
}

std::vector<MemoryLevelMetrics> MemoryBandwidthProfiler::GetAllMemoryLevelMetrics() const {
    std::lock_guard<std::mutex> lock(data_mutex_);

    std::vector<MemoryLevelMetrics> metrics;
    for (const auto& pair : memory_level_metrics_) {
        metrics.push_back(pair.second);
    }

    return metrics;
}

void MemoryBandwidthProfiler::UpdateMemoryLevelMetrics(MemoryLevel level) {
    std::lock_guard<std::mutex> lock(data_mutex_);

    auto it = memory_level_metrics_.find(level);
    if (it != memory_level_metrics_.end()) {
        // Update timestamp
        it->second.last_updated = std::chrono::system_clock::now();

        // Recalculate derived metrics based on recent transfers
        // Implementation depends on specific requirements
    }
}

double MemoryBandwidthProfiler::GetCurrentBandwidthGBPerSec(TransferType type) const {
    std::lock_guard<std::mutex> lock(data_mutex_);

    // Calculate current bandwidth from recent transfers
    auto now = std::chrono::system_clock::now();
    auto window_start = now - std::chrono::seconds(1); // Last 1 second

    double total_bytes = 0.0;
    int sample_count = 0;

    for (const auto& transfer : transfer_history_) {
        if (transfer.type == type && transfer.timestamp >= window_start) {
            total_bytes += transfer.transfer_size;
            sample_count++;
        }
    }

    if (sample_count == 0) {
        return 0.0;
    }

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - window_start
    );

    return CalculateBandwidthGBPerSec(static_cast<size_t>(total_bytes), duration);
}

double MemoryBandwidthProfiler::GetPeakBandwidthGBPerSec(TransferType type) const {
    std::lock_guard<std::mutex> lock(data_mutex_);

    double peak_bandwidth = 0.0;

    for (const auto& transfer : transfer_history_) {
        if (transfer.type == type && transfer.bandwidth_gb_per_sec > peak_bandwidth) {
            peak_bandwidth = transfer.bandwidth_gb_per_sec;
        }
    }

    return peak_bandwidth;
}

double MemoryBandwidthProfiler::GetAverageBandwidthGBPerSec(TransferType type) const {
    std::lock_guard<std::mutex> lock(data_mutex_);

    if (transfer_history_.empty()) {
        return 0.0;
    }

    double total_bandwidth = 0.0;
    int sample_count = 0;

    for (const auto& transfer : transfer_history_) {
        if (transfer.type == type) {
            total_bandwidth += transfer.bandwidth_gb_per_sec;
            sample_count++;
        }
    }

    return sample_count > 0 ? total_bandwidth / sample_count : 0.0;
}

double MemoryBandwidthProfiler::GetEfficiencyPercentage(TransferType type) const {
    std::lock_guard<std::mutex> lock(data_mutex_);

    if (transfer_history_.empty()) {
        return 0.0;
    }

    double total_efficiency = 0.0;
    int sample_count = 0;

    for (const auto& transfer : transfer_history_) {
        if (transfer.type == type) {
            total_efficiency += transfer.efficiency_percentage;
            sample_count++;
        }
    }

    return sample_count > 0 ? total_efficiency / sample_count : 0.0;
}

BandwidthProfile MemoryBandwidthProfiler::GetCurrentProfile() const {
    std::lock_guard<std::mutex> lock(profile_mutex_);

    if (!profiling_active_ || current_profile_name_.empty()) {
        BandwidthProfile empty_profile;
        return empty_profile;
    }

    auto it = profiles_.find(current_profile_name_);
    if (it != profiles_.end()) {
        return it->second;
    }

    BandwidthProfile empty_profile;
    return empty_profile;
}

std::vector<BandwidthProfile> MemoryBandwidthProfiler::GetHistoricalProfiles() const {
    std::lock_guard<std::mutex> lock(profile_mutex_);

    std::vector<BandwidthProfile> profiles;
    for (const auto& pair : profiles_) {
        profiles.push_back(pair.second);
    }

    // Sort by timestamp (most recent first)
    std::sort(profiles.begin(), profiles.end(),
        [](const BandwidthProfile& a, const BandwidthProfile& b) {
            return a.start_time > b.start_time;
        });

    return profiles;
}

BandwidthProfile MemoryBandwidthProfiler::GetProfileByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(profile_mutex_);

    auto it = profiles_.find(name);
    if (it != profiles_.end()) {
        return it->second;
    }

    BandwidthProfile empty_profile;
    return empty_profile;
}

BandwidthBenchmark MemoryBandwidthProfiler::RunBandwidthBenchmark(
    const std::string& benchmark_name,
    const std::vector<TransferType>& transfer_types,
    const std::vector<size_t>& transfer_sizes,
    int iterations
) {
    BandwidthBenchmark benchmark;
    benchmark.benchmark_name = benchmark_name;
    benchmark.transfer_types = transfer_types;
    benchmark.transfer_sizes = transfer_sizes;
    benchmark.timestamp = std::chrono::system_clock::now();

    // Run benchmarks for each transfer type and size
    for (TransferType type : transfer_types) {
        for (size_t size : transfer_sizes) {
            // Measure bandwidth
            double bandwidth = BenchmarkTransferType(type, size, iterations);
            benchmark.bandwidth_results[type][size] = bandwidth;

            // Measure latency
            auto latency = MeasureTransferLatency(type, size, iterations);
            benchmark.latency_results[type][size] =
                std::chrono::duration_cast<std::chrono::microseconds>(latency).count();
        }
    }

    // Find optimal configuration
    double max_bandwidth = 0.0;
    for (const auto& type_result : benchmark.bandwidth_results) {
        for (const auto& size_result : type_result.second) {
            if (size_result.second > max_bandwidth) {
                max_bandwidth = size_result.second;
                benchmark.optimal_transfer_type = type_result.first;
                benchmark.optimal_transfer_size = size_result.first;
            }
        }
    }
    benchmark.peak_performance_bandwidth = max_bandwidth;

    return benchmark;
}

std::vector<BandwidthBenchmark> MemoryBandwidthProfiler::RunStandardBenchmarks() {
    std::vector<BandwidthBenchmark> benchmarks;

    // Standard transfer types
    std::vector<TransferType> standard_types = {
        TransferType::HOST_TO_DEVICE,
        TransferType::DEVICE_TO_HOST,
        TransferType::DEVICE_TO_DEVICE
    };

    // Standard transfer sizes (powers of 2 from 1KB to 1GB)
    std::vector<size_t> standard_sizes;
    for (size_t size = 1024; size <= 1024ull * 1024 * 1024; size *= 2) {
        standard_sizes.push_back(size);
    }

    // Run basic bandwidth benchmark
    benchmarks.push_back(RunBandwidthBenchmark(
        "Standard_Bandwidth",
        standard_types,
        standard_sizes,
        DEFAULT_BENCHMARK_ITERATIONS
    ));

    // Run async transfer benchmark if supported
    if (SupportsUnifiedMemory()) {
        std::vector<TransferType> async_types = {
            TransferType::ASYNC_HOST_TO_DEVICE,
            TransferType::ASYNC_DEVICE_TO_HOST
        };
        benchmarks.push_back(RunBandwidthBenchmark(
            "Async_Bandwidth",
            async_types,
            standard_sizes,
            DEFAULT_BENCHMARK_ITERATIONS
        ));
    }

    return benchmarks;
}

void MemoryBandwidthProfiler::SetPerformanceTarget(double bandwidth_gb_per_sec) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    config_.performance_target_gb_per_sec = bandwidth_gb_per_sec;
}

double MemoryBandwidthProfiler::GetPerformanceTarget() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return config_.performance_target_gb_per_sec;
}

bool MemoryBandwidthProfiler::IsMeetingPerformanceTarget() const {
    double current_bandwidth = GetCurrentBandwidthGBPerSec(TransferType::HOST_TO_DEVICE);
    return current_bandwidth >= config_.performance_target_gb_per_sec;
}

std::vector<std::string> MemoryBandwidthProfiler::IdentifyBottlenecks() const {
    std::lock_guard<std::mutex> lock(data_mutex_);

    std::vector<std::string> bottlenecks;

    // Check transfer efficiency
    double avg_efficiency = GetEfficiencyPercentage(TransferType::HOST_TO_DEVICE);
    if (avg_efficiency < config_.efficiency_threshold * 100.0) {
        bottlenecks.push_back("Low transfer efficiency for H2D transfers");
    }

    // Check memory utilization
    auto global_metrics = GetMemoryLevelMetrics(MemoryLevel::GLOBAL_MEMORY);
    if (global_metrics.utilization_percentage < 50.0) {
        bottlenecks.push_back("Low global memory utilization");
    }

    // Check cache performance
    auto l2_metrics = GetMemoryLevelMetrics(MemoryLevel::L2_CACHE);
    if (l2_metrics.cache_hit_rate < 0.8) {
        bottlenecks.push_back("Low L2 cache hit rate");
    }

    // Check for performance variance
    double peak_bandwidth = GetPeakBandwidthGBPerSec(TransferType::HOST_TO_DEVICE);
    double avg_bandwidth = GetAverageBandwidthGBPerSec(TransferType::HOST_TO_DEVICE);
    if (peak_bandwidth > 0 && (peak_bandwidth - avg_bandwidth) / peak_bandwidth > 0.3) {
        bottlenecks.push_back("High performance variance detected");
    }

    return bottlenecks;
}

std::vector<std::string> MemoryBandwidthProfiler::GetOptimizationRecommendations() const {
    std::lock_guard<std::mutex> lock(data_mutex_);

    std::vector<std::string> recommendations;

    // Transfer type recommendations
    double h2d_efficiency = GetEfficiencyPercentage(TransferType::HOST_TO_DEVICE);
    if (h2d_efficiency < 75.0) {
        recommendations.push_back("Consider using pinned memory for H2D transfers");
    }

    // Asynchronous transfer recommendations
    if (SupportsUnifiedMemory()) {
        recommendations.push_back("Enable unified memory for improved performance");
    }

    // P2P transfer recommendations
    if (SupportsP2PTransfers()) {
        recommendations.push_back("Consider P2P transfers for multi-GPU operations");
    }

    // Memory access pattern recommendations
    auto l2_metrics = GetMemoryLevelMetrics(MemoryLevel::L2_CACHE);
    if (l2_metrics.cache_hit_rate < 0.8) {
        recommendations.push_back("Optimize memory access patterns for better cache utilization");
    }

    // Bandwidth utilization recommendations
    double current_bandwidth = GetCurrentBandwidthGBPerSec(TransferType::HOST_TO_DEVICE);
    double theoretical_max = GetTheoreticalBandwidthGBPerSec(
        TransferType::HOST_TO_DEVICE,
        MemoryLevel::GLOBAL_MEMORY
    );

    if (theoretical_max > 0 && current_bandwidth / theoretical_max < 0.5) {
        recommendations.push_back("Significant bandwidth optimization potential available");
    }

    return recommendations;
}

json MemoryBandwidthProfiler::GetBandwidthAnalytics() const {
    std::lock_guard<std::mutex> lock(data_mutex_);

    json analytics;

    // Current performance
    analytics["current_performance"] = {
        {"bandwidth_gb_per_sec", GetCurrentBandwidthGBPerSec()},
        {"peak_bandwidth_gb_per_sec", GetPeakBandwidthGBPerSec()},
        {"average_bandwidth_gb_per_sec", GetAverageBandwidthGBPerSec()},
        {"efficiency_percentage", GetEfficiencyPercentage()},
        {"meets_performance_target", IsMeetingPerformanceTarget()}
    };

    // Transfer type breakdown
    analytics["transfer_types"] = json::array();
    for (int i = static_cast<int>(TransferType::HOST_TO_DEVICE);
         i <= static_cast<int>(TransferType::ASYNC_DEVICE_TO_DEVICE); ++i) {
        TransferType type = static_cast<TransferType>(i);
        json type_data = {
            {"type", i},
            {"bandwidth_gb_per_sec", GetAverageBandwidthGBPerSec(type)},
            {"efficiency_percentage", GetEfficiencyPercentage(type)}
        };
        analytics["transfer_types"].push_back(type_data);
    }

    // Memory level metrics
    analytics["memory_levels"] = json::array();
    auto all_metrics = GetAllMemoryLevelMetrics();
    for (const auto& metrics : all_metrics) {
        json level_data = {
            {"level", static_cast<int>(metrics.level)},
            {"name", metrics.level_name},
            {"read_bandwidth_gb_per_sec", metrics.read_bandwidth_gb_per_sec},
            {"write_bandwidth_gb_per_sec", metrics.write_bandwidth_gb_per_sec},
            {"total_bandwidth_gb_per_sec", metrics.total_bandwidth_gb_per_sec},
            {"utilization_percentage", metrics.utilization_percentage},
            {"cache_hit_rate", metrics.cache_hit_rate}
        };
        analytics["memory_levels"].push_back(level_data);
    }

    // Bottlenecks and recommendations
    auto bottlenecks = IdentifyBottlenecks();
    auto recommendations = GetOptimizationRecommendations();

    analytics["analysis"] = {
        {"bottlenecks", bottlenecks},
        {"recommendations", recommendations},
        {"total_samples", static_cast<int>(transfer_history_.size())}
    };

    // Device information
    analytics["device_info"] = {
        {"device_name", device_name_},
        {"compute_capability", compute_capability_},
        {"total_global_memory", total_global_memory_},
        {"l2_cache_size", l2_cache_size_},
        {"shared_memory_per_block", shared_memory_per_block_}
    };

    return analytics;
}

std::string MemoryBandwidthProfiler::GenerateBandwidthReport() const {
    auto analytics = GetBandwidthAnalytics();

    std::stringstream report;
    report << "=== Memory Bandwidth Profiler Report ===\n\n";

    // Device information
    report << "Device: " << analytics["device_info"]["device_name"].get<std::string>() << "\n";
    report << "Compute Capability: " << analytics["device_info"]["compute_capability"].get<int>() << "\n";
    report << "Total Global Memory: " << analytics["device_info"]["total_global_memory"].get<size_t>() / (1024*1024) << " MB\n\n";

    // Current performance
    report << "Current Performance:\n";
    report << "  Bandwidth: " << std::fixed << std::setprecision(2)
           << analytics["current_performance"]["bandwidth_gb_per_sec"].get<double>() << " GB/s\n";
    report << "  Peak Bandwidth: " << std::fixed << std::setprecision(2)
           << analytics["current_performance"]["peak_bandwidth_gb_per_sec"].get<double>() << " GB/s\n";
    report << "  Efficiency: " << std::fixed << std::setprecision(1)
           << analytics["current_performance"]["efficiency_percentage"].get<double>() << "%\n";
    report << "  Meets Target: " << (analytics["current_performance"]["meets_performance_target"].get<bool>() ? "Yes" : "No") << "\n\n";

    // Transfer type breakdown
    report << "Transfer Type Performance:\n";
    for (const auto& type : analytics["transfer_types"]) {
        report << "  Type " << type["type"].get<int>() << ": "
               << std::fixed << std::setprecision(2) << type["bandwidth_gb_per_sec"].get<double>() << " GB/s, "
               << std::fixed << std::setprecision(1) << type["efficiency_percentage"].get<double>() << "% efficiency\n";
    }
    report << "\n";

    // Analysis
    report << "Analysis:\n";
    auto bottlenecks = analytics["analysis"]["bottlenecks"].get<std::vector<std::string>>();
    if (!bottlenecks.empty()) {
        report << "  Bottlenecks:\n";
        for (const auto& bottleneck : bottlenecks) {
            report << "    - " << bottleneck << "\n";
        }
    }

    auto recommendations = analytics["analysis"]["recommendations"].get<std::vector<std::string>>();
    if (!recommendations.empty()) {
        report << "  Recommendations:\n";
        for (const auto& recommendation : recommendations) {
            report << "    - " << recommendation << "\n";
        }
    }

    report << "\nTotal Samples: " << analytics["analysis"]["total_samples"].get<int>() << "\n";

    return report.str();
}

void MemoryBandwidthProfiler::ExportProfileData(const std::string& filename) const {
    auto analytics = GetBandwidthAnalytics();

    std::ofstream file(filename);
    if (file.is_open()) {
        file << analytics.dump(2);
        file.close();
    }
}

void MemoryBandwidthProfiler::ExportBenchmarkData(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(benchmark_mutex_);

    json benchmarks_data = json::array();
    for (const auto& benchmark : benchmark_history_) {
        json benchmark_json;
        benchmark_json["name"] = benchmark.benchmark_name;
        benchmark_json["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            benchmark.timestamp.time_since_epoch()
        ).count();

        // Convert results to JSON
        for (const auto& type_result : benchmark.bandwidth_results) {
            for (const auto& size_result : type_result.second) {
                benchmark_json["bandwidth_results"][std::to_string(static_cast<int>(type_result.first))][std::to_string(size_result.first)] = size_result.second;
            }
        }

        for (const auto& type_result : benchmark.latency_results) {
            for (const auto& size_result : type_result.second) {
                benchmark_json["latency_results"][std::to_string(static_cast<int>(type_result.first))][std::to_string(size_result.first)] = size_result.second;
            }
        }

        benchmark_json["optimal_transfer_type"] = static_cast<int>(benchmark.optimal_transfer_type);
        benchmark_json["optimal_transfer_size"] = benchmark.optimal_transfer_size;
        benchmark_json["peak_performance_bandwidth"] = benchmark.peak_performance_bandwidth;

        benchmarks_data.push_back(benchmark_json);
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        file << benchmarks_data.dump(2);
        file.close();
    }
}

void MemoryBandwidthProfiler::EnableRealTimeMonitoring(bool enabled) {
    std::lock_guard<std::mutex> lock(data_mutex_);

    if (enabled && !real_time_monitoring_enabled_) {
        real_time_monitoring_enabled_ = true;
        shutdown_requested_ = false;
        monitoring_thread_ = std::make_unique<std::thread>(
            &MemoryBandwidthProfiler::MonitoringThreadFunction, this
        );
    } else if (!enabled && real_time_monitoring_enabled_) {
        real_time_monitoring_enabled_ = false;
        shutdown_requested_ = true;
        if (monitoring_thread_ && monitoring_thread_->joinable()) {
            monitoring_thread_->join();
        }
        monitoring_thread_.reset();
    }
}

void MemoryBandwidthProfiler::SetMonitoringInterval(std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    config_.monitoring_interval = interval;
}

std::chrono::milliseconds MemoryBandwidthProfiler::GetMonitoringInterval() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return config_.monitoring_interval;
}

void MemoryBandwidthProfiler::UpdateConfiguration(const ProfilerConfig& config) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    config_ = config;
}

MemoryBandwidthProfiler::ProfilerConfig MemoryBandwidthProfiler::GetCurrentConfiguration() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return config_;
}

MemoryBandwidthProfiler::ErrorType MemoryBandwidthProfiler::GetLastError() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_error_;
}

std::string MemoryBandwidthProfiler::GetErrorMessage() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_error_message_;
}

bool MemoryBandwidthProfiler::AttemptErrorRecovery() {
    std::lock_guard<std::mutex> lock(data_mutex_);

    switch (last_error_) {
        case ErrorType::CUDA_ERROR:
            // Reset CUDA context
            cudaDeviceReset();
            return InitializeDeviceProperties();

        case ErrorType::INITIALIZATION_FAILED:
            // Attempt re-initialization
            return Initialize(current_mode_);

        case ErrorType::DATA_CORRUPTION:
            // Clear corrupted data
            transfer_history_.clear();
            profiles_.clear();
            memory_level_metrics_.clear();
            last_error_ = ErrorType::NONE;
            last_error_message_.clear();
            return true;

        default:
            return false;
    }
}

// Private methods

bool MemoryBandwidthProfiler::InitializeDeviceProperties() {
    cudaError_t result = cudaGetDeviceProperties(&device_properties_, device_id_);
    if (result != cudaSuccess) {
        SetError(ErrorType::CUDA_ERROR, "Failed to get device properties");
        return false;
    }

    compute_capability_ = device_properties_.major * 10 + device_properties_.minor;
    device_name_ = device_properties_.name;
    total_global_memory_ = device_properties_.totalGlobalMem;
    l2_cache_size_ = device_properties_.l2CacheSize;
    shared_memory_per_block_ = device_properties_.sharedMemPerBlock;

    return true;
}

void MemoryBandwidthProfiler::InitializeMemoryLevelMetrics() {
    std::vector<MemoryLevel> levels = {
        MemoryLevel::GLOBAL_MEMORY,
        MemoryLevel::L2_CACHE,
        MemoryLevel::SHARED_MEMORY,
        MemoryLevel::CONSTANT_MEMORY
    };

    for (MemoryLevel level : levels) {
        MemoryLevelMetrics metrics;
        metrics.level = level;
        metrics.level_name = GetMemoryLevelName(level);
        metrics.read_bandwidth_gb_per_sec = 0.0;
        metrics.write_bandwidth_gb_per_sec = 0.0;
        metrics.total_bandwidth_gb_per_sec = 0.0;
        metrics.peak_bandwidth_gb_per_sec = 0.0;
        metrics.average_bandwidth_gb_per_sec = 0.0;
        metrics.total_capacity_bytes = GetMemoryCapacity(level);
        metrics.used_capacity_bytes = 0;
        metrics.available_capacity_bytes = metrics.total_capacity_bytes;
        metrics.utilization_percentage = 0.0;
        metrics.total_reads = 0;
        metrics.total_writes = 0;
        metrics.cache_hits = 0;
        metrics.cache_misses = 0;
        metrics.cache_hit_rate = 0.0;
        metrics.average_read_latency = std::chrono::nanoseconds(0);
        metrics.average_write_latency = std::chrono::nanoseconds(0);
        metrics.min_latency = std::chrono::nanoseconds(0);
        metrics.max_latency = std::chrono::nanoseconds(0);
        metrics.bandwidth_efficiency = 0.0;
        metrics.access_pattern_efficiency = 0.0;
        metrics.memory_divergence_rate = 0.0;
        metrics.last_updated = std::chrono::system_clock::now();

        memory_level_metrics_[level] = metrics;
    }
}

void MemoryBandwidthProfiler::UpdateRealTimeMetrics() {
    UpdateMemoryUtilization();
    CalculateDerivedMetrics();

    // Update current profile if active
    if (profiling_active_) {
        std::lock_guard<std::mutex> profile_lock(profile_mutex_);
        auto it = profiles_.find(current_profile_name_);
        if (it != profiles_.end()) {
            it->second = GetCurrentProfile();
        }
    }
}

void MemoryBandwidthProfiler::CalculateDerivedMetrics() {
    // This method would calculate derived metrics from raw transfer data
    // Implementation depends on specific requirements
}

void MemoryBandwidthProfiler::UpdateMemoryUtilization() {
    // Update memory utilization for all levels
    for (auto& pair : memory_level_metrics_) {
        MemoryLevelMetrics& metrics = pair.second;
        if (metrics.total_capacity_bytes > 0) {
            metrics.utilization_percentage =
                (static_cast<double>(metrics.used_capacity_bytes) /
                 static_cast<double>(metrics.total_capacity_bytes)) * 100.0;
        }
    }
}

double MemoryBandwidthProfiler::CalculateBandwidthGBPerSec(
    size_t bytes,
    std::chrono::nanoseconds duration
) const {
    if (duration.count() == 0) {
        return 0.0;
    }

    double seconds = duration.count() * NANOSECONDS_TO_SECONDS;
    double gigabytes = bytes * BYTES_TO_GB;

    return gigabytes / seconds;
}

double MemoryBandwidthProfiler::CalculateEfficiency(
    double actual_bandwidth,
    double theoretical_bandwidth
) const {
    if (theoretical_bandwidth <= 0.0) {
        return 0.0;
    }

    return (actual_bandwidth / theoretical_bandwidth) * 100.0;
}

double MemoryBandwidthProfiler::GetTheoreticalBandwidthGBPerSec(
    TransferType type,
    MemoryLevel level
) const {
    // Architecture-specific theoretical bandwidth values
    switch (compute_capability_) {
        case 90: // Hopper
            switch (level) {
                case MemoryLevel::HBM3: return 3500.0; // 3.5 TB/s
                case MemoryLevel::L2_CACHE: return 3500.0;
                case MemoryLevel::GLOBAL_MEMORY: return 3500.0;
                default: return 1000.0;
            }

        case 89: // Ada
            switch (level) {
                case MemoryLevel::GDDR6: return 1000.0; // 1 TB/s
                case MemoryLevel::L2_CACHE: return 1000.0;
                case MemoryLevel::GLOBAL_MEMORY: return 1000.0;
                default: return 500.0;
            }

        case 86: // Ampere
            switch (level) {
                case MemoryLevel::GDDR6: return 768.0; // 768 GB/s
                case MemoryLevel::L2_CACHE: return 768.0;
                case MemoryLevel::GLOBAL_MEMORY: return 768.0;
                default: return 400.0;
            }

        case 75: // Turing
            switch (level) {
                case MemoryLevel::GDDR6: return 616.0; // 616 GB/s
                case MemoryLevel::L2_CACHE: return 616.0;
                case MemoryLevel::GLOBAL_MEMORY: return 616.0;
                default: return 300.0;
            }

        default:
            return 500.0; // Conservative default
    }
}

std::string MemoryBandwidthProfiler::GetMemoryLevelName(MemoryLevel level) const {
    switch (level) {
        case MemoryLevel::GLOBAL_MEMORY: return "Global Memory";
        case MemoryLevel::SHARED_MEMORY: return "Shared Memory";
        case MemoryLevel::L2_CACHE: return "L2 Cache";
        case MemoryLevel::L1_CACHE: return "L1 Cache";
        case MemoryLevel::TEXTURE_MEMORY: return "Texture Memory";
        case MemoryLevel::CONSTANT_MEMORY: return "Constant Memory";
        case MemoryLevel::REGISTER_FILE: return "Register File";
        case MemoryLevel::HBM2: return "HBM2";
        case MemoryLevel::HBM3: return "HBM3";
        case MemoryLevel::GDDR6: return "GDDR6";
        default: return "Unknown";
    }
}

std::vector<MemoryLevel> MemoryBandwidthProfiler::GetAvailableMemoryLevels() const {
    std::vector<MemoryLevel> levels = {
        MemoryLevel::GLOBAL_MEMORY,
        MemoryLevel::SHARED_MEMORY,
        MemoryLevel::L2_CACHE,
        MemoryLevel::CONSTANT_MEMORY
    };

    // Add architecture-specific levels
    switch (compute_capability_) {
        case 90:
            levels.push_back(MemoryLevel::HBM3);
            break;
        case 89:
        case 86:
        case 75:
            levels.push_back(MemoryLevel::GDDR6);
            break;
        default:
            break;
    }

    return levels;
}

void MemoryBandwidthProfiler::UpdateMemoryLevelCounters(
    MemoryLevel level,
    bool is_read,
    size_t bytes
) {
    auto it = memory_level_metrics_.find(level);
    if (it != memory_level_metrics_.end()) {
        MemoryLevelMetrics& metrics = it->second;

        if (is_read) {
            metrics.total_reads++;
        } else {
            metrics.total_writes++;
        }

        // Update bandwidth (simplified calculation)
        auto now = std::chrono::system_clock::now();
        auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - metrics.last_updated
        );

        if (time_diff.count() > 0) {
            double bandwidth_gb_per_sec = (bytes * BYTES_TO_GB) / (time_diff.count() / 1000.0);
            metrics.total_bandwidth_gb_per_sec = bandwidth_gb_per_sec;

            if (bandwidth_gb_per_sec > metrics.peak_bandwidth_gb_per_sec) {
                metrics.peak_bandwidth_gb_per_sec = bandwidth_gb_per_sec;
            }

            // Update average
            int total_operations = metrics.total_reads + metrics.total_writes;
            if (total_operations > 0) {
                metrics.average_bandwidth_gb_per_sec =
                    (metrics.average_bandwidth_gb_per_sec * (total_operations - 1) + bandwidth_gb_per_sec) /
                    total_operations;
            }
        }

        metrics.last_updated = now;
    }
}

size_t MemoryBandwidthProfiler::GetMemoryCapacity(MemoryLevel level) const {
    switch (level) {
        case MemoryLevel::GLOBAL_MEMORY:
            return total_global_memory_;
        case MemoryLevel::L2_CACHE:
            return l2_cache_size_;
        case MemoryLevel::SHARED_MEMORY:
            return shared_memory_per_block_;
        case MemoryLevel::CONSTANT_MEMORY:
            return 64 * 1024; // 64KB constant memory
        case MemoryLevel::HBM3:
            return total_global_memory_;
        case MemoryLevel::GDDR6:
            return total_global_memory_;
        default:
            return 0;
    }
}

double MemoryBandwidthProfiler::BenchmarkTransferType(
    TransferType type,
    size_t transfer_size,
    int iterations
) {
    // Simplified benchmark implementation
    // In a real implementation, this would perform actual memory transfers

    void* host_ptr = nullptr;
    void* device_ptr = nullptr;

    // Allocate memory
    if (type == TransferType::HOST_TO_DEVICE || type == TransferType::DEVICE_TO_HOST) {
        host_ptr = malloc(transfer_size);
        if (!host_ptr) {
            SetError(ErrorType::CUDA_ERROR, "Failed to allocate host memory");
            return 0.0;
        }
    }

    cudaError_t result = cudaMalloc(&device_ptr, transfer_size);
    if (result != cudaSuccess) {
        if (host_ptr) free(host_ptr);
        SetError(ErrorType::CUDA_ERROR, "Failed to allocate device memory");
        return 0.0;
    }

    // Warm-up
    for (int i = 0; i < 5; ++i) {
        switch (type) {
            case TransferType::HOST_TO_DEVICE:
                cudaMemcpy(device_ptr, host_ptr, transfer_size, cudaMemcpyHostToDevice);
                break;
            case TransferType::DEVICE_TO_HOST:
                cudaMemcpy(host_ptr, device_ptr, transfer_size, cudaMemcpyDeviceToHost);
                break;
            case TransferType::DEVICE_TO_DEVICE:
                // Device-to-device transfer would need two device pointers
                break;
            default:
                break;
        }
    }

    // Benchmark
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        switch (type) {
            case TransferType::HOST_TO_DEVICE:
                cudaMemcpy(device_ptr, host_ptr, transfer_size, cudaMemcpyHostToDevice);
                break;
            case TransferType::DEVICE_TO_HOST:
                cudaMemcpy(host_ptr, device_ptr, transfer_size, cudaMemcpyDeviceToHost);
                break;
            case TransferType::DEVICE_TO_DEVICE:
                // Simplified - would need proper implementation
                break;
            default:
                break;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    // Cleanup
    if (device_ptr) cudaFree(device_ptr);
    if (host_ptr) free(host_ptr);

    // Calculate bandwidth
    double total_bytes = static_cast<double>(transfer_size * iterations);
    double total_seconds = duration.count() * NANOSECONDS_TO_SECONDS;

    return (total_bytes * BYTES_TO_GB) / total_seconds;
}

std::chrono::nanoseconds MemoryBandwidthProfiler::MeasureTransferLatency(
    TransferType type,
    size_t transfer_size,
    int iterations
) {
    // Simplified latency measurement
    // In a real implementation, this would use CUDA events for accurate timing

    double bandwidth = BenchmarkTransferType(type, transfer_size, iterations);
    if (bandwidth <= 0.0) {
        return std::chrono::nanoseconds(0);
    }

    // Estimate latency from bandwidth (simplified)
    double seconds = (transfer_size * BYTES_TO_GB) / bandwidth;
    return std::chrono::nanoseconds(static_cast<int64_t>(seconds * 1e9));
}

void MemoryBandwidthProfiler::MonitoringThreadFunction() {
    while (!shutdown_requested_) {
        UpdateRealTimeMetrics();

        std::this_thread::sleep_for(config_.monitoring_interval);
    }
}

void MemoryBandwidthProfiler::CleanupOldProfiles() {
    if (profiles_.size() > config_.max_profile_history) {
        // Remove oldest profiles
        auto it = profiles_.begin();
        while (profiles_.size() > config_.max_profile_history && it != profiles_.end()) {
            it = profiles_.erase(it);
        }
    }
}

void MemoryBandwidthProfiler::CleanupOldTransfers() {
    if (transfer_history_.size() > config_.max_transfer_history) {
        // Remove oldest transfers
        size_t remove_count = transfer_history_.size() - config_.max_transfer_history;
        transfer_history_.erase(transfer_history_.begin(),
                               transfer_history_.begin() + remove_count);
    }
}

void MemoryBandwidthProfiler::SetError(ErrorType error, const std::string& message) {
    last_error_ = error;
    last_error_message_ = message;
}

bool MemoryBandwidthProfiler::SupportsP2PTransfers() const {
    return compute_capability_ >= 60; // Pascal and later support P2P
}

bool MemoryBandwidthProfiler::SupportsUnifiedMemory() const {
    return compute_capability_ >= 60; // Pascal and later support unified memory
}

// Factory function
std::unique_ptr<MemoryBandwidthProfiler> CreateMemoryBandwidthProfiler(int device_id) {
    return std::make_unique<MemoryBandwidthProfiler>(device_id);
}

} // namespace performance
} // namespace gpu
} // namespace keycuda