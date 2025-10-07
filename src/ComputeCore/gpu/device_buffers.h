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
    DeviceBuffers(DeviceBuffers&& other) noexcept;
    DeviceBuffers& operator=(DeviceBuffers&& other) noexcept;

    void Configure(dim3 grid, dim3 block, int points_per_thread);

    DeviceBatch PrepareBatch(const core::UInt256& start, std::uint64_t batch_size);
    void Clear();

private:
    void Release();
    void Swap(DeviceBuffers& other) noexcept;

    dim3 grid_{0,0,0};
    dim3 block_{0,0,0};
    int points_per_thread_{1};
    std::size_t slots_{0};
    std::vector<core::UInt256> host_scalars_;
    std::vector<core::UInt256> batch_scalars_; // Reusable batch buffer
};

}  // namespace puzzle71::gpu
