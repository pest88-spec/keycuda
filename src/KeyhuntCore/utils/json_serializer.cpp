/**
 * @file json_serializer.cpp
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

#include "json_serializer.h"
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <ctime>
#include <iomanip>

using json = nlohmann::json;

namespace keyhunt {
namespace utils {

/**
 * @brief Compute SHA-256 digest of JSON string
 * @param jsonString JSON string to hash (minified, keys sorted)
 * @return Hex-encoded SHA-256 digest (64 characters)
 *
 * Used for tamper-proof manifest protection.
 * Always minify and sort keys before hashing for canonical representation.
 */
std::string computeSHA256Digest(const std::string& jsonString) {
    // Compute SHA-256 hash
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(jsonString.c_str()),
           jsonString.size(), hash);

    // Convert to hex string
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(hash[i]);
    }

    return oss.str();
}

/**
 * @brief Compute SHA-256 digest of JSON object (canonical form)
 * @param j JSON object to hash
 * @return Hex-encoded SHA-256 digest
 *
 * Converts JSON to canonical form (sorted keys, minified) before hashing.
 */
std::string computeSHA256DigestOfJSON(const json& j) {
    // Serialize to string with sorted keys (canonical form)
    std::string jsonString = j.dump();  // nlohmann::json automatically sorts keys
    return computeSHA256Digest(jsonString);
}

/**
 * @brief Serialize GPUKernelConfiguration to JSON
 * @param config Configuration object
 * @return JSON representation
 */
json serializeGPUKernelConfiguration(const struct GPUKernelConfiguration& config) {
    json j;
    j["kernelName"] = config.kernelName;
    j["gpuArchitecture"] = config.gpuArchitecture;  // enum as string
    j["gridDim"] = {config.gridDimX, config.gridDimY, config.gridDimZ};
    j["blockDim"] = {config.blockDimX, config.blockDimY, config.blockDimZ};
    j["pointsPerThread"] = config.pointsPerThread;
    j["sharedMemoryBytes"] = config.sharedMemoryBytes;
    j["registerBudget"] = config.registerBudget;
    j["streamId"] = config.streamId;
    return j;
}

/**
 * @brief Deserialize GPUKernelConfiguration from JSON
 * @param j JSON object
 * @return Configuration object
 */
// struct GPUKernelConfiguration deserializeGPUKernelConfiguration(const json& j) {
//     struct GPUKernelConfiguration config;
//     config.kernelName = j["kernelName"];
//     config.gpuArchitecture = j["gpuArchitecture"];  // TODO: convert string to enum
//     config.gridDimX = j["gridDim"][0];
//     config.gridDimY = j["gridDim"][1];
//     config.gridDimZ = j["gridDim"][2];
//     config.blockDimX = j["blockDim"][0];
//     config.blockDimY = j["blockDim"][1];
//     config.blockDimZ = j["blockDim"][2];
//     config.pointsPerThread = j["pointsPerThread"];
//     config.sharedMemoryBytes = j["sharedMemoryBytes"];
//     config.registerBudget = j["registerBudget"];
//     config.streamId = j["streamId"];
//     return config;
// }

/**
 * @brief Serialize HardwareMetadata to JSON
 * @param metadata Hardware metadata object (from cuda_utils.cu)
 * @return JSON representation
 */
json serializeHardwareMetadata(const struct HardwareMetadata& metadata) {
    json j;
    j["gpuModel"] = metadata.gpuModel;
    j["gpuCount"] = metadata.gpuCount;
    j["computeCapability"] = metadata.computeCapability;
    j["smCount"] = metadata.smCount;
    j["totalMemoryBytes"] = metadata.totalMemoryBytes;
    j["clockSpeedMHz"] = metadata.clockSpeedMHz;
    j["memoryClockSpeedMHz"] = metadata.memoryClockSpeedMHz;
    j["driverVersion"] = metadata.driverVersion;
    j["cudaRuntimeVersion"] = metadata.cudaRuntimeVersion;
    j["cudaDriverVersion"] = metadata.cudaDriverVersion;
    j["firmwareVersion"] = metadata.firmwareVersion;
    j["thermalState"] = metadata.thermalState;
    j["powerLimitWatts"] = metadata.powerLimitWatts;
    j["timestamp"] = metadata.timestamp;
    return j;
}

/**
 * @brief Save JSON to file
 * @param j JSON object to save
 * @param filePath Output file path
 * @param pretty Pretty-print with indentation (default: true)
 * @throws std::runtime_error if file write fails
 */
void saveJSONToFile(const json& j, const std::string& filePath, bool pretty) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filePath);
    }

    if (pretty) {
        file << j.dump(2);  // 2-space indentation
    } else {
        file << j.dump();  // Minified
    }

    file.close();
}

/**
 * @brief Load JSON from file
 * @param filePath Input file path
 * @return Parsed JSON object
 * @throws std::runtime_error if file read or parse fails
 */
json loadJSONFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for reading: " + filePath);
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Failed to parse JSON from file: " + std::string(e.what()));
    }

    file.close();
    return j;
}

/**
 * @brief Verify SHA-256 digest of JSON object
 * @param j JSON object with "sha256Digest" field
 * @return true if digest matches, false otherwise
 *
 * Recomputes digest from all fields except "sha256Digest" and compares.
 * Used for tamper detection in baseline and result files.
 */
bool verifySHA256Digest(const json& j) {
    if (!j.contains("sha256Digest")) {
        return false;  // No digest field
    }

    std::string storedDigest = j["sha256Digest"];

    // Remove digest field and recompute
    json jWithoutDigest = j;
    jWithoutDigest.erase("sha256Digest");

    std::string computedDigest = computeSHA256DigestOfJSON(jWithoutDigest);

    return (storedDigest == computedDigest);
}

/**
 * @brief Add SHA-256 digest to JSON object
 * @param j JSON object to protect
 * @return JSON object with "sha256Digest" field added
 *
 * Computes digest of all fields, then adds "sha256Digest" field.
 * Use before saving baseline or result files.
 */
json addSHA256Digest(json j) {  // Pass by value to avoid modifying input
    // Remove existing digest field if present
    j.erase("sha256Digest");

    // Compute digest
    std::string digest = computeSHA256DigestOfJSON(j);

    // Add digest field
    j["sha256Digest"] = digest;

    return j;
}

/**
 * @brief Save JSON to file with SHA-256 protection
 * @param j JSON object to save (digest will be computed and added)
 * @param filePath Output file path
 * @throws std::runtime_error if file write fails
 *
 * Automatically computes and adds SHA-256 digest before saving.
 * Used for baseline and result archival.
 */
void saveProtectedJSONToFile(const json& j, const std::string& filePath) {
    json protected_json = addSHA256Digest(j);
    saveJSONToFile(protected_json, filePath, true);  // Pretty-print
}

/**
 * @brief Load JSON from file and verify SHA-256 digest
 * @param filePath Input file path
 * @return Parsed JSON object (if digest verification passes)
 * @throws std::runtime_error if file read fails or digest verification fails
 *
 * Automatically verifies SHA-256 digest after loading.
 * Throws exception if tampering detected.
 */
json loadProtectedJSONFromFile(const std::string& filePath) {
    json j = loadJSONFromFile(filePath);

    if (!verifySHA256Digest(j)) {
        throw std::runtime_error("SHA-256 digest verification failed: possible tampering detected in " + filePath);
    }

    return j;
}

// Additional serialization functions for PerformanceBaseline entities

json serializePerformanceBaseline(const PerformanceBaseline& baseline) {
    json j;
    j["baselineId"] = baseline.baselineId;
    j["gpuModel"] = baseline.gpuModel;
    j["computeCapability"] = baseline.computeCapability;
    j["driverVersion"] = baseline.driverVersion;
    j["cudaRuntimeVersion"] = baseline.cudaRuntimeVersion;
    j["kernelConfiguration"] = serializeGPUKernelConfiguration(baseline.kernelConfiguration);
    j["targetThroughput"] = baseline.targetThroughput;
    j["medianThroughput"] = baseline.medianThroughput;
    j["gpuUtilizationPercent"] = baseline.gpuUtilizationPercent;
    j["memoryBandwidthPercent"] = baseline.memoryBandwidthPercent;
    j["occupancyPercent"] = baseline.occupancyPercent;
    j["registerUsage"] = baseline.registerUsage;
    j["establishedDate"] = baseline.establishedDate;
    j["status"] = baseline.status;
    return j;
}

json serializeBenchmarkResult(const BenchmarkResult& result) {
    json j;
    j["resultId"] = result.resultId;
    j["featureBranch"] = result.featureBranch;
    j["commitSha"] = result.commitSha;
    j["gpuModel"] = result.gpuModel;
    j["hardwareMetadata"] = serializeHardwareMetadata(result.hardwareMetadata);
    j["kernelConfiguration"] = serializeGPUKernelConfiguration(result.kernelConfiguration);
    j["testDurationSeconds"] = result.testDurationSeconds;
    j["sampleCount"] = result.sampleCount;
    j["throughputSamples"] = result.throughputSamples;
    j["medianThroughput"] = result.medianThroughput;
    j["meanThroughput"] = result.meanThroughput;
    j["p95Throughput"] = result.p95Throughput;
    j["gpuUtilizationSamples"] = result.gpuUtilizationSamples;
    j["memoryBandwidthSamples"] = result.memoryBandwidthSamples;
    j["validationPassRate"] = result.validationPassRate;
    j["thermalEvents"] = result.thermalEvents;

    // Baseline comparison
    json baselineComp;
    baselineComp["baselineId"] = result.baselineComparison.baselineId;
    baselineComp["baselineThroughput"] = result.baselineComparison.baselineThroughput;
    baselineComp["throughputDelta"] = result.baselineComparison.throughputDelta;
    baselineComp["throughputDeltaPercent"] = result.baselineComparison.throughputDeltaPercent;
    baselineComp["isRegression"] = result.baselineComparison.isRegression;
    j["baselineComparison"] = baselineComp;

    j["profilingMetrics"] = result.profilingMetrics;
    j["passFailStatus"] = result.passFailStatus;
    j["timestamp"] = result.timestamp;
    return j;
}

json serializeTelemetrySample(const TelemetrySample& sample) {
    json j;
    j["timestamp"] = sample.timestamp;
    j["batchId"] = sample.batchId;
    j["keyspaceChunkStart"] = sample.keyspaceChunkStart;
    j["keysProcessed"] = sample.keysProcessed;
    j["throughputGkeysPerSec"] = sample.throughputGkeysPerSec;
    j["gpuUtilizationPercent"] = sample.gpuUtilizationPercent;
    j["memoryBandwidthPercent"] = sample.memoryBandwidthPercent;
    j["validationErrors"] = sample.validationErrors;
    if (sample.checkpointLatencyMs >= 0) {
        j["checkpointLatencyMs"] = sample.checkpointLatencyMs;
    }
    j["alertEvents"] = sample.alertEvents;
    return j;
}

json serializeValidationResult(const ValidationResult& result) {
    json j;
    j["validationId"] = result.validationId;
    j["kernelName"] = result.kernelName;
    j["cpuReferenceImplementation"] = result.cpuReferenceImplementation;
    j["testCaseCount"] = result.testCaseCount;
    j["passedCount"] = result.passedCount;
    j["failedCount"] = result.failedCount;
    j["passRate"] = result.passRate;
    j["maxRelativeError"] = result.maxRelativeError;
    j["meanRelativeError"] = result.meanRelativeError;

    // Failed test samples
    json failedSamples = json::array();
    for (const auto& sample : result.failedTestSamples) {
        json failedTest;
        failedTest["testCaseId"] = sample.testCaseId;
        failedTest["inputPrivateKey"] = sample.inputPrivateKey;
        failedTest["cpuResult"] = sample.cpuResult;
        failedTest["gpuResult"] = sample.gpuResult;
        failedTest["relativeError"] = sample.relativeError;
        failedSamples.push_back(failedTest);
    }
    j["failedTestSamples"] = failedSamples;

    j["randomSeed"] = result.randomSeed;
    j["timestamp"] = result.timestamp;
    return j;
}

// Deserialization functions

GPUKernelConfiguration deserializeGPUKernelConfiguration(const json& j) {
    GPUKernelConfiguration config;
    config.kernelName = j["kernelName"];
    std::string archStr = j["gpuArchitecture"];
    if (archStr == "Turing") config.gpuArchitecture = GPUArchitecture::Turing;
    else if (archStr == "Ampere") config.gpuArchitecture = GPUArchitecture::Ampere;
    else if (archStr == "Hopper") config.gpuArchitecture = GPUArchitecture::Hopper;

    auto gridDim = j["gridDim"];
    config.gridDimX = gridDim[0];
    config.gridDimY = gridDim[1];
    config.gridDimZ = gridDim[2];

    auto blockDim = j["blockDim"];
    config.blockDimX = blockDim[0];
    config.blockDimY = blockDim[1];
    config.blockDimZ = blockDim[2];

    config.pointsPerThread = j["pointsPerThread"];
    config.sharedMemoryBytes = j["sharedMemoryBytes"];
    config.registerBudget = j["registerBudget"];
    config.streamId = j["streamId"];
    return config;
}

HardwareMetadata deserializeHardwareMetadata(const json& j) {
    HardwareMetadata metadata;
    metadata.gpuModel = j["gpuModel"];
    metadata.gpuCount = j["gpuCount"];
    metadata.computeCapability = j["computeCapability"];
    metadata.smCount = j["smCount"];
    metadata.totalMemoryBytes = j["totalMemoryBytes"];
    metadata.clockSpeedMHz = j["clockSpeedMHz"];
    metadata.memoryClockSpeedMHz = j["memoryClockSpeedMHz"];
    metadata.driverVersion = j["driverVersion"];
    metadata.cudaRuntimeVersion = j["cudaRuntimeVersion"];
    metadata.cudaDriverVersion = j["cudaDriverVersion"];
    metadata.firmwareVersion = j["firmwareVersion"];

    std::string thermalStr = j["thermalState"];
    if (thermalStr == "Normal") metadata.thermalState = ThermalState::Normal;
    else if (thermalStr == "Throttling") metadata.thermalState = ThermalState::Throttling;
    else if (thermalStr == "Critical") metadata.thermalState = ThermalState::Critical;

    metadata.powerLimitWatts = j["powerLimitWatts"];
    metadata.timestamp = j["timestamp"];
    return metadata;
}

PerformanceBaseline deserializePerformanceBaseline(const json& j) {
    PerformanceBaseline baseline;
    baseline.baselineId = j["baselineId"];
    baseline.gpuModel = j["gpuModel"];
    baseline.computeCapability = j["computeCapability"];
    baseline.driverVersion = j["driverVersion"];
    baseline.cudaRuntimeVersion = j["cudaRuntimeVersion"];
    baseline.kernelConfiguration = deserializeGPUKernelConfiguration(j["kernelConfiguration"]);
    baseline.targetThroughput = j["targetThroughput"];
    baseline.medianThroughput = j["medianThroughput"];
    baseline.gpuUtilizationPercent = j["gpuUtilizationPercent"];
    baseline.memoryBandwidthPercent = j["memoryBandwidthPercent"];
    baseline.occupancyPercent = j["occupancyPercent"];
    baseline.registerUsage = j["registerUsage"];
    baseline.establishedDate = j["establishedDate"];
    baseline.sha256Digest = j["sha256Digest"];

    std::string statusStr = j["status"];
    if (statusStr == "Active") baseline.status = BaselineStatus::Active;
    else if (statusStr == "Deprecated") baseline.status = BaselineStatus::Deprecated;
    else if (statusStr == "Superseded") baseline.status = BaselineStatus::Superseded;

    return baseline;
}

BenchmarkResult deserializeBenchmarkResult(const json& j) {
    BenchmarkResult result;
    result.resultId = j["resultId"];
    result.featureBranch = j["featureBranch"];
    result.commitSha = j["commitSha"];
    result.gpuModel = j["gpuModel"];
    result.hardwareMetadata = deserializeHardwareMetadata(j["hardwareMetadata"]);
    result.kernelConfiguration = deserializeGPUKernelConfiguration(j["kernelConfiguration"]);
    result.testDurationSeconds = j["testDurationSeconds"];
    result.sampleCount = j["sampleCount"];
    result.throughputSamples = j["throughputSamples"].get<std::vector<double>>();
    result.medianThroughput = j["medianThroughput"];
    result.meanThroughput = j["meanThroughput"];
    result.p95Throughput = j["p95Throughput"];
    result.gpuUtilizationSamples = j["gpuUtilizationSamples"].get<std::vector<double>>();
    result.memoryBandwidthSamples = j["memoryBandwidthSamples"].get<std::vector<double>>();
    result.validationPassRate = j["validationPassRate"];
    result.thermalEvents = j["thermalEvents"].get<std::vector<std::string>>();

    // Baseline comparison
    auto baselineComp = j["baselineComparison"];
    result.baselineComparison.baselineId = baselineComp["baselineId"];
    result.baselineComparison.baselineThroughput = baselineComp["baselineThroughput"];
    result.baselineComparison.throughputDelta = baselineComp["throughputDelta"];
    result.baselineComparison.throughputDeltaPercent = baselineComp["throughputDeltaPercent"];
    result.baselineComparison.isRegression = baselineComp["isRegression"];

    result.profilingMetrics = j["profilingMetrics"];
    result.sha256Digest = j["sha256Digest"];
    result.timestamp = j["timestamp"];

    std::string statusStr = j["passFailStatus"];
    if (statusStr == "Pass") result.passFailStatus = TestStatus::Pass;
    else if (statusStr == "Fail") result.passFailStatus = TestStatus::Fail;
    else if (statusStr == "Quarantined") result.passFailStatus = TestStatus::Quarantined;

    return result;
}

TelemetrySample deserializeTelemetrySample(const json& j) {
    TelemetrySample sample;
    sample.timestamp = j["timestamp"];
    sample.batchId = j["batchId"];
    sample.keyspaceChunkStart = j["keyspaceChunkStart"];
    sample.keysProcessed = j["keysProcessed"];
    sample.throughputGkeysPerSec = j["throughputGkeysPerSec"];
    sample.gpuUtilizationPercent = j["gpuUtilizationPercent"];
    sample.memoryBandwidthPercent = j["memoryBandwidthPercent"];
    sample.validationErrors = j["validationErrors"];
    sample.checkpointLatencyMs = j.value("checkpointLatencyMs", -1.0);
    sample.alertEvents = j["alertEvents"].get<std::vector<std::string>>();
    return sample;
}

ValidationResult deserializeValidationResult(const json& j) {
    ValidationResult result;
    result.validationId = j["validationId"];
    result.kernelName = j["kernelName"];
    result.cpuReferenceImplementation = j["cpuReferenceImplementation"];
    result.testCaseCount = j["testCaseCount"];
    result.passedCount = j["passedCount"];
    result.failedCount = j["failedCount"];
    result.passRate = j["passRate"];
    result.maxRelativeError = j["maxRelativeError"];
    result.meanRelativeError = j["meanRelativeError"];

    // Failed test samples
    result.failedTestSamples.clear();
    for (const auto& sample : j["failedTestSamples"]) {
        FailedTest failedTest;
        failedTest.testCaseId = sample["testCaseId"];
        failedTest.inputPrivateKey = sample["inputPrivateKey"];
        failedTest.cpuResult = sample["cpuResult"];
        failedTest.gpuResult = sample["gpuResult"];
        failedTest.relativeError = sample["relativeError"];
        result.failedTestSamples.push_back(failedTest);
    }

    result.randomSeed = j["randomSeed"];
    result.timestamp = j["timestamp"];
    return result;
}

// Utility functions for creating test data

GPUKernelConfiguration createSampleGPUKernelConfiguration() {
    GPUKernelConfiguration config;
    config.kernelName = "eccScalarMulKernel";
    config.gpuArchitecture = GPUArchitecture::Ampere;
    config.gridDimX = 624;
    config.gridDimY = 1;
    config.gridDimZ = 1;
    config.blockDimX = 256;
    config.blockDimY = 1;
    config.blockDimZ = 1;
    config.pointsPerThread = 1024;
    config.sharedMemoryBytes = 49152;
    config.registerBudget = 128;
    config.streamId = 0;
    return config;
}

HardwareMetadata createSampleHardwareMetadata() {
    HardwareMetadata metadata;
    metadata.gpuModel = "RTX 3090";
    metadata.gpuCount = 1;
    metadata.computeCapability = "8.6";
    metadata.smCount = 82;
    metadata.totalMemoryBytes = 25769803776ULL;
    metadata.clockSpeedMHz = 1695;
    metadata.memoryClockSpeedMHz = 9751;
    metadata.driverVersion = "535.104.05";
    metadata.cudaRuntimeVersion = "12.1";
    metadata.cudaDriverVersion = "12.2";
    metadata.firmwareVersion = "94.02.5c.40.01";
    metadata.thermalState = ThermalState::Normal;
    metadata.powerLimitWatts = 350;

    // Current timestamp
    auto now = std::time(nullptr);
    auto tm = *std::gmtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    metadata.timestamp = oss.str();

    return metadata;
}

PerformanceBaseline createSamplePerformanceBaseline() {
    PerformanceBaseline baseline;
    baseline.baselineId = "rtx3090_v1";
    baseline.gpuModel = "RTX 3090";
    baseline.computeCapability = "8.6";
    baseline.driverVersion = "535.104.05";
    baseline.cudaRuntimeVersion = "12.1";
    baseline.kernelConfiguration = createSampleGPUKernelConfiguration();
    baseline.targetThroughput = 2.0;
    baseline.medianThroughput = 2.15;
    baseline.gpuUtilizationPercent = 92.3;
    baseline.memoryBandwidthPercent = 74.8;
    baseline.occupancyPercent = 55.6;
    baseline.registerUsage = 128;
    baseline.establishedDate = "2025-10-11T10:30:00Z";
    baseline.status = BaselineStatus::Active;
    return baseline;
}

BenchmarkResult createSampleBenchmarkResult() {
    BenchmarkResult result;
    result.resultId = "20251011_103000_RTX3090";
    result.featureBranch = "003-gpu-1-28";
    result.commitSha = "a1b2c3d4e5f6789abcdef0123456789abcdef0123";
    result.gpuModel = "RTX 3090";
    result.hardwareMetadata = createSampleHardwareMetadata();
    result.kernelConfiguration = createSampleGPUKernelConfiguration();
    result.testDurationSeconds = 600;
    result.sampleCount = 20;

    // Sample throughput data
    result.throughputSamples = {2.12, 2.15, 2.14, 2.16, 2.13, 2.15, 2.14, 2.17, 2.15, 2.14,
                                2.16, 2.13, 2.15, 2.14, 2.15, 2.13, 2.16, 2.14, 2.15, 2.14};
    result.medianThroughput = 2.15;
    result.meanThroughput = 2.14;
    result.p95Throughput = 2.16;

    // Sample utilization data
    result.gpuUtilizationSamples = {92.1, 92.3, 92.5, 92.2, 92.4, 92.3, 92.5, 92.1, 92.3, 92.4,
                                    92.2, 92.3, 92.5, 92.1, 92.4, 92.3, 92.2, 92.4, 92.3, 92.2};
    result.memoryBandwidthSamples = {74.5, 74.8, 75.1, 74.6, 74.9, 74.8, 75.0, 74.7, 74.8, 74.9,
                                      74.6, 74.8, 75.1, 74.5, 75.0, 74.8, 74.7, 74.9, 74.8, 74.6};
    result.validationPassRate = 100.0;
    result.thermalEvents = {};

    // Baseline comparison
    result.baselineComparison.baselineId = "rtx3090_v1";
    result.baselineComparison.baselineThroughput = 2.0;
    result.baselineComparison.throughputDelta = 0.15;
    result.baselineComparison.throughputDeltaPercent = 7.5;
    result.baselineComparison.isRegression = false;

    result.profilingMetrics = "";
    result.passFailStatus = TestStatus::Pass;
    result.timestamp = "2025-10-11T11:00:00Z";

    return result;
}

} // namespace utils
} // namespace keyhunt
