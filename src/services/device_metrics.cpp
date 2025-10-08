#include "services/device_metrics.h"

#include <cuda_runtime.h>

#include <dlfcn.h>

#include <array>
#include <cstring>
#include <string>
#include <type_traits>

namespace puzzle71::services {
namespace {

constexpr int kNvmlSuccess = 0;
constexpr unsigned int kNvmlTemperatureGpu = 0;

struct NvmlUtilizationRates {
    unsigned int gpu;
    unsigned int memory;
};

struct NvmlMemoryInfo {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

using nvmlDevice_t = void*;

struct NvmlApi {
    using nvmlInit_v2_t = int (*)();
    using nvmlShutdown_t = int (*)();
    using nvmlDeviceGetHandleByIndex_v2_t = int (*)(unsigned int, nvmlDevice_t*);
    using nvmlDeviceGetName_t = int (*)(nvmlDevice_t, char*, unsigned int);
    using nvmlDeviceGetUtilizationRates_t = int (*)(nvmlDevice_t, NvmlUtilizationRates*);
    using nvmlDeviceGetTemperature_t = int (*)(nvmlDevice_t, unsigned int, unsigned int*);
    using nvmlDeviceGetMemoryInfo_t = int (*)(nvmlDevice_t, NvmlMemoryInfo*);

    void* library{nullptr};
    bool initialized{false};
    nvmlInit_v2_t init{nullptr};
    nvmlShutdown_t shutdown{nullptr};
    nvmlDeviceGetHandleByIndex_v2_t get_handle{nullptr};
    nvmlDeviceGetName_t get_name{nullptr};
    nvmlDeviceGetUtilizationRates_t get_utilization{nullptr};
    nvmlDeviceGetTemperature_t get_temperature{nullptr};
    nvmlDeviceGetMemoryInfo_t get_memory{nullptr};

    static NvmlApi Load() {
        NvmlApi api;
        api.library = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!api.library) {
            return api;
        }

        auto load_symbol = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::decay_t<decltype(fn)>>(dlsym(api.library, name));
            return fn != nullptr;
        };

        if (!load_symbol(api.init, "nvmlInit_v2") ||
            !load_symbol(api.shutdown, "nvmlShutdown") ||
            !load_symbol(api.get_handle, "nvmlDeviceGetHandleByIndex_v2") ||
            !load_symbol(api.get_name, "nvmlDeviceGetName") ||
            !load_symbol(api.get_utilization, "nvmlDeviceGetUtilizationRates") ||
            !load_symbol(api.get_temperature, "nvmlDeviceGetTemperature") ||
            !load_symbol(api.get_memory, "nvmlDeviceGetMemoryInfo")) {
            dlclose(api.library);
            api.library = nullptr;
            return api;
        }

        if (api.init() != kNvmlSuccess) {
            dlclose(api.library);
            api.library = nullptr;
            return api;
        }

        api.initialized = true;
        return api;
    }

    ~NvmlApi() {
        if (initialized && shutdown) {
            shutdown();
        }
        if (library) {
            dlclose(library);
        }
    }
};

NvmlApi& Nvml() {
    static NvmlApi api = NvmlApi::Load();
    return api;
}

std::string QueryPciBusId(std::uint32_t device_id) {
    std::array<char, 32> buffer{};
    if (cudaDeviceGetPCIBusId(buffer.data(), static_cast<int>(buffer.size()), static_cast<int>(device_id)) == cudaSuccess) {
        return std::string(buffer.data());
    }
    return {};
}

std::string QueryDeviceName(std::uint32_t device_id) {
    cudaDeviceProp props{};
    if (cudaGetDeviceProperties(&props, static_cast<int>(device_id)) == cudaSuccess) {
        return std::string(props.name);
    }
    return "GPU" + std::to_string(device_id);
}

}  // namespace

std::optional<DeviceMetrics> QueryDeviceMetrics(std::uint32_t device_id) {
    NvmlApi& nvml = Nvml();
    if (!nvml.initialized) {
        return std::nullopt;
    }

    nvmlDevice_t handle{};
    if (nvml.get_handle(device_id, &handle) != kNvmlSuccess) {
        return std::nullopt;
    }

    DeviceMetrics metrics{};
    metrics.device_id = device_id;
    metrics.name = QueryDeviceName(device_id);
    metrics.pci_bus_id = QueryPciBusId(device_id);

    char name_buffer[96];
    if (nvml.get_name(handle, name_buffer, sizeof(name_buffer)) == kNvmlSuccess) {
        metrics.name = name_buffer;
    }

    NvmlUtilizationRates util{0, 0};
    if (nvml.get_utilization(handle, &util) == kNvmlSuccess) {
        metrics.occupancy = static_cast<double>(util.gpu);
        metrics.memory_utilization = static_cast<double>(util.memory);
    }

    NvmlMemoryInfo memory{0, 0, 0};
    if (nvml.get_memory(handle, &memory) == kNvmlSuccess && memory.total > 0) {
        metrics.memory_total_bytes = static_cast<std::uint64_t>(memory.total);
        metrics.memory_used_bytes = static_cast<std::uint64_t>(memory.used);
        metrics.memory_utilization = (static_cast<double>(memory.used) / static_cast<double>(memory.total)) * 100.0;
    }

    unsigned int temperature = 0;
    if (nvml.get_temperature(handle, kNvmlTemperatureGpu, &temperature) == kNvmlSuccess) {
        metrics.temperature = static_cast<double>(temperature);
    }

    return metrics;
}

}  // namespace puzzle71::services
