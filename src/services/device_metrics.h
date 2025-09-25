#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace puzzle71::services {

struct DeviceMetrics {
    std::string name;
    std::uint32_t device_id{0};
    double occupancy{0.0};
    double temperature{0.0};
};

std::optional<DeviceMetrics> QueryDeviceMetrics(std::uint32_t device_id);

}  // namespace puzzle71::services
