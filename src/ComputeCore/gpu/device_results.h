#pragma once

#include <cuda_runtime.h>

#include <array>
#include <cstdint>

namespace puzzle71::gpu {

struct DeviceCandidate {
    std::uint32_t block{0};
    std::uint32_t thread{0};
    std::uint32_t idx{0};
    std::uint32_t compressed{0};
    std::uint32_t x[8]{};
    std::uint32_t y[8]{};
    std::uint32_t digest[5]{};
};

struct DeviceResultBuffer {
    DeviceCandidate* candidates{nullptr};
    std::uint32_t* count{nullptr};
    std::uint32_t* dropped{nullptr};
    std::uint32_t capacity{0};
};

inline std::size_t ResultBufferBytes(std::size_t capacity) {
    return capacity * sizeof(DeviceCandidate);
}

}  // namespace puzzle71::gpu
