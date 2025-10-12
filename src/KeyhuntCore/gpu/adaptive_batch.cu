/**
 * @file adaptive_batch.cu
 * @brief Adaptive batch sizing for optimal GPU utilization (T056)
 *
 * Dynamically adjusts batch sizes based on:
 * - Current GPU utilization
 * - Memory availability
 * - Performance metrics
 * - Target throughput requirements
 *
 * Optimizes for:
 * - Maximum sustained throughput
 * - Efficient memory usage
 * - Stable performance across different workloads
 */

#include <cuda_runtime.h>
#include <vector>
#include <deque>
#include <algorithm>
#include <numeric>
#include <cmath>

#include <thrust/device_vector.h>
#include <thrust/reduce.h>
#include <thrust/transform.h>

#include "auto_tuner.cuh"
#include "../utils/cuda_utils.cuh"

namespace keyhunt {
namespace gpu {

/**
 * @brief Performance history for adaptive decisions
 */
struct PerformanceHistory {
    std::deque<float> throughputHistory;    // Recent throughput measurements
    std::deque<float> memoryUsageHistory;   // Recent memory usage
    std::deque<float> kernelTimeHistory;    // Recent kernel execution times
    std::deque<size_t> batchSizeHistory;    // Recent batch sizes tried

    static constexpr size_t MAX_HISTORY = 10;  // Keep last 10 measurements

    void addThroughput(float throughput) {
        throughputHistory.push_back(throughput);
        if (throughputHistory.size() > MAX_HISTORY) {
            throughputHistory.pop_front();
        }
    }

    void addMemoryUsage(float usage) {
        memoryUsageHistory.push_back(usage);
        if (memoryUsageHistory.size() > MAX_HISTORY) {
            memoryUsageHistory.pop_front();
        }
    }

    void addKernelTime(float time) {
        kernelTimeHistory.push_back(time);
        if (kernelTimeHistory.size() > MAX_HISTORY) {
            kernelTimeHistory.pop_front();
        }
    }

    void addBatchSize(size_t batchSize) {
        batchSizeHistory.push_back(batchSize);
        if (batchSizeHistory.size() > MAX_HISTORY) {
            batchSizeHistory.pop_front();
        }
    }

    float getAverageThroughput() const {
        if (throughputHistory.empty()) return 0.0f;

        // Use Thrust reduce for parallel computation (T030)
        thrust::device_vector<float> d_throughput(throughputHistory.begin(), throughputHistory.end());
        float sum = thrust::reduce(d_throughput.begin(), d_throughput.end(), 0.0f, thrust::plus<float>());
        return sum / throughputHistory.size();
    }

    float getAverageKernelTime() const {
        if (kernelTimeHistory.empty()) return 0.0f;

        // Use Thrust reduce for parallel computation (T030)
        thrust::device_vector<float> d_kernelTime(kernelTimeHistory.begin(), kernelTimeHistory.end());
        float sum = thrust::reduce(d_kernelTime.begin(), d_kernelTime.end(), 0.0f, thrust::plus<float>());
        return sum / kernelTimeHistory.size();
    }

    float getThroughputVariance() const {
        if (throughputHistory.size() < 2) return 0.0f;
        float mean = getAverageThroughput();

        // Use Thrust transform + reduce for parallel variance computation (T030)
        thrust::device_vector<float> d_throughput(throughputHistory.begin(), throughputHistory.end());
        thrust::device_vector<float> d_squared_diffs(throughputHistory.size());

        // Transform: compute squared differences in parallel
        thrust::transform(d_throughput.begin(), d_throughput.end(),
                         d_squared_diffs.begin(),
                         [mean] __host__ __device__ (float t) {
                             return (t - mean) * (t - mean);
                         });

        // Reduce: sum squared differences in parallel
        float variance_sum = thrust::reduce(d_squared_diffs.begin(), d_squared_diffs.end(), 0.0f, thrust::plus<float>());
        return variance_sum / throughputHistory.size();
    }

    void clear() {
        throughputHistory.clear();
        memoryUsageHistory.clear();
        kernelTimeHistory.clear();
        batchSizeHistory.clear();
    }
};

/**
 * @brief Adaptive batch sizing parameters
 */
struct AdaptiveBatchParams {
    size_t minBatchSize;          // Minimum batch size
    size_t maxBatchSize;          // Maximum batch size
    float targetUtilization;      // Target GPU utilization (0-1)
    float memoryThreshold;        // Memory usage threshold (0-1)
    float adaptationRate;         // How aggressively to adapt (0-1)
    int stableWindow;             // Number of measurements for stability
    float throughputVarianceThreshold;  // Max acceptable variance

    AdaptiveBatchParams() : minBatchSize(10000), maxBatchSize(10000000),
                           targetUtilization(0.85f), memoryThreshold(0.80f,
                           adaptationRate(0.1f), stableWindow(5),
                           throughputVarianceThreshold(0.1f) {}
};

/**
 * @brief Adaptive batch manager
 */
class AdaptiveBatchManager {
private:
    AdaptiveBatchParams params_;
    PerformanceHistory history_;
    size_t currentBatchSize_;
    float currentThroughput_;
    bool isStable_;
    int stabilityCounter_;
    size_t totalAdaptations_;
    uint64_t totalKeysProcessed_;

    // Performance targets
    float targetThroughput_;
    float minAcceptableThroughput_;

public:
    /**
     * @brief Constructor
     */
    AdaptiveBatchManager() : currentBatchSize_(1000000), currentThroughput_(0.0f),
                            isStable_(false), stabilityCounter_(0),
                            totalAdaptations_(0), totalKeysProcessed_(0),
                            targetThroughput_(0.0f), minAcceptableThroughput_(0.0f) {
        // Query device capabilities to set sensible defaults
        initializeDeviceSpecificParams();
    }

    /**
     * @brief Initialize with custom parameters
     */
    void initialize(const AdaptiveBatchParams& params) {
        params_ = params;
        currentBatchSize_ = std::min(params_.maxBatchSize,
                                    std::max(params_.minBatchSize, currentBatchSize_));
        history_.clear();
        isStable_ = false;
        stabilityCounter_ = 0;
    }

    /**
     * @brief Get recommended batch size for next iteration
     */
    size_t getNextBatchSize() {
        if (!isStable_) {
            // Not stable yet, use conservative size
            return currentBatchSize_;
        }

        // Check if we should adapt based on recent performance
        if (shouldAdapt()) {
            adaptBatchSize();
        }

        return currentBatchSize_;
    }

    /**
     * @brief Report performance metrics for completed batch
     */
    void reportBatchPerformance(size_t batchSize, float kernelTime, float throughput) {
        // Add to history
        history_.addBatchSize(batchSize);
        history_.addKernelTime(kernelTime);
        history_.addThroughput(throughput);
        currentThroughput_ = throughput;
        totalKeysProcessed_ += batchSize;

        // Check memory usage
        size_t freeMem, totalMem;
        cudaMemGetInfo(&freeMem, &totalMem);
        float memoryUsage = 1.0f - (static_cast<float>(freeMem) / totalMem);
        history_.addMemoryUsage(memoryUsage);

        // Update stability assessment
        updateStability();

        // Print adaptation info if adapted
        static size_t lastReportedSize = 0;
        if (lastReportedSize != currentBatchSize_) {
            printf("Adapted batch size: %zu -> %zu (throughput: %.2f Mkeys/s)\n",
                   lastReportedSize, currentBatchSize_, throughput / 1e6f);
            lastReportedSize = currentBatchSize_;
        }
    }

    /**
     * @brief Set target throughput
     */
    void setTargetThroughput(float targetThroughput, float minAcceptable = 0.0f) {
        targetThroughput_ = targetThroughput;
        minAcceptableThroughput_ = minAcceptable;
    }

    /**
     * @brief Get current statistics
     */
    void getStatistics(float& avgThroughput, float& throughputVariance,
                       size_t& adaptationCount, float& efficiency) const {
        avgThroughput = history_.getAverageThroughput();
        throughputVariance = history_.getThroughputVariance();
        adaptationCount = totalAdaptations_;

        // Calculate efficiency relative to target
        if (targetThroughput_ > 0.0f) {
            efficiency = avgThroughput / targetThroughput_;
        } else {
            efficiency = 1.0f;
        }
    }

    /**
     * @brief Reset adaptive state
     */
    void reset() {
        history_.clear();
        isStable_ = false;
        stabilityCounter_ = 0;
        totalAdaptations_ = 0;
        totalKeysProcessed_ = 0;
    }

private:
    /**
     * @brief Initialize device-specific parameters
     */
    void initializeDeviceSpecificParams() {
        cudaDeviceProp props;
        cudaGetDeviceProperties(&props, 0);

        // Adjust batch size limits based on GPU memory
        size_t freeMem, totalMem;
        cudaMemGetInfo(&freeMem, &totalMem);

        // Estimate memory per key
        size_t perKey = 32 + 65 + 20 + 8 * sizeof(uint32_t) * 2;  // Conservative estimate
        size_t maxKeys = (freeMem * 3 / 4) / perKey;  // Use 75% of free memory

        params_.minBatchSize = std::max(size_t(10000), maxKeys / 1000);
        params_.maxBatchSize = std::min(size_t(10000000), maxKeys);

        // Adjust adaptation rate based on architecture
        if (props.major >= 8) {  // Ampere and newer
            params_.adaptationRate = 0.15f;  // More aggressive
            params_.stableWindow = 3;
        } else {  // Older architectures
            params_.adaptationRate = 0.05f;  // More conservative
            params_.stableWindow = 5;
        }

        currentBatchSize_ = params_.maxBatchSize / 4;  // Start at 25% of max
    }

    /**
     * @brief Check if adaptation is needed
     */
    bool shouldAdapt() {
        if (history_.throughputHistory.size() < params_.stableWindow) {
            return false;  // Need more data
        }

        // Check if throughput is stable
        float variance = history_.getThroughputVariance();
        float mean = history_.getAverageThroughput();
        float cv = (mean > 0) ? variance / (mean * mean) : 0.0f;  // Coefficient of variation

        if (cv > params_.throughputVarianceThreshold) {
            return true;  // Too variable, need to adapt
        }

        // Check if we're meeting target
        if (targetThroughput_ > 0.0f && mean < targetThroughput_ * 0.9f) {
            return true;  // Below target, try to improve
        }

        // Check memory pressure
        if (!history_.memoryUsageHistory.empty()) {
            float memUsage = history_.memoryUsageHistory.back();
            if (memUsage > params_.memoryThreshold) {
                return true;  // Memory pressure, reduce batch size
            }
        }

        return false;
    }

    /**
     * @brief Adapt batch size based on performance trends
     */
    void adaptBatchSize() {
        if (history_.throughputHistory.size() < 2) return;

        // Calculate performance trend
        float recentAvg = 0.0f;
        int recentCount = std::min(3, static_cast<int>(history_.throughputHistory.size()));
        for (int i = 0; i < recentCount; i++) {
            recentAvg += history_.throughputHistory[history_.throughputHistory.size() - 1 - i];
        }
        recentAvg /= recentCount;

        float olderAvg = 0.0f;
        int olderCount = std::min(3, static_cast<int>(history_.throughputHistory.size() - recentCount));
        if (olderCount > 0) {
            for (int i = 0; i < olderCount; i++) {
                olderAvg += history_.throughputHistory[history_.throughputHistory.size() - 1 - recentCount - i];
            }
            olderAvg /= olderCount;
        }

        // Determine adaptation direction
        float trend = recentAvg - olderAvg;
        size_t newBatchSize = currentBatchSize_;

        // Adapt based on trend
        if (trend > 0) {
            // Improving, try larger batch
            newBatchSize = static_cast<size_t>(currentBatchSize_ * (1.0f + params_.adaptationRate));
        } else if (trend < 0) {
            // Declining, try smaller batch
            newBatchSize = static_cast<size_t>(currentBatchSize_ * (1.0f - params_.adaptationRate));
        }

        // Check memory pressure
        if (!history_.memoryUsageHistory.empty()) {
            float memUsage = history_.memoryUsageHistory.back();
            if (memUsage > params_.memoryThreshold) {
                // Reduce batch size due to memory pressure
                newBatchSize = static_cast<size_t>(newBatchSize * 0.8f);
            } else if (memUsage < params_.memoryThreshold * 0.6f) {
                // Plenty of memory, can increase batch size
                newBatchSize = static_cast<size_t>(newBatchSize * 1.1f);
            }
        }

        // Apply constraints
        newBatchSize = std::max(params_.minBatchSize, newBatchSize);
        newBatchSize = std::min(params_.maxBatchSize, newBatchSize);

        // Align to warp size
        newBatchSize = (newBatchSize / 32) * 32;

        // Update if changed
        if (newBatchSize != currentBatchSize_) {
            currentBatchSize_ = newBatchSize;
            totalAdaptations_++;
            isStable_ = false;
            stabilityCounter_ = 0;
        }
    }

    /**
     * @brief Update stability assessment
     */
    void updateStability() {
        if (history_.throughputHistory.size() < params_.stableWindow) {
            return;
        }

        float variance = history_.getThroughputVariance();
        float mean = history_.getAverageThroughput();
        float cv = (mean > 0) ? variance / (mean * mean) : 0.0f;

        if (cv < params_.throughputVarianceThreshold) {
            stabilityCounter_++;
            if (stabilityCounter_ >= params_.stableWindow) {
                isStable_ = true;
            }
        } else {
            stabilityCounter_ = 0;
            isStable_ = false;
        }
    }
};

/**
 * @brief Global adaptive batch manager instance
 */
static AdaptiveBatchManager globalAdaptiveBatchManager;

/**
 * @brief Initialize adaptive batch manager
 */
void initializeAdaptiveBatch(const AdaptiveBatchParams& params = AdaptiveBatchParams()) {
    globalAdaptiveBatchManager.initialize(params);
}

/**
 * @brief Get next batch size
 */
size_t getNextAdaptiveBatchSize() {
    return globalAdaptiveBatchManager.getNextBatchSize();
}

/**
 * @brief Report batch performance
 */
void reportBatchPerformance(size_t batchSize, float kernelTime, float throughput) {
    globalAdaptiveBatchManager.reportBatchPerformance(batchSize, kernelTime, throughput);
}

/**
 * @brief Set target throughput
 */
void setTargetThroughput(float targetThroughput, float minAcceptable = 0.0f) {
    globalAdaptiveBatchManager.setTargetThroughput(targetThroughput, minAcceptable);
}

} // namespace gpu
} // namespace keyhunt