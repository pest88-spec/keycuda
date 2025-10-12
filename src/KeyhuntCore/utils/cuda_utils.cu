/**
 * @file cuda_utils.cu
 * @brief CUDA utility functions for device management and error checking
 *
 * Implements CUDA device query, error checking wrappers, and hardware metadata
 * collection for GPU performance optimization feature (003-gpu-1-28).
 *
 * Constitution Compliance:
 * - Principle I (Determinism): Device queries are deterministic for given hardware
 * - Principle IV (Zero Regression): Error checking ensures failures are caught early
 */

#include <cuda_runtime.h>
#include <cuda.h>
#include <sstream>
#include <stdexcept>
#include <ctime>
#include <iomanip>

// Forward declare HardwareMetadata structure (defined in data model)
struct HardwareMetadata {
    std::string gpuModel;
    uint32_t gpuCount;
    std::string computeCapability;
    uint32_t smCount;
    size_t totalMemoryBytes;
    uint32_t clockSpeedMHz;
    uint32_t memoryClockSpeedMHz;
    std::string driverVersion;
    std::string cudaRuntimeVersion;
    std::string cudaDriverVersion;
    std::string firmwareVersion;
    std::string thermalState;  // "Normal", "Throttling", "Critical"
    uint32_t powerLimitWatts;
    std::string timestamp;
};

namespace keyhunt {
namespace utils {

/**
 * @brief Check CUDA error and throw exception if error detected
 * @param err CUDA error code to check
 * @param msg Custom error message to include in exception
 * @throws std::runtime_error if CUDA error detected
 */
inline void checkCudaError(cudaError_t err, const char* msg) {
    if (err != cudaSuccess) {
        std::ostringstream oss;
        oss << msg << ": " << cudaGetErrorString(err)
            << " (error code: " << err << ")";
        throw std::runtime_error(oss.str());
    }
}

/**
 * @brief Wrapper for cudaDeviceSynchronize with error checking
 * @throws std::runtime_error if synchronization fails
 */
inline void safeCudaDeviceSynchronize() {
    cudaError_t err = cudaDeviceSynchronize();
    checkCudaError(err, "cudaDeviceSynchronize failed");
}

/**
 * @brief Get current timestamp in ISO 8601 format (UTC)
 * @return ISO 8601 timestamp string
 */
std::string getCurrentTimestampUTC() {
    std::time_t now = std::time(nullptr);
    std::tm* utc = std::gmtime(&now);

    std::ostringstream oss;
    oss << std::put_time(utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/**
 * @brief Query CUDA device properties and return hardware metadata
 * @param deviceId GPU device ID (default: 0)
 * @return HardwareMetadata entity with complete hardware information
 * @throws std::runtime_error if device query fails
 *
 * Queries:
 * - GPU model name, compute capability, SM count
 * - Total memory, clock speeds (GPU and memory)
 * - Driver version, CUDA runtime version, CUDA driver API version
 * - Firmware version (if available)
 * - Thermal state (requires nvidia-smi for advanced features)
 * - Power limit (if available)
 */
HardwareMetadata getCudaDeviceProperties(int deviceId = 0) {
    HardwareMetadata metadata;

    // Query device count
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    checkCudaError(err, "Failed to query CUDA device count");

    if (deviceCount == 0) {
        throw std::runtime_error("No CUDA devices found");
    }

    if (deviceId < 0 || deviceId >= deviceCount) {
        std::ostringstream oss;
        oss << "Invalid device ID: " << deviceId
            << " (available: 0-" << (deviceCount - 1) << ")";
        throw std::runtime_error(oss.str());
    }

    metadata.gpuCount = static_cast<uint32_t>(deviceCount);

    // Set current device
    err = cudaSetDevice(deviceId);
    checkCudaError(err, "Failed to set CUDA device");

    // Query device properties
    cudaDeviceProp prop;
    err = cudaGetDeviceProperties(&prop, deviceId);
    checkCudaError(err, "Failed to get device properties");

    // Populate metadata fields
    metadata.gpuModel = std::string(prop.name);

    // Compute capability (major.minor)
    std::ostringstream ccOss;
    ccOss << prop.major << "." << prop.minor;
    metadata.computeCapability = ccOss.str();

    metadata.smCount = static_cast<uint32_t>(prop.multiProcessorCount);
    metadata.totalMemoryBytes = prop.totalGlobalMem;
    metadata.clockSpeedMHz = static_cast<uint32_t>(prop.clockRate / 1000);  // Convert kHz to MHz
    metadata.memoryClockSpeedMHz = static_cast<uint32_t>(prop.memoryClockRate / 1000);

    // Query driver version
    int driverVersion = 0;
    err = cudaDriverGetVersion(&driverVersion);
    checkCudaError(err, "Failed to get CUDA driver version");

    std::ostringstream driverOss;
    driverOss << (driverVersion / 1000) << "." << ((driverVersion % 1000) / 10);
    metadata.cudaDriverVersion = driverOss.str();

    // Query runtime version
    int runtimeVersion = 0;
    err = cudaRuntimeGetVersion(&runtimeVersion);
    checkCudaError(err, "Failed to get CUDA runtime version");

    std::ostringstream runtimeOss;
    runtimeOss << (runtimeVersion / 1000) << "." << ((runtimeVersion % 1000) / 10);
    metadata.cudaRuntimeVersion = runtimeOss.str();

    // Driver version string (for consistency with data model)
    metadata.driverVersion = metadata.cudaDriverVersion;

    // Firmware version (not directly available via CUDA Runtime API)
    // Would require nvidia-ml library (NVML) for detailed firmware info
    metadata.firmwareVersion = "N/A (requires NVML)";

    // Thermal state (simplified check - would need NVML for accurate detection)
    // For now, assume Normal state
    metadata.thermalState = "Normal";

    // Power limit (not available via CUDA Runtime API alone)
    // Would require NVML library for accurate power limit query
    metadata.powerLimitWatts = 0;  // 0 indicates unavailable

    // Timestamp
    metadata.timestamp = getCurrentTimestampUTC();

    return metadata;
}

/**
 * @brief Print hardware metadata to console (for debugging)
 * @param metadata Hardware metadata to print
 */
void printHardwareMetadata(const HardwareMetadata& metadata) {
    std::cout << "=== CUDA Hardware Metadata ===" << std::endl;
    std::cout << "GPU Model: " << metadata.gpuModel << std::endl;
    std::cout << "GPU Count: " << metadata.gpuCount << std::endl;
    std::cout << "Compute Capability: " << metadata.computeCapability << std::endl;
    std::cout << "SM Count: " << metadata.smCount << std::endl;
    std::cout << "Total Memory: " << (metadata.totalMemoryBytes / (1024.0 * 1024.0 * 1024.0))
              << " GB" << std::endl;
    std::cout << "GPU Clock: " << metadata.clockSpeedMHz << " MHz" << std::endl;
    std::cout << "Memory Clock: " << metadata.memoryClockSpeedMHz << " MHz" << std::endl;
    std::cout << "Driver Version: " << metadata.driverVersion << std::endl;
    std::cout << "CUDA Runtime: " << metadata.cudaRuntimeVersion << std::endl;
    std::cout << "CUDA Driver: " << metadata.cudaDriverVersion << std::endl;
    std::cout << "Firmware: " << metadata.firmwareVersion << std::endl;
    std::cout << "Thermal State: " << metadata.thermalState << std::endl;
    std::cout << "Power Limit: " << metadata.powerLimitWatts << " W" << std::endl;
    std::cout << "Timestamp: " << metadata.timestamp << std::endl;
    std::cout << "===============================" << std::endl;
}

} // namespace utils
} // namespace keyhunt
