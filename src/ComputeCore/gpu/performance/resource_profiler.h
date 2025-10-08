#pragma once

#include <vector>
#include <memory>
#include <string>
#include <map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cuda_runtime.h>
// NVML support is optional
#ifdef HAS_NVML
#include <nvidia/ml/ml.h>
#else
// Define NVML types when NVML is not available
// Avoid conflicts with other headers by using include guards
#ifndef NVML_FALLBACK_TYPES_DEFINED
#define NVML_FALLBACK_TYPES_DEFINED
typedef void* nvmlDevice_t;
typedef enum {
    NVML_SUCCESS = 0,
    NVML_ERROR_NOT_SUPPORTED = 8
} nvmlReturn_t;
#endif // NVML_FALLBACK_TYPES_DEFINED
#endif

namespace keycuda {
namespace gpu {
namespace performance {

enum class GpuArchitecture {
    UNKNOWN = 0,
    PASCAL = 60,
    VOLTA = 70,
    TURING = 75,
    AMPERE = 80,
    ADA_LOVELACE = 89,
    HOPPER = 90
};

enum class MemoryType {
    UNKNOWN,
    DDR,
    GDDR5,
    GDDR5X,
    GDDR6,
    GDDR6X,
    HBM2,
    HBM3
};

struct GpuCapabilities {
    // Basic identification
    int device_id = -1;
    std::string device_name;
    GpuArchitecture architecture = GpuArchitecture::UNKNOWN;
    int compute_capability = 0;  // e.g., 86, 89, 90

    // Memory characteristics
    size_t total_memory_mb = 0;
    size_t free_memory_mb = 0;
    MemoryType memory_type = MemoryType::UNKNOWN;
    size_t memory_bus_width_bits = 0;
    double memory_bandwidth_gb_per_sec = 0.0;
    size_t l2_cache_size_kb = 0;

    // Compute resources
    int sm_count = 0;             // Streaming multiprocessors
    int max_threads_per_sm = 0;
    int max_threads_per_block = 0;
    int max_blocks_per_sm = 0;
    size_t shared_memory_per_block = 0;
    size_t total_shared_memory = 0;
    int max_registers_per_thread = 0;
    int warp_size = 0;

    // Clock frequencies
    double clock_rate_mhz = 0.0;
    double memory_clock_rate_mhz = 0.0;
    double boost_clock_mhz = 0.0;

    // Feature support
    bool supports_managed_memory = false;
    bool supports_cooperative_groups = false;
    bool supports_async_copy = false;
    bool supports_tensor_cores = false;
    bool supports_ray_tracing = false;
    bool supports_mbarrier = false;
    bool supports_cdp = false;  // Dynamic Parallelism

    // Performance characteristics
    double peak_fp32_tflops = 0.0;
    double peak_fp16_tflops = 0.0;
    double peak_tensor_tflops = 0.0;
    double memory_bandwidth_utilization_percent = 0.0;

    // Power and thermal
    double thermal_design_power_watts = 0.0;
    double max_power_limit_watts = 0.0;

    // Architecture-specific optimizations
    std::vector<std::string> supported_optimizations;
    std::map<std::string, double> architecture_multipliers;

    // Capability assessment
    bool is_optimization_capable = false;
    double optimization_potential = 0.0;  // 0.0-1.0
    std::string optimization_recommendation;
};

struct MemoryUsage {
    size_t total_memory_mb = 0;
    size_t used_memory_mb = 0;
    size_t free_memory_mb = 0;
    double utilization_percentage = 0.0;
};

struct ThermalState {
    double temperature_celsius = 0.0;
    double power_usage_watts = 0.0;
    bool thermal_throttling = false;
    int throttling_events = 0;
};

class ResourceProfiler {
public:
    ResourceProfiler();
    virtual ~ResourceProfiler();

    // Core profiling methods
    virtual GpuCapabilities GetGpuCapabilities(int gpu_id) const = 0;
    virtual MemoryUsage GetMemoryUsage(int gpu_id) const = 0;
    virtual ThermalState GetThermalState(int gpu_id) const = 0;
    virtual bool IsGpuAvailable(int gpu_id) const = 0;

    // Architecture detection and analysis
    virtual GpuArchitecture DetectArchitecture(int gpu_id) const = 0;
    virtual std::string GetArchitectureName(GpuArchitecture arch) const = 0;
    virtual bool IsArchitectureSupported(GpuArchitecture arch) const = 0;
    virtual std::vector<GpuArchitecture> GetSupportedArchitectures() const = 0;

    // Capability profiling
    virtual bool HasTensorCores(int gpu_id) const = 0;
    virtual bool HasRayTracingCores(int gpu_id) const = 0;
    virtual bool SupportsCooperativeLaunch(int gpu_id) const = 0;
    virtual bool SupportsManagedMemory(int gpu_id) const = 0;
    virtual MemoryType GetMemoryType(int gpu_id) const = 0;

    // Performance analysis
    virtual double GetTheoreticalBandwidth(int gpu_id) const = 0;
    virtual double GetPeakComputePerformance(int gpu_id) const = 0;
    virtual double EstimateOptimalBlockCount(int gpu_id, size_t workload_size) const = 0;
    virtual std::vector<int> GetOptimalBlockSizes(int gpu_id) const = 0;

    // Resource monitoring
    virtual std::vector<int> GetAvailableGpus() const = 0;
    virtual std::string GetGpuName(int gpu_id) const = 0;
    virtual std::vector<GpuCapabilities> GetAllGpuCapabilities() const = 0;
    virtual GpuCapabilities GetBestGpuForWorkload(size_t workload_size) const = 0;

    // Feature compatibility checks
    virtual bool SupportsOptimizedFeatures(int gpu_id) const = 0;
    virtual bool IsWorkloadSuitable(int gpu_id, size_t workload_size) const = 0;
    virtual std::string GetOptimizationRecommendation(int gpu_id) const = 0;

    // Factory method
    static std::unique_ptr<ResourceProfiler> Create();
};

// Concrete implementation
class CudaResourceProfiler : public ResourceProfiler {
public:
    CudaResourceProfiler();
    virtual ~CudaResourceProfiler();

    GpuCapabilities GetGpuCapabilities(int gpu_id) const override;
    MemoryUsage GetMemoryUsage(int gpu_id) const override;
    ThermalState GetThermalState(int gpu_id) const override;
    bool IsGpuAvailable(int gpu_id) const override;
    std::vector<int> GetAvailableGpus() const override;
    std::string GetGpuName(int gpu_id) const override;
    bool SupportsOptimizedFeatures(int gpu_id) const override;

private:
    bool nvml_initialized_ = false;

    // Basic helper methods
    int GetComputeCapability(int gpu_id) const;
    size_t GetTotalMemory(int gpu_id) const;
    int GetSmCount(int gpu_id) const;
    double GetMemoryBandwidth(int gpu_id) const;
    size_t GetL2CacheSize(int gpu_id) const;
    std::string GetGpuDeviceName(int gpu_id) const;
    int GetMaxThreadsPerSm(int compute_capability) const;

    // Architecture detection helpers
    GpuArchitecture IdentifyArchitecture(int compute_cap) const;
    std::string ParseGpuName(const std::string& device_name) const;
    MemoryType DetectMemoryType(int gpu_id) const;
    bool HasArchitectureFeature(int gpu_id, const std::string& feature) const;

    // Performance calculation helpers
    double CalculatePeakFp32Performance(const GpuCapabilities& caps) const;
    double CalculatePeakFp16Performance(const GpuCapabilities& caps) const;
    double CalculatePeakTensorPerformance(const GpuCapabilities& caps) const;
    std::vector<int> CalculateOptimalBlockSizes(const GpuCapabilities& caps) const;
    double EstimateOccupancy(int block_size, int registers_per_thread, const GpuCapabilities& caps) const;

    // Memory analysis helpers
    size_t EstimateMemoryBusWidth(int gpu_id) const;
    double CalculateTheoreticalBandwidth(const GpuCapabilities& caps) const;
    double GetMemoryClockRate(int gpu_id) const;

    // Feature detection helpers
    bool DetectTensorCores(int compute_cap) const;
    bool DetectRayTracingCores(const std::string& device_name) const;
    bool DetectCooperativeGroups(int compute_cap) const;
    bool DetectManagedMemory(int gpu_id) const;
    bool DetectAsyncCopy(int compute_cap) const;
    bool DetectMbarrier(int compute_cap) const;
    bool DetectCDP(int compute_cap) const;

    // Optimization analysis helpers
    double CalculateOptimizationPotential(const GpuCapabilities& caps) const;
    std::vector<std::string> GetSupportedOptimizations(const GpuCapabilities& caps) const;
    std::map<std::string, double> GetArchitectureMultipliers(const GpuCapabilities& caps) const;
    std::string GenerateOptimizationRecommendation(const GpuCapabilities& caps) const;

    // Workload analysis helpers
    double EstimateWorkloadComplexity(size_t workload_size) const;
    bool IsMemoryBound(size_t workload_size, const GpuCapabilities& caps) const;
    bool IsComputeBound(size_t workload_size, const GpuCapabilities& caps) const;
    GpuCapabilities SelectBestGpu(const std::vector<GpuCapabilities>& candidates, size_t workload_size) const;

    // NVML helper methods
    bool InitializeNvml();
    void ShutdownNvml();
    double GetGpuTemperature(int gpu_id) const;
    double GetGpuPowerUsage(int gpu_id) const;
    bool IsThermalThrottling(int gpu_id) const;
    double GetPowerLimit(int gpu_id) const;
};

} // namespace performance
} // namespace gpu
} // namespace keycuda