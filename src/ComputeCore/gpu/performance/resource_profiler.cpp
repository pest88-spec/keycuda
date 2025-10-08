#include "ComputeCore/gpu/performance/resource_profiler.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <iterator>

namespace keycuda {
namespace gpu {
namespace performance {

// Factory method implementation
std::unique_ptr<ResourceProfiler> ResourceProfiler::Create() {
    return std::make_unique<CudaResourceProfiler>();
}

// Base class
ResourceProfiler::ResourceProfiler() = default;
ResourceProfiler::~ResourceProfiler() = default;

// CudaResourceProfiler implementation
CudaResourceProfiler::CudaResourceProfiler() {
    InitializeNvml();
}

CudaResourceProfiler::~CudaResourceProfiler() {
    ShutdownNvml();
}

GpuCapabilities CudaResourceProfiler::GetGpuCapabilities(int gpu_id) const {
    GpuCapabilities caps;

    if (!IsGpuAvailable(gpu_id)) {
        return caps;
    }

    try {
        // Basic identification
        caps.device_id = gpu_id;
        caps.device_name = GetGpuDeviceName(gpu_id);
        caps.compute_capability = GetComputeCapability(gpu_id);
        caps.architecture = IdentifyArchitecture(caps.compute_capability);

        // Memory characteristics
        caps.total_memory_mb = GetTotalMemory(gpu_id) / (1024 * 1024);
        caps.memory_type = DetectMemoryType(gpu_id);
        caps.memory_bus_width_bits = EstimateMemoryBusWidth(gpu_id);
        caps.memory_bandwidth_gb_per_sec = GetMemoryBandwidth(gpu_id);
        caps.l2_cache_size_kb = GetL2CacheSize(gpu_id);

        // Get current memory usage
        auto memory_usage = GetMemoryUsage(gpu_id);
        caps.free_memory_mb = memory_usage.free_memory_mb;

        // Compute resources
        caps.sm_count = GetSmCount(gpu_id);
        caps.max_threads_per_sm = GetMaxThreadsPerSm(caps.compute_capability);
        caps.memory_bandwidth_utilization_percent = memory_usage.utilization_percentage;

        // Get detailed device properties
        cudaDeviceProp props;
        cudaError_t error = cudaGetDeviceProperties(&props, gpu_id);
        if (error == cudaSuccess) {
            caps.max_threads_per_block = props.maxThreadsPerBlock;
            caps.max_blocks_per_sm = props.maxBlocksPerMultiProcessor;
            caps.shared_memory_per_block = props.sharedMemPerBlock;
            caps.total_shared_memory = props.sharedMemPerMultiprocessor;
            caps.max_registers_per_thread = props.regsPerBlock / props.maxThreadsPerBlock;
            caps.warp_size = props.warpSize;
            caps.clock_rate_mhz = static_cast<double>(props.clockRate) / 1000.0;
            caps.memory_clock_rate_mhz = static_cast<double>(props.memoryClockRate) / 1000.0;
        }

        // Feature detection
        caps.supports_managed_memory = DetectManagedMemory(gpu_id);
        caps.supports_cooperative_groups = DetectCooperativeGroups(caps.compute_capability);
        caps.supports_tensor_cores = DetectTensorCores(caps.compute_capability);
        caps.supports_ray_tracing = DetectRayTracingCores(caps.device_name);
        caps.supports_async_copy = DetectAsyncCopy(caps.compute_capability);
        caps.supports_mbarrier = DetectMbarrier(caps.compute_capability);
        caps.supports_cdp = DetectCDP(caps.compute_capability);

        // Performance calculations
        caps.peak_fp32_tflops = CalculatePeakFp32Performance(caps);
        caps.peak_fp16_tflops = CalculatePeakFp16Performance(caps);
        caps.peak_tensor_tflops = CalculatePeakTensorPerformance(caps);

        // Power characteristics
        caps.thermal_design_power_watts = GetPowerLimit(gpu_id);

        // Architecture-specific optimizations
        caps.supported_optimizations = GetSupportedOptimizations(caps);
        caps.architecture_multipliers = GetArchitectureMultipliers(caps);

        // Capability assessment
        caps.optimization_potential = CalculateOptimizationPotential(caps);
        caps.optimization_recommendation = GenerateOptimizationRecommendation(caps);
        caps.is_optimization_capable = SupportsOptimizedFeatures(gpu_id);

    } catch (const std::exception& e) {
        std::cerr << "Error getting GPU capabilities for GPU " << gpu_id << ": " << e.what() << std::endl;
    }

    return caps;
}

MemoryUsage CudaResourceProfiler::GetMemoryUsage(int gpu_id) const {
    MemoryUsage usage;

    if (!IsGpuAvailable(gpu_id)) {
        return usage;
    }

    try {
        size_t free_memory = 0;
        size_t total_memory = 0;

        cudaError_t error = cudaMemGetInfo(&free_memory, &total_memory);
        if (error != cudaSuccess) {
            throw std::runtime_error("cudaMemGetInfo failed: " + std::string(cudaGetErrorString(error)));
        }

        usage.total_memory_mb = total_memory / (1024 * 1024);
        usage.used_memory_mb = (total_memory - free_memory) / (1024 * 1024);
        usage.free_memory_mb = free_memory / (1024 * 1024);
        usage.utilization_percentage = (static_cast<double>(usage.used_memory_mb) / usage.total_memory_mb) * 100.0;
    } catch (const std::exception& e) {
        std::cerr << "Error getting memory usage for GPU " << gpu_id << ": " << e.what() << std::endl;
    }

    return usage;
}

ThermalState CudaResourceProfiler::GetThermalState(int gpu_id) const {
    ThermalState state;

    if (!IsGpuAvailable(gpu_id) || !nvml_initialized_) {
        return state;
    }

    try {
        state.temperature_celsius = GetGpuTemperature(gpu_id);
        state.power_usage_watts = GetGpuPowerUsage(gpu_id);
        state.thermal_throttling = IsThermalThrottling(gpu_id);
        state.throttling_events = state.thermal_throttling ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "Error getting thermal state for GPU " << gpu_id << ": " << e.what() << std::endl;
    }

    return state;
}

bool CudaResourceProfiler::IsGpuAvailable(int gpu_id) const {
    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);

    if (error != cudaSuccess) {
        return false;
    }

    return gpu_id >= 0 && gpu_id < device_count;
}

std::vector<int> CudaResourceProfiler::GetAvailableGpus() const {
    std::vector<int> available_gpus;

    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);

    if (error != cudaSuccess) {
        return available_gpus;
    }

    for (int i = 0; i < device_count; ++i) {
        if (IsGpuAvailable(i)) {
            available_gpus.push_back(i);
        }
    }

    return available_gpus;
}

std::string CudaResourceProfiler::GetGpuName(int gpu_id) const {
    return GetGpuDeviceName(gpu_id);
}

bool CudaResourceProfiler::SupportsOptimizedFeatures(int gpu_id) const {
    auto caps = GetGpuCapabilities(gpu_id);

    // Require compute capability 7.5+ for optimal features
    if (caps.compute_capability < 75) {
        return false;
    }

    // Require minimum memory for effective optimization
    if (caps.total_memory_mb < 8192) { // 8GB minimum
        return false;
    }

    return true;
}

// Private helper methods
int CudaResourceProfiler::GetComputeCapability(int gpu_id) const {
    cudaDeviceProp props;
    cudaError_t error = cudaGetDeviceProperties(&props, gpu_id);

    if (error != cudaSuccess) {
        throw std::runtime_error("cudaGetDeviceProperties failed: " + std::string(cudaGetErrorString(error)));
    }

    return props.major * 10 + props.minor; // e.g., 8.6 -> 86
}

size_t CudaResourceProfiler::GetTotalMemory(int gpu_id) const {
    cudaDeviceProp props;
    cudaError_t error = cudaGetDeviceProperties(&props, gpu_id);

    if (error != cudaSuccess) {
        throw std::runtime_error("cudaGetDeviceProperties failed: " + std::string(cudaGetErrorString(error)));
    }

    return props.totalGlobalMem;
}

int CudaResourceProfiler::GetSmCount(int gpu_id) const {
    cudaDeviceProp props;
    cudaError_t error = cudaGetDeviceProperties(&props, gpu_id);

    if (error != cudaSuccess) {
        throw std::runtime_error("cudaGetDeviceProperties failed: " + std::string(cudaGetErrorString(error)));
    }

    return props.multiProcessorCount;
}

double CudaResourceProfiler::GetMemoryBandwidth(int gpu_id) const {
    // Estimate memory bandwidth based on GPU architecture
    int compute_cap = GetComputeCapability(gpu_id);

    // These are theoretical maximums in GB/s
    switch (compute_cap) {
        case 90: // Hopper
            return 3350.0; // H100 SXM5
        case 89: // Ada Lovelace (RTX 4090)
            return 1008.0;
        case 86: // Ampere (RTX 3090)
            return 936.0;
        case 80: // Ampere (A100)
            return 1555.0;
        case 75: // Turing (RTX 2080 Ti)
            return 616.0;
        default:
            return 500.0; // Conservative estimate
    }
}

size_t CudaResourceProfiler::GetL2CacheSize(int gpu_id) const {
    cudaDeviceProp props;
    cudaError_t error = cudaGetDeviceProperties(&props, gpu_id);

    if (error != cudaSuccess) {
        throw std::runtime_error("cudaGetDeviceProperties failed: " + std::string(cudaGetErrorString(error)));
    }

    return props.l2CacheSize / 1024; // Convert to KB
}

std::string CudaResourceProfiler::GetGpuDeviceName(int gpu_id) const {
    cudaDeviceProp props;
    cudaError_t error = cudaGetDeviceProperties(&props, gpu_id);

    if (error != cudaSuccess) {
        return "Unknown GPU";
    }

    return std::string(props.name);
}

int CudaResourceProfiler::GetMaxThreadsPerSm(int compute_capability) const {
    // Maximum threads per SM based on compute capability
    if (compute_capability >= 80) {
        return 1536; // Ampere and later
    } else if (compute_capability >= 75) {
        return 1024; // Turing
    } else {
        return 2048; // Pascal and earlier
    }
}

// NVML helper methods
bool CudaResourceProfiler::InitializeNvml() {
#ifdef HAS_NVML
    nvmlReturn_t result = nvmlInit();
    nvml_initialized_ = (result == NVML_SUCCESS);

    if (!nvml_initialized_) {
        std::cerr << "Warning: NVML initialization failed (fallback mode enabled)" << std::endl;
        std::cerr << "Advanced thermal monitoring will not be available" << std::endl;
    }
#else
    std::cout << "NVML not available - using fallback resource profiling" << std::endl;
    nvml_initialized_ = false;
#endif

    return nvml_initialized_;
}

void CudaResourceProfiler::ShutdownNvml() {
#ifdef HAS_NVML
    if (nvml_initialized_) {
        nvmlShutdown();
        nvml_initialized_ = false;
    }
#endif
}

double CudaResourceProfiler::GetGpuTemperature(int gpu_id) const {
    (void)gpu_id; // Suppress unused parameter warning
#ifdef HAS_NVML
    if (nvml_initialized_) {
        nvmlDevice_t device;
        nvmlReturn_t result = nvmlDeviceGetHandleByIndex(gpu_id, &device);

        if (result == NVML_SUCCESS) {
            unsigned int temp;
            result = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);

            if (result == NVML_SUCCESS) {
                return static_cast<double>(temp);
            }
        }
    }
#endif
    // Fallback: return reasonable default
    return 30.0;  // Default temperature estimate
}

double CudaResourceProfiler::GetGpuPowerUsage(int gpu_id) const {
    (void)gpu_id; // Suppress unused parameter warning
#ifdef HAS_NVML
    if (nvml_initialized_) {
        nvmlDevice_t device;
        nvmlReturn_t result = nvmlDeviceGetHandleByIndex(gpu_id, &device);

        if (result == NVML_SUCCESS) {
            unsigned int power_mw;
            result = nvmlDeviceGetPowerUsage(device, &power_mw);

            if (result == NVML_SUCCESS) {
                return static_cast<double>(power_mw) / 1000.0; // Convert mW to W
            }
        }
    }
#endif
    // Fallback: estimate power based on typical RTX 3090 values
    return 37.0;  // Reasonable idle power estimate
}

bool CudaResourceProfiler::IsThermalThrottling(int gpu_id) const {
    double temp = GetGpuTemperature(gpu_id);
    return temp > 85.0;  // Simple throttling check
}

// New architecture detection methods
GpuArchitecture CudaResourceProfiler::IdentifyArchitecture(int compute_cap) const {
    switch (compute_cap) {
        case 60: return GpuArchitecture::PASCAL;
        case 70: return GpuArchitecture::VOLTA;
        case 75: return GpuArchitecture::TURING;
        case 80: return GpuArchitecture::AMPERE;
        case 86: return GpuArchitecture::AMPERE;
        case 89: return GpuArchitecture::ADA_LOVELACE;
        case 90: return GpuArchitecture::HOPPER;
        default: return GpuArchitecture::UNKNOWN;
    }
}

std::string CudaResourceProfiler::ParseGpuName(const std::string& device_name) const {
    // Extract architecture information from device name
    std::string lower_name = device_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    if (lower_name.find("rtx 40") != std::string::npos) {
        return "Ada Lovelace";
    } else if (lower_name.find("rtx 30") != std::string::npos) {
        return "Ampere";
    } else if (lower_name.find("rtx 20") != std::string::npos) {
        return "Turing";
    } else if (lower_name.find("a100") != std::string::npos || lower_name.find("a30") != std::string::npos) {
        return "Ampere";
    } else if (lower_name.find("h100") != std::string::npos || lower_name.find("h200") != std::string::npos) {
        return "Hopper";
    } else if (lower_name.find("v100") != std::string::npos) {
        return "Volta";
    } else if (lower_name.find("p100") != std::string::npos || lower_name.find("p40") != std::string::npos) {
        return "Pascal";
    }

    return "Unknown";
}

MemoryType CudaResourceProfiler::DetectMemoryType(int gpu_id) const {
    std::string device_name = GetGpuDeviceName(gpu_id);
    std::string lower_name = device_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    if (lower_name.find("hbm") != std::string::npos) {
        if (lower_name.find("hbm3") != std::string::npos) return MemoryType::HBM3;
        if (lower_name.find("hbm2") != std::string::npos) return MemoryType::HBM2;
        return MemoryType::HBM2;
    } else if (lower_name.find("gddr6x") != std::string::npos) {
        return MemoryType::GDDR6X;
    } else if (lower_name.find("gddr6") != std::string::npos) {
        return MemoryType::GDDR6;
    } else if (lower_name.find("gddr5x") != std::string::npos) {
        return MemoryType::GDDR5X;
    } else if (lower_name.find("gddr5") != std::string::npos) {
        return MemoryType::GDDR5;
    }

    // Default based on architecture
    int compute_cap = GetComputeCapability(gpu_id);
    if (compute_cap >= 90) return MemoryType::HBM3;  // Hopper
    if (compute_cap >= 89) return MemoryType::GDDR6X;  // Ada
    if (compute_cap >= 80) return MemoryType::GDDR6;   // Ampere
    if (compute_cap >= 75) return MemoryType::GDDR6;   // Turing
    if (compute_cap >= 70) return MemoryType::HBM2;    // Volta
    return MemoryType::GDDR5;  // Conservative default
}

// Performance calculation methods
double CudaResourceProfiler::CalculatePeakFp32Performance(const GpuCapabilities& caps) const {
    // TFLOPS = (SM_count * max_threads_per_sm * clock_rate * 2) / 1e12
    // Assuming FMA capability (2 FLOPs per clock)
    double clock_ghz = caps.clock_rate_mhz / 1000.0;
    return (caps.sm_count * caps.max_threads_per_sm * clock_ghz * 2.0) / 1000.0;
}

double CudaResourceProfiler::CalculatePeakFp16Performance(const GpuCapabilities& caps) const {
    // FP16 performance varies significantly by architecture
    double fp32_tlops = CalculatePeakFp32Performance(caps);

    switch (caps.architecture) {
        case GpuArchitecture::HOPPER:
            return fp32_tlops * 64.0;  // 64x FP16 vs FP32
        case GpuArchitecture::ADA_LOVELACE:
            return fp32_tlops * 32.0;  // 32x FP16 vs FP32
        case GpuArchitecture::AMPERE:
            return fp32_tlops * 16.0;  // 16x FP16 vs FP32
        case GpuArchitecture::TURING:
            return fp32_tlops * 8.0;   // 8x FP16 vs FP32
        case GpuArchitecture::VOLTA:
            return fp32_tlops * 8.0;   // 8x FP16 vs FP32
        default:
            return fp32_tlops * 2.0;   // Conservative estimate
    }
}

double CudaResourceProfiler::CalculatePeakTensorPerformance(const GpuCapabilities& caps) const {
    if (!caps.supports_tensor_cores) return 0.0;

    double fp32_tlops = CalculatePeakFp32Performance(caps);

    switch (caps.architecture) {
        case GpuArchitecture::HOPPER:
            return fp32_tlops * 256.0;  // Hopper Tensor Cores
        case GpuArchitecture::ADA_LOVELACE:
            return fp32_tlops * 128.0;  // Ada Tensor Cores
        case GpuArchitecture::AMPERE:
            return fp32_tlops * 64.0;   // Ampere Tensor Cores
        case GpuArchitecture::TURING:
            return fp32_tlops * 32.0;   // Turing Tensor Cores
        case GpuArchitecture::VOLTA:
            return fp32_tlops * 16.0;   // Volta Tensor Cores
        default:
            return 0.0;
    }
}

std::vector<int> CudaResourceProfiler::CalculateOptimalBlockSizes(const GpuCapabilities& caps) const {
    std::vector<int> optimal_sizes;

    // Architecture-specific optimal block sizes
    switch (caps.architecture) {
        case GpuArchitecture::HOPPER:
        case GpuArchitecture::ADA_LOVELACE:
            optimal_sizes = {256, 512, 1024, 768, 384};
            break;
        case GpuArchitecture::AMPERE:
            optimal_sizes = {256, 512, 1024, 768, 384};
            break;
        case GpuArchitecture::TURING:
            optimal_sizes = {256, 512, 1024, 384};
            break;
        case GpuArchitecture::VOLTA:
            optimal_sizes = {256, 512, 768, 384};
            break;
        case GpuArchitecture::PASCAL:
            optimal_sizes = {256, 512, 1024};
            break;
        default:
            optimal_sizes = {256, 512, 1024};
            break;
    }

    // Filter by maximum threads per block
    optimal_sizes.erase(
        std::remove_if(optimal_sizes.begin(), optimal_sizes.end(),
                       [caps](int size) { return size > caps.max_threads_per_block; }),
        optimal_sizes.end()
    );

    return optimal_sizes;
}

// Feature detection methods
bool CudaResourceProfiler::DetectTensorCores(int compute_cap) const {
    return compute_cap >= 70;  // Tensor cores introduced in Volta (7.0)
}

bool CudaResourceProfiler::DetectRayTracingCores(const std::string& device_name) const {
    std::string lower_name = device_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    return lower_name.find("rtx") != std::string::npos;
}

bool CudaResourceProfiler::DetectCooperativeGroups(int compute_cap) const {
    return compute_cap >= 60;  // Cooperative groups supported from Pascal
}

bool CudaResourceProfiler::DetectManagedMemory(int gpu_id) const {
    cudaDeviceProp props;
    cudaError_t error = cudaGetDeviceProperties(&props, gpu_id);
    if (error != cudaSuccess) return false;
    return props.managedMemory == 1;
}

bool CudaResourceProfiler::DetectAsyncCopy(int compute_cap) const {
    return compute_cap >= 80;  // Async copy introduced in Ampere
}

bool CudaResourceProfiler::DetectMbarrier(int compute_cap) const {
    return compute_cap >= 80;  // Memory barriers introduced in Ampere
}

bool CudaResourceProfiler::DetectCDP(int compute_cap) const {
    return compute_cap >= 35;  // Dynamic Parallelism from Kepler
}

// Optimization analysis methods
double CudaResourceProfiler::CalculateOptimizationPotential(const GpuCapabilities& caps) const {
    double potential = 0.0;

    // Base potential from compute capability
    if (caps.compute_capability >= 90) potential += 0.4;      // Hopper
    else if (caps.compute_capability >= 89) potential += 0.35; // Ada
    else if (caps.compute_capability >= 86) potential += 0.3;  // Ampere
    else if (caps.compute_capability >= 75) potential += 0.25; // Turing
    else if (caps.compute_capability >= 70) potential += 0.2;  // Volta
    else potential += 0.1;  // Older architectures

    // Memory bandwidth contribution
    if (caps.memory_bandwidth_gb_per_sec > 3000) potential += 0.3;  // Hopper-level bandwidth
    else if (caps.memory_bandwidth_gb_per_sec > 1000) potential += 0.2;
    else if (caps.memory_bandwidth_gb_per_sec > 600) potential += 0.1;

    // Feature support contribution
    if (caps.supports_tensor_cores) potential += 0.2;
    if (caps.supports_managed_memory) potential += 0.1;
    if (caps.supports_cooperative_groups) potential += 0.1;

    return std::min(potential, 1.0);
}

std::vector<std::string> CudaResourceProfiler::GetSupportedOptimizations(const GpuCapabilities& caps) const {
    std::vector<std::string> optimizations;

    // Architecture-specific optimizations
    switch (caps.architecture) {
        case GpuArchitecture::HOPPER:
            optimizations.push_back("tensor_memory_acceleration");
            optimizations.push_back("dp4a_optimization");
            optimizations.push_back("warp_specialization");
            optimizations.push_back("cluster_launch");
            break;
        case GpuArchitecture::ADA_LOVELACE:
            optimizations.push_back("tensor_memory_acceleration");
            optimizations.push_back("dp4a_optimization");
            optimizations.push_back("memory_swizzle");
            break;
        case GpuArchitecture::AMPERE:
            optimizations.push_back("tensor_core_utilization");
            optimizations.push_back("async_copy");
            optimizations.push_back("memory_coalescing");
            break;
        case GpuArchitecture::TURING:
            optimizations.push_back("tensor_core_utilization");
            optimizations.push_back("memory_coalescing");
            break;
        case GpuArchitecture::VOLTA:
            optimizations.push_back("tensor_core_utilization");
            optimizations.push_back("warp_scheduling");
            break;
        default:
            optimizations.push_back("basic_optimization");
            break;
    }

    // Memory-based optimizations
    if (caps.memory_type == MemoryType::HBM2 || caps.memory_type == MemoryType::HBM3) {
        optimizations.push_back("hbm_optimization");
    }

    if (caps.memory_bandwidth_gb_per_sec > 1000) {
        optimizations.push_back("bandwidth_optimization");
    }

    return optimizations;
}

std::map<std::string, double> CudaResourceProfiler::GetArchitectureMultipliers(const GpuCapabilities& caps) const {
    std::map<std::string, double> multipliers;

    // Base multipliers by architecture
    switch (caps.architecture) {
        case GpuArchitecture::HOPPER:
            multipliers["throughput"] = 2.5;
            multipliers["memory_efficiency"] = 2.0;
            multipliers["compute_efficiency"] = 2.8;
            break;
        case GpuArchitecture::ADA_LOVELACE:
            multipliers["throughput"] = 2.2;
            multipliers["memory_efficiency"] = 1.8;
            multipliers["compute_efficiency"] = 2.4;
            break;
        case GpuArchitecture::AMPERE:
            multipliers["throughput"] = 2.0;
            multipliers["memory_efficiency"] = 1.6;
            multipliers["compute_efficiency"] = 2.0;
            break;
        case GpuArchitecture::TURING:
            multipliers["throughput"] = 1.6;
            multipliers["memory_efficiency"] = 1.4;
            multipliers["compute_efficiency"] = 1.6;
            break;
        case GpuArchitecture::VOLTA:
            multipliers["throughput"] = 1.4;
            multipliers["memory_efficiency"] = 1.3;
            multipliers["compute_efficiency"] = 1.5;
            break;
        default:
            multipliers["throughput"] = 1.0;
            multipliers["memory_efficiency"] = 1.0;
            multipliers["compute_efficiency"] = 1.0;
            break;
    }

    // Memory type adjustments
    if (caps.memory_type == MemoryType::HBM3) {
        multipliers["memory_efficiency"] *= 1.5;
    } else if (caps.memory_type == MemoryType::HBM2) {
        multipliers["memory_efficiency"] *= 1.3;
    } else if (caps.memory_type == MemoryType::GDDR6X) {
        multipliers["memory_efficiency"] *= 1.2;
    }

    return multipliers;
}

std::string CudaResourceProfiler::GenerateOptimizationRecommendation(const GpuCapabilities& caps) const {
    std::stringstream ss;

    if (caps.optimization_potential > 0.8) {
        ss << "Excellent optimization potential on " << caps.device_name
           << " (" << GetArchitectureName(caps.architecture) << "). "
           << "Recommend aggressive tuning with " << caps.supported_optimizations.size()
           << " available optimizations.";
    } else if (caps.optimization_potential > 0.5) {
        ss << "Good optimization potential on " << caps.device_name
           << ". Focus on " << (caps.supports_tensor_cores ? "tensor core utilization" : "memory optimization")
           << " and architecture-specific tuning.";
    } else if (caps.optimization_potential > 0.3) {
        ss << "Moderate optimization potential on " << caps.device_name
           << ". Consider basic optimizations and memory access patterns.";
    } else {
        ss << "Limited optimization potential on " << caps.device_name
           << ". Focus on algorithmic improvements rather than hardware tuning.";
    }

    return ss.str();
}

// Implementation of new virtual methods
GpuArchitecture CudaResourceProfiler::DetectArchitecture(int gpu_id) const {
    int compute_cap = GetComputeCapability(gpu_id);
    return IdentifyArchitecture(compute_cap);
}

std::string CudaResourceProfiler::GetArchitectureName(GpuArchitecture arch) const {
    switch (arch) {
        case GpuArchitecture::PASCAL: return "Pascal";
        case GpuArchitecture::VOLTA: return "Volta";
        case GpuArchitecture::TURING: return "Turing";
        case GpuArchitecture::AMPERE: return "Ampere";
        case GpuArchitecture::ADA_LOVELACE: return "Ada Lovelace";
        case GpuArchitecture::HOPPER: return "Hopper";
        default: return "Unknown";
    }
}

bool CudaResourceProfiler::IsArchitectureSupported(GpuArchitecture arch) const {
    return arch >= GpuArchitecture::PASCAL;  // Support Pascal and newer
}

std::vector<GpuArchitecture> CudaResourceProfiler::GetSupportedArchitectures() const {
    return {
        GpuArchitecture::PASCAL,
        GpuArchitecture::VOLTA,
        GpuArchitecture::TURING,
        GpuArchitecture::AMPERE,
        GpuArchitecture::ADA_LOVELACE,
        GpuArchitecture::HOPPER
    };
}

bool CudaResourceProfiler::HasTensorCores(int gpu_id) const {
    int compute_cap = GetComputeCapability(gpu_id);
    return DetectTensorCores(compute_cap);
}

bool CudaResourceProfiler::HasRayTracingCores(int gpu_id) const {
    std::string device_name = GetGpuDeviceName(gpu_id);
    return DetectRayTracingCores(device_name);
}

bool CudaResourceProfiler::SupportsCooperativeLaunch(int gpu_id) const {
    int compute_cap = GetComputeCapability(gpu_id);
    return DetectCooperativeGroups(compute_cap);
}

bool CudaResourceProfiler::SupportsManagedMemory(int gpu_id) const {
    return DetectManagedMemory(gpu_id);
}

MemoryType CudaResourceProfiler::GetMemoryType(int gpu_id) const {
    return DetectMemoryType(gpu_id);
}

double CudaResourceProfiler::GetTheoreticalBandwidth(int gpu_id) const {
    auto caps = GetGpuCapabilities(gpu_id);
    return CalculateTheoreticalBandwidth(caps);
}

double CudaResourceProfiler::GetPeakComputePerformance(int gpu_id) const {
    auto caps = GetGpuCapabilities(gpu_id);
    return caps.peak_fp32_tflops;
}

double CudaResourceProfiler::EstimateOptimalBlockCount(int gpu_id, size_t workload_size) const {
    auto caps = GetGpuCapabilities(gpu_id);
    // Simple heuristic: aim for 2-4x SM count for good occupancy
    int base_block_count = caps.sm_count * 2;

    // Adjust for workload size
    if (workload_size > 0 && workload_size < caps.max_threads_per_block) {
        base_block_count = 1;
    }

    return static_cast<double>(base_block_count);
}

std::vector<int> CudaResourceProfiler::GetOptimalBlockSizes(int gpu_id) const {
    auto caps = GetGpuCapabilities(gpu_id);
    return CalculateOptimalBlockSizes(caps);
}

std::vector<GpuCapabilities> CudaResourceProfiler::GetAllGpuCapabilities() const {
    std::vector<GpuCapabilities> all_caps;
    auto available_gpus = GetAvailableGpus();

    for (int gpu_id : available_gpus) {
        all_caps.push_back(GetGpuCapabilities(gpu_id));
    }

    return all_caps;
}

GpuCapabilities CudaResourceProfiler::GetBestGpuForWorkload(size_t workload_size) const {
    auto all_caps = GetAllGpuCapabilities();
    if (all_caps.empty()) {
        return GpuCapabilities();
    }

    return SelectBestGpu(all_caps, workload_size);
}

bool CudaResourceProfiler::IsWorkloadSuitable(int gpu_id, size_t workload_size) const {
    auto caps = GetGpuCapabilities(gpu_id);

    // Check if GPU is optimization capable
    if (!caps.is_optimization_capable) return false;

    // Check memory constraints
    if (workload_size > caps.free_memory_mb * 1024 * 1024) return false;

    // Check compute capability minimum
    if (caps.compute_capability < 75) return false;  // Require Turing or better

    return true;
}

std::string CudaResourceProfiler::GetOptimizationRecommendation(int gpu_id) const {
    auto caps = GetGpuCapabilities(gpu_id);
    return GenerateOptimizationRecommendation(caps);
}

// Additional helper methods
size_t CudaResourceProfiler::EstimateMemoryBusWidth(int gpu_id) const {
    // Estimate bus width based on GPU architecture and memory type
    std::string device_name = GetGpuDeviceName(gpu_id);
    int compute_cap = GetComputeCapability(gpu_id);
    MemoryType mem_type = DetectMemoryType(gpu_id);

    if (mem_type == MemoryType::HBM2 || mem_type == MemoryType::HBM3) {
        return 1024;  // HBM typically 1024-bit or wider
    }

    if (compute_cap >= 89) {  // Ada
        return 384;  // RTX 4090 has 384-bit bus
    } else if (compute_cap >= 86) {  // Ampere
        if (device_name.find("3090") != std::string::npos) return 384;
        if (device_name.find("3080") != std::string::npos) return 320;
        if (device_name.find("3070") != std::string::npos) return 256;
        return 256;  // Conservative
    } else if (compute_cap >= 75) {  // Turing
        if (device_name.find("2080") != std::string::npos) return 256;
        if (device_name.find("2070") != std::string::npos) return 256;
        return 256;
    }

    return 256;  // Conservative default
}

double CudaResourceProfiler::CalculateTheoreticalBandwidth(const GpuCapabilities& caps) const {
    // Bandwidth = memory_clock * bus_width * 2 (DDR) / 8
    double memory_clock_ghz = caps.memory_clock_rate_mhz / 1000.0;
    double bus_width_bytes = static_cast<double>(caps.memory_bus_width_bits) / 8.0;

    return memory_clock_ghz * bus_width_bytes * 2.0;  // GB/s
}

double CudaResourceProfiler::GetMemoryClockRate(int gpu_id) const {
    cudaDeviceProp props;
    cudaError_t error = cudaGetDeviceProperties(&props, gpu_id);
    if (error != cudaSuccess) return 0.0;

    return static_cast<double>(props.memoryClockRate) / 1000.0;  // Convert kHz to MHz
}

double CudaResourceProfiler::EstimateOccupancy(int block_size, int registers_per_thread, const GpuCapabilities& caps) const {
    // Simplified occupancy calculation
    int max_blocks_per_sm = caps.max_blocks_per_sm;
    int max_threads_per_sm = caps.max_threads_per_sm;

    // Calculate blocks limited by threads
    int blocks_by_threads = max_threads_per_sm / block_size;

    // Calculate blocks limited by registers (simplified)
    int total_registers = 65536;  // Typical per SM
    int blocks_by_registers = total_registers / (block_size * registers_per_thread);

    // Calculate blocks limited by shared memory
    int blocks_by_shared_memory = caps.shared_memory_per_block > 0 ?
        caps.total_shared_memory / caps.shared_memory_per_block : max_blocks_per_sm;

    // Take the minimum
    int actual_blocks = std::min({blocks_by_threads, blocks_by_registers, blocks_by_shared_memory, max_blocks_per_sm});

    // Calculate occupancy as fraction of maximum threads
    return static_cast<double>(actual_blocks * block_size) / static_cast<double>(max_threads_per_sm);
}

double CudaResourceProfiler::EstimateWorkloadComplexity(size_t workload_size) const {
    // Simple complexity estimation based on workload size
    if (workload_size < 1000) return 0.1;
    if (workload_size < 1000000) return 0.5;
    if (workload_size < 1000000000) return 0.8;
    return 1.0;
}

bool CudaResourceProfiler::IsMemoryBound(size_t workload_size, const GpuCapabilities& caps) const {
    // Simple heuristic: larger workloads tend to be memory bound
    double complexity = EstimateWorkloadComplexity(workload_size);
    return complexity > 0.6 && caps.memory_bandwidth_gb_per_sec < 1000;
}

bool CudaResourceProfiler::IsComputeBound(size_t workload_size, const GpuCapabilities& caps) const {
    double complexity = EstimateWorkloadComplexity(workload_size);
    return complexity > 0.8 && caps.peak_fp32_tflops > 10.0;
}

GpuCapabilities CudaResourceProfiler::SelectBestGpu(const std::vector<GpuCapabilities>& candidates, size_t workload_size) const {
    if (candidates.empty()) return GpuCapabilities();

    GpuCapabilities best = candidates[0];
    double best_score = 0.0;

    for (const auto& caps : candidates) {
        double score = 0.0;

        // Score based on compute capability
        score += caps.compute_capability * 10.0;

        // Score based on memory bandwidth
        score += caps.memory_bandwidth_gb_per_sec / 100.0;

        // Score based on optimization potential
        score += caps.optimization_potential * 50.0;

        // Score based on available memory
        score += (caps.free_memory_mb / 1024.0) * 5.0;

        // Penalty for thermal throttling
        if (caps.thermal_design_power_watts > 350.0) score -= 10.0;

        if (score > best_score) {
            best_score = score;
            best = caps;
        }
    }

    return best;
}

double CudaResourceProfiler::GetPowerLimit(int gpu_id) const {
    (void)gpu_id; // Suppress unused parameter warning
#ifdef HAS_NVML
    if (nvml_initialized_) {
        nvmlDevice_t device;
        nvmlReturn_t result = nvmlDeviceGetHandleByIndex(gpu_id, &device);

        if (result == NVML_SUCCESS) {
            unsigned int power_mw;
            result = nvmlDeviceGetPowerManagementLimit(device, &power_mw);

            if (result == NVML_SUCCESS) {
                return static_cast<double>(power_mw) / 1000.0; // Convert mW to W
            }
        }
    }
#endif
    // Fallback: estimate based on architecture
    int compute_cap = GetComputeCapability(gpu_id);
    if (compute_cap >= 90) return 700.0;    // Hopper
    if (compute_cap >= 89) return 450.0;    // Ada
    if (compute_cap >= 86) return 350.0;    // Ampere
    if (compute_cap >= 75) return 250.0;    // Turing
    if (compute_cap >= 70) return 300.0;    // Volta
    return 200.0;  // Conservative default
}

} // namespace performance
} // namespace gpu
} // namespace keycuda