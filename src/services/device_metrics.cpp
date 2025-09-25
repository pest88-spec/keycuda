#include "services/device_metrics.h"

namespace puzzle71::services {

std::optional<DeviceMetrics> QueryDeviceMetrics(std::uint32_t device_id) {
    // TODO(T039): Hook into NVML for real occupancy/temperature metrics.
    DeviceMetrics metrics{};
    metrics.device_id = device_id;
    metrics.name = "GPU" + std::to_string(device_id);
    metrics.occupancy = 0.0;
    metrics.temperature = 0.0;
    return metrics;
}

}  // namespace puzzle71::services
