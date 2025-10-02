#pragma once

#include <cstdint>

namespace puzzle71::utils {

#if defined(__CUDACC__)
#define PUZZLE71_HOST_DEVICE __host__ __device__
#else
#define PUZZLE71_HOST_DEVICE
#endif

PUZZLE71_HOST_DEVICE constexpr std::uint32_t ByteSwap32(std::uint32_t value) {
    return (value << 24) | ((value << 8) & 0x00FF0000u) |
           ((value >> 8) & 0x0000FF00u) | (value >> 24);
}

#undef PUZZLE71_HOST_DEVICE

}  // namespace puzzle71::utils

