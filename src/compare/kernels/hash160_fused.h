#pragma once

#include <cuda_runtime.h>

#include <array>
#include <cstdint>

namespace puzzle71::compare {

struct DeviceHashContext {
    std::array<std::uint32_t, 5> target_hash160{};
};

inline __device__ std::array<std::uint32_t, 5> Hash160Stub(const std::uint8_t* /*compressed_pubkey*/) {
    // TODO(T030): Implement device HASH160 routine fused with batch stepping kernel.
    return {0, 0, 0, 0, 0};
}

inline void LoadTargetHash160(DeviceHashContext* ctx, const std::array<std::uint32_t, 5>& host_hash) {
    ctx->target_hash160 = host_hash;
}

}  // namespace puzzle71::compare
