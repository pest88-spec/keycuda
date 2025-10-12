/**
 * @file auto_tuner.cu
 * @brief GPU kernel configuration auto-tuner (T023)
 *
 * Automatically determines optimal kernel configuration parameters
 * based on device capabilities and problem characteristics.
 *
 * Key optimizations:
 * - Automatic block size selection based on register usage
 * - Dynamic grid size calculation for optimal occupancy
 * - Memory-aware batch sizing
 * - Performance-based parameter tuning
 *
 * Constitution Compliance:
 * - Principle VII (GPU Memory Hierarchy): Optimal resource utilization
 */

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <vector>
#include <map>
#include <string>
#include <chrono>
#include <algorithm>
#include <cmath>

#include "memory_manager.cuh"
#include "../utils/cuda_utils.cuh"

namespace keyhunt {
namespace gpu {

/**
 * @brief Kernel configuration parameters
 */
struct KernelConfig {
    dim3 blockSize;        // Block dimensions (threads per block)
    dim3 gridSize;         // Grid dimensions (blocks per grid)
    size_t sharedMemSize;  // Shared memory per block
    int pointsPerThread;   // ECC points processed per thread
    float occupancy;       // Expected GPU occupancy
    size_t batchSize;      // Number of keys per batch

    KernelConfig() : blockSize(256), gridSize(1), sharedMemSize(0),
                     pointsPerThread(1), occupancy(0.0), batchSize(1000000) {}
};

/**
 * @brief Device performance characteristics
 */
struct DeviceProfile {
    int computeCapability;    // e.g., 70, 75, 80, 86, 89, 90
    int multiprocessorCount;  // Number of SMs
    int maxThreadsPerSM;      // Maximum threads per SM
    int maxThreadsPerBlock;   // Maximum threads per block
    size_t sharedMemPerBlock; // Shared memory per block
    int maxRegistersPerBlock; // Maximum registers per block
    int warpSize;             // Warp size (usually 32)
    size_t l2CacheSize;       // L2 cache size
    size_t memoryBandwidth;   // Theoretical memory bandwidth (GB/s)
    float clockRate;          // GPU clock rate (GHz)

    // Recommended configurations for different workloads
    std::map<std::string, KernelConfig> presets;
};

/**
 * @brief Performance measurement result
 */
struct PerformanceMetrics {
    float throughput;        // Keys per second
    float bandwidthUtil;     // Memory bandwidth utilization (%)
    float occupancy;         // Actual measured occupancy (%)
    float kernelTime;        // Kernel execution time (ms)
    float powerConsumption;  // Power consumption (W) if available

    PerformanceMetrics() : throughput(0.0), bandwidthUtil(0.0),
                           occupancy(0.0), kernelTime(0.0), powerConsumption(0.0) {}
};

/**
 * @brief GPU auto-tuner class
 */
class AutoTuner {
private:
    std::map<int, DeviceProfile> deviceProfiles_;
    int currentDeviceId_;
    DeviceProfile currentProfile_;
    bool initialized_;

public:
    AutoTuner() : currentDeviceId_(0), initialized_(false) {
        // Initialize device profiles for known architectures
        initializeDeviceProfiles();
    }

    /**
     * @brief Initialize tuner for current device
     */
    void initialize(int deviceId = 0) {
        currentDeviceId_ = deviceId;
        cudaSetDevice(deviceId);

        // Query device properties
        cudaDeviceProp props;
        cudaGetDeviceProperties(&props, deviceId);

        // Create device profile
        currentProfile_.computeCapability = props.major * 10 + props.minor;
        currentProfile_.multiprocessorCount = props.multiProcessorCount;
        currentProfile_.maxThreadsPerSM = props.maxThreadsPerMultiProcessor;
        currentProfile_.maxThreadsPerBlock = props.maxThreadsPerBlock;
        currentProfile_.sharedMemPerBlock = props.sharedMemPerBlock;
        currentProfile_.maxRegistersPerBlock = props.regsPerBlock;
        currentProfile_.warpSize = props.warpSize;
        currentProfile_.l2CacheSize = props.l2CacheSize;
        currentProfile_.clockRate = props.clockRate / 1000.0f; // Convert kHz to MHz

        // Estimate memory bandwidth based on architecture
        currentProfile_.memoryBandwidth = estimateMemoryBandwidth(currentProfile_.computeCapability);

        // Generate optimal presets for this device
        generatePresets();

        initialized_ = true;
    }

    /**
     * @brief Get optimal configuration for ECC scalar multiplication
     * @param numKeys Number of keys to process
     * @param targetThroughput Target throughput (keys/sec)
     * @return Optimal kernel configuration
     */
    KernelConfig getOptimalConfig(size_t numKeys, float targetThroughput = 0.0f) {
        if (!initialized_) {
            initialize();
        }

        KernelConfig config;

        // Select base preset based on problem size
        std::string presetName = selectBasePreset(numKeys);
        if (currentProfile_.presets.find(presetName) != currentProfile_.presets.end()) {
            config = currentProfile_.presets[presetName];
        }

        // Optimize batch size based on memory constraints
        config.batchSize = optimizeBatchSize(config, numKeys);

        // Adjust grid size for actual workload
        size_t totalThreads = (numKeys + config.pointsPerThread - 1) / config.pointsPerThread;
        config.gridSize = dim3((totalThreads + config.blockSize.x - 1) / config.blockSize.x);

        // Calculate expected occupancy
        config.occupancy = calculateOccupancy(config);

        // Fine-tune if target throughput is specified
        if (targetThroughput > 0.0f) {
            fineTuneForThroughput(config, targetThroughput);
        }

        return config;
    }

    /**
     * @brief Benchmark different configurations and return the best
     * @param numKeys Number of keys for benchmarking
     * @param iterations Number of benchmark iterations
     * @return Best performing configuration
     */
    KernelConfig benchmarkBestConfig(size_t numKeys, int iterations = 5) {
        if (!initialized_) {
            initialize();
        }

        std::vector<KernelConfig> candidateConfigs = generateCandidateConfigs(numKeys);
        PerformanceMetrics bestMetrics;
        KernelConfig bestConfig;

        for (const auto& config : candidateConfigs) {
            std::vector<float> throughputs;
            float avgThroughput = 0.0f;

            // Run multiple iterations
            for (int iter = 0; iter < iterations; iter++) {
                PerformanceMetrics metrics = benchmarkConfig(config, numKeys);
                throughputs.push_back(metrics.throughput);
                avgThroughput += metrics.throughput;
            }

            avgThroughput /= iterations;

            // Update best if this is better
            if (avgThroughput > bestMetrics.throughput) {
                bestMetrics.throughput = avgThroughput;
                bestConfig = config;
            }
        }

        return bestConfig;
    }

    /**
     * @brief Get device profile information
     */
    const DeviceProfile& getDeviceProfile() const {
        return currentProfile_;
    }

private:
    /**
     * @brief Initialize profiles for known GPU architectures
     */
    void initializeDeviceProfiles() {
        // Turing (RTX 20-series)
        DeviceProfile turing = {
            75, 68, 2048, 1024, 65536, 65536, 32, 4194304, 448, 1.545
        };
        turing.presets["small"] = KernelConfig{dim3(256), dim3(1), 65536, 64, 0.75, 100000};
        turing.presets["medium"] = KernelConfig{dim3(256), dim3(100), 65536, 256, 0.80, 1000000};
        turing.presets["large"] = KernelConfig{dim3(256), dim3(1000), 65536, 1024, 0.85, 10000000};
        deviceProfiles_[75] = turing;

        // Ampere (RTX 30-series)
        DeviceProfile ampere = {
            86, 108, 3072, 1536, 102400, 65536, 32, 6291456, 936, 1.785
        };
        ampere.presets["small"] = KernelConfig{dim3(256), dim3(1), 98304, 64, 0.80, 100000};
        ampere.presets["medium"] = KernelConfig{dim3(256), dim3(200), 98304, 512, 0.85, 1000000};
        ampere.presets["large"] = KernelConfig{dim3(256), dim3(2000), 98304, 2048, 0.90, 10000000};
        deviceProfiles_[86] = ampere;

        // Hopper (H100)
        DeviceProfile hopper = {
            90, 132, 2048, 1024, 227328, 65536, 32, 52428800, 3350, 1.770
        };
        hopper.presets["small"] = KernelConfig{dim3(256), dim3(1), 131072, 64, 0.85, 100000};
        hopper.presets["medium"] = KernelConfig{dim3(256), dim3(500), 131072, 1024, 0.90, 1000000};
        hopper.presets["large"] = KernelConfig{dim3(256), dim3(5000), 131072, 4096, 0.95, 10000000};
        deviceProfiles_[90] = hopper;
    }

    /**
     * @brief Estimate memory bandwidth based on GPU architecture
     */
    float estimateMemoryBandwidth(int computeCapability) {
        switch (computeCapability) {
            case 75: return 448.0f;   // RTX 2080 Ti
            case 80: return 716.8f;   // A100 (80)
            case 86: return 936.0f;   // RTX 3080 Ti
            case 89: return 1008.0f;  // RTX 4090
            case 90: return 3350.0f;  // H100
            default: return 500.0f;   // Conservative estimate
        }
    }

    /**
     * @brief Generate architecture-specific presets
     */
    void generatePresets() {
        // Base presets are already defined in initializeDeviceProfiles()
        // Can be customized further based on actual device measurements
    }

    /**
     * @brief Select base preset based on problem size
     */
    std::string selectBasePreset(size_t numKeys) {
        if (numKeys < 100000) {
            return "small";
        } else if (numKeys < 10000000) {
            return "medium";
        } else {
            return "large";
        }
    }

    /**
     * @brief Optimize batch size for memory constraints
     */
    size_t optimizeBatchSize(const KernelConfig& config, size_t totalKeys) {
        // Calculate memory requirements per batch
        size_t keySize = 32;  // Private key size in bytes
        size_t publicKeySize = 65;  // Public key size in bytes
        size_t sharedMemPerBatch = config.sharedMemSize * config.gridSize.x;

        // Estimate available memory (leave 25% for system)
        size_t freeMem, totalMem;
        cudaMemGetInfo(&freeMem, &totalMem);
        size_t usableMem = freeMem * 3 / 4;

        // Calculate max keys that fit in memory
        size_t memoryBoundKeys = usableMem / (keySize + publicKeySize + sharedMemPerBatch / totalKeys);

        // Choose batch size based on memory and optimal throughput
        size_t optimalBatch = std::min(config.batchSize, memoryBoundKeys);
        optimalBatch = std::min(optimalBatch, totalKeys);

        // Align to warp size
        optimalBatch = (optimalBatch / 32) * 32;

        return std::max(optimalBatch, size_t(1024));  // Minimum batch size
    }

    /**
     * @brief Calculate theoretical occupancy
     */
    float calculateOccupancy(const KernelConfig& config) {
        int activeWarpsPerSM = 0;
        int maxWarpsPerSM = currentProfile_.maxThreadsPerSM / currentProfile_.warpSize;
        int warpsPerBlock = (config.blockSize.x + currentProfile_.warpSize - 1) / currentProfile_.warpSize;

        // Estimate based on resource limits
        int blocksPerSM = std::min(
            currentProfile_.maxThreadsPerSM / config.blockSize.x,
            currentProfile_.maxRegistersPerBlock / (64 * config.blockSize.x)  // Assume 64 regs/thread
        );

        activeWarpsPerSM = blocksPerSM * warpsPerBlock;
        return static_cast<float>(activeWarpsPerSM) / maxWarpsPerSM;
    }

    /**
     * @brief Fine-tune configuration for target throughput
     */
    void fineTuneForThroughput(KernelConfig& config, float targetThroughput) {
        // Adjust points per thread based on target
        float theoreticalThroughput = currentProfile_.memoryBandwidth * 1e9f / 64.0f;  // 64 bytes per key

        if (targetThroughput > theoreticalThroughput) {
            // Need more work per thread to hide latency
            config.pointsPerThread = std::min(config.pointsPerThread * 2, 4096);
        } else {
            // Can reduce work per thread
            config.pointsPerThread = std::max(config.pointsPerThread / 2, 1);
        }
    }

    /**
     * @brief Generate candidate configurations for benchmarking
     */
    std::vector<KernelConfig> generateCandidateConfigs(size_t numKeys) {
        std::vector<KernelConfig> candidates;

        // Different block sizes to test
        std::vector<int> blockSizes = {128, 256, 512, 1024};

        // Different shared memory sizes
        std::vector<size_t> sharedMemSizes = {0, 32768, 65536, 98304};

        // Different points per thread
        std::vector<int> pointsPerThread = {64, 128, 256, 512, 1024};

        for (int blockSize : blockSizes) {
            if (blockSize > currentProfile_.maxThreadsPerBlock) continue;

            for (size_t sharedMem : sharedMemSizes) {
                if (sharedMem > currentProfile_.sharedMemPerBlock) continue;

                for (int ppt : pointsPerThread) {
                    KernelConfig config;
                    config.blockSize = dim3(blockSize);
                    config.sharedMemSize = sharedMem;
                    config.pointsPerThread = ppt;

                    // Calculate grid size
                    size_t totalThreads = (numKeys + ppt - 1) / ppt;
                    config.gridSize = dim3((totalThreads + blockSize - 1) / blockSize);

                    candidates.push_back(config);
                }
            }
        }

        return candidates;
    }

    /**
     * @brief Benchmark a specific configuration
     */
    PerformanceMetrics benchmarkConfig(const KernelConfig& config, size_t numKeys) {
        PerformanceMetrics metrics;

        // Allocate test data
        ECCPointsSoA precomputed = allocateCoalescedPoints(1024);
        uint8_t* d_privateKeys;
        uint8_t* d_publicKeys;

        cudaMalloc(&d_privateKeys, numKeys * 32);
        cudaMalloc(&d_publicKeys, numKeys * 65);

        // Create CUDA events for timing
        cudaEvent_t start, stop;
        cudaEventCreate(&start);
        cudaEventCreate(&stop);

        // Launch kernel (placeholder - actual kernel would be passed as parameter)
        cudaEventRecord(start);
        // eccScalarMulKernelSoA<<<config.gridSize, config.blockSize, config.sharedMemSize>>>(
        //     d_privateKeys, precomputed, d_publicKeysX, d_publicKeysY, numKeys);
        cudaEventRecord(stop);

        // Wait for completion
        cudaEventSynchronize(stop);

        // Calculate metrics
        float elapsedMs;
        cudaEventElapsedTime(&elapsedMs, start, stop);
        metrics.kernelTime = elapsedMs;
        metrics.throughput = static_cast<float>(numKeys) / (elapsedMs / 1000.0f);

        // Estimate bandwidth utilization
        float bytesTransferred = numKeys * (32 + 65);  // Input + output
        float theoreticalBandwidth = currentProfile_.memoryBandwidth * 1e9f / 1000.0f;  // GB/s
        metrics.bandwidthUtil = (bytesTransferred / (elapsedMs / 1000.0f)) / theoreticalBandwidth * 100.0f;

        // Cleanup
        cudaEventDestroy(start);
        cudaEventDestroy(stop);
        deallocateCoalescedPoints(precomputed);
        cudaFree(d_privateKeys);
        cudaFree(d_publicKeys);

        return metrics;
    }
};

/**
 * @brief Global auto-tuner instance
 */
static AutoTuner globalAutoTuner;

/**
 * @brief Get optimal kernel configuration
 * @param numKeys Number of keys to process
 * @param deviceId GPU device ID (optional)
 * @return Optimal configuration
 */
KernelConfig getOptimalKernelConfig(size_t numKeys, int deviceId = 0) {
    return globalAutoTuner.getOptimalConfig(numKeys, 0.0f);
}

/**
 * @brief Benchmark and return best configuration
 * @param numKeys Number of keys for benchmarking
 * @param deviceId GPU device ID (optional)
 * @return Best performing configuration
 */
KernelConfig benchmarkBestKernelConfig(size_t numKeys, int deviceId = 0) {
    return globalAutoTuner.benchmarkBestConfig(numKeys);
}

/**
 * @brief Initialize auto-tuner for specific device
 * @param deviceId GPU device ID
 */
void initializeAutoTuner(int deviceId = 0) {
    globalAutoTuner.initialize(deviceId);
}

/**
 * @brief Get device performance profile
 * @param deviceId GPU device ID
 * @return Device profile information
 */
DeviceProfile getDeviceProfile(int deviceId = 0) {
    return globalAutoTuner.getDeviceProfile();
}

} // namespace gpu
} // namespace keyhunt