/**
 * @file json_serializer.h
 * @brief JSON serialization for data model entities with SHA-256 digest support
 *
 * Provides serialization/deserialization for GPU performance benchmarking entities.
 * Implements tamper-proof manifests using SHA-256 digests (Constitution Principle V).
 *
 * Entities supported:
 * - GPUKernelConfiguration
 * - PerformanceBaseline
 * - BenchmarkResult
 * - HardwareMetadata
 * - TelemetrySample
 * - ValidationResult
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace keyhunt {
namespace utils {

// Forward declarations for entities (from data-model.md)
enum class GPUArchitecture { Turing, Ampere, Hopper };
enum class BaselineStatus { Active, Deprecated, Superseded };
enum class TestStatus { Pass, Fail, Quarantined };
enum class OccupancyLimit { Registers, SharedMemory, BlockSize, None };
enum class ThermalState { Normal, Throttling, Critical };
enum class DebtMarker { TODO, FIXME, PLACEHOLDER, HACK, STUB };
enum class DebtCategory { IncompleteImplementation, SuboptimalPattern, StubFunction };
enum class Priority { P1, P2, P3 };
enum class DebtStatus { Open, InProgress, Completed, WontFix };

// Core entities from data-model.md
struct GPUKernelConfiguration {
    std::string kernelName;
    GPUArchitecture gpuArchitecture;
    uint32_t gridDimX, gridDimY, gridDimZ;
    uint32_t blockDimX, blockDimY, blockDimZ;
    uint32_t pointsPerThread;
    size_t sharedMemoryBytes;
    uint32_t registerBudget;
    int streamId;
};

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
    ThermalState thermalState;
    uint32_t powerLimitWatts;
    std::string timestamp;
};

struct BaselineComparison {
    std::string baselineId;
    double baselineThroughput;
    double throughputDelta;
    double throughputDeltaPercent;
    bool isRegression;
};

struct PerformanceBaseline {
    std::string baselineId;
    std::string gpuModel;
    std::string computeCapability;
    std::string driverVersion;
    std::string cudaRuntimeVersion;
    GPUKernelConfiguration kernelConfiguration;
    double targetThroughput;
    double medianThroughput;
    double gpuUtilizationPercent;
    double memoryBandwidthPercent;
    double occupancyPercent;
    uint32_t registerUsage;
    std::string establishedDate;
    std::string sha256Digest;
    BaselineStatus status;
};

struct BenchmarkResult {
    std::string resultId;
    std::string featureBranch;
    std::string commitSha;
    std::string gpuModel;
    HardwareMetadata hardwareMetadata;
    GPUKernelConfiguration kernelConfiguration;
    uint32_t testDurationSeconds;
    uint32_t sampleCount;
    std::vector<double> throughputSamples;
    double medianThroughput;
    double meanThroughput;
    double p95Throughput;
    std::vector<double> gpuUtilizationSamples;
    std::vector<double> memoryBandwidthSamples;
    double validationPassRate;
    std::vector<std::string> thermalEvents;
    BaselineComparison baselineComparison;
    std::string profilingMetrics;  // Simplified - would be ProfilingReport object in full implementation
    TestStatus passFailStatus;
    std::string sha256Digest;
    std::string timestamp;
};

struct TelemetrySample {
    std::string timestamp;
    uint64_t batchId;
    std::string keyspaceChunkStart;
    uint64_t keysProcessed;
    double throughputGkeysPerSec;
    double gpuUtilizationPercent;
    double memoryBandwidthPercent;
    uint32_t validationErrors;
    double checkpointLatencyMs;  // optional
    std::vector<std::string> alertEvents;
};

struct FailedTest {
    uint32_t testCaseId;
    std::string inputPrivateKey;
    std::string cpuResult;
    std::string gpuResult;
    double relativeError;
};

struct ValidationResult {
    std::string validationId;
    std::string kernelName;
    std::string cpuReferenceImplementation;
    uint32_t testCaseCount;
    uint32_t passedCount;
    uint32_t failedCount;
    double passRate;
    double maxRelativeError;
    double meanRelativeError;
    std::vector<FailedTest> failedTestSamples;
    uint64_t randomSeed;
    std::string timestamp;
};

// Serialization functions
std::string computeSHA256Digest(const std::string& jsonString);
std::string computeSHA256DigestOfJSON(const json& j);

json serializeGPUKernelConfiguration(const GPUKernelConfiguration& config);
json serializeHardwareMetadata(const HardwareMetadata& metadata);
json serializePerformanceBaseline(const PerformanceBaseline& baseline);
json serializeBenchmarkResult(const BenchmarkResult& result);
json serializeTelemetrySample(const TelemetrySample& sample);
json serializeValidationResult(const ValidationResult& result);

// Deserialization functions
GPUKernelConfiguration deserializeGPUKernelConfiguration(const json& j);
HardwareMetadata deserializeHardwareMetadata(const json& j);
PerformanceBaseline deserializePerformanceBaseline(const json& j);
BenchmarkResult deserializeBenchmarkResult(const json& j);
TelemetrySample deserializeTelemetrySample(const json& j);
ValidationResult deserializeValidationResult(const json& j);

// File I/O functions
void saveJSONToFile(const json& j, const std::string& filePath, bool pretty = true);
json loadJSONFromFile(const std::string& filePath);

// SHA-256 protection functions
bool verifySHA256Digest(const json& j);
json addSHA256Digest(json j);
void saveProtectedJSONToFile(const json& j, const std::string& filePath);
json loadProtectedJSONFromFile(const std::string& filePath);

// Utility functions for creating test data
PerformanceBaseline createSamplePerformanceBaseline();
BenchmarkResult createSampleBenchmarkResult();
HardwareMetadata createSampleHardwareMetadata();
GPUKernelConfiguration createSampleGPUKernelConfiguration();

} // namespace utils
} // namespace keyhunt