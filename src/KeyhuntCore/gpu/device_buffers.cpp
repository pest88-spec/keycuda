#include "KeyhuntCore/gpu/device_buffers.h"

#include <algorithm>

namespace puzzle71::gpu {

namespace {

core::UInt256 Increment(const core::UInt256& value, std::uint64_t delta) {
    core::UInt256 out = value;
    out.AddUint64(delta);
    return out;
}

}  // namespace

DeviceBuffers::DeviceBuffers() = default;

DeviceBuffers::~DeviceBuffers() {
    Release();
}

void DeviceBuffers::Release() {
    host_scalars_.clear();
}

void DeviceBuffers::Configure(dim3 grid, dim3 block) {
    grid_ = grid;
    block_ = block;
    std::uint64_t threads = static_cast<std::uint64_t>(grid.x) * block.x;
    host_scalars_.resize(threads);
}

DeviceBatch DeviceBuffers::PrepareBatch(const core::UInt256& start, std::uint64_t batch_size) {
    if (host_scalars_.empty()) {
        Configure(dim3(1,1,1), dim3(1,1,1));
    }

    std::uint64_t threads = static_cast<std::uint64_t>(grid_.x) * block_.x;
    std::uint64_t span = std::min<std::uint64_t>(batch_size, threads);

    core::UInt256 current = start;
    for (std::uint64_t i = 0; i < span; ++i) {
        host_scalars_[i] = current;
        current = Increment(current, 1);
    }

    DeviceBatch batch;
    batch.scalars.assign(host_scalars_.begin(), host_scalars_.begin() + span);
    return batch;
}

}  // namespace puzzle71::gpu

