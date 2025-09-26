#pragma once

#include <cuda_runtime.h>

#include <cstdint>
#include <vector>

#include "core/uint256.h"

namespace puzzle71::gpu {

struct DeviceBatch {
    std::vector<core::UInt256> scalars;
};

class DeviceBuffers {
public:
    DeviceBuffers();
    ~DeviceBuffers();

    DeviceBuffers(const DeviceBuffers&) = delete;
    DeviceBuffers& operator=(const DeviceBuffers&) = delete;

    void Configure(dim3 grid, dim3 block);

    DeviceBatch PrepareBatch(const core::UInt256& start, std::uint64_t batch_size);

private:
    void Release();

    dim3 grid_{0,0,0};
    dim3 block_{0,0,0};
    std::vector<core::UInt256> host_scalars_;
};

}  // namespace puzzle71::gpu
