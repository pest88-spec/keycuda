#include "ComputeCore/gpu/device_buffers.h"
#include "ComputeCore/gpu/batch_planner.h"

#include <algorithm>
#include <stdexcept>

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

DeviceBuffers::DeviceBuffers(DeviceBuffers&& other) noexcept {
    Swap(other);
}

DeviceBuffers& DeviceBuffers::operator=(DeviceBuffers&& other) noexcept {
    if (this != &other) {
        Release();
        Swap(other);
    }
    return *this;
}

void DeviceBuffers::Release() {
    host_scalars_.clear();
    batch_scalars_.clear();
}

void DeviceBuffers::Swap(DeviceBuffers& other) noexcept {
    std::swap(grid_, other.grid_);
    std::swap(block_, other.block_);
    host_scalars_.swap(other.host_scalars_);
    batch_scalars_.swap(other.batch_scalars_);
}

void DeviceBuffers::Configure(dim3 grid, dim3 block, int points_per_thread) {
    grid_ = grid;
    block_ = block;
    points_per_thread_ = std::max(points_per_thread, 1);

    std::uint64_t threads = static_cast<std::uint64_t>(grid.x) * block.x;
    if (threads == 0) {
        threads = 1;
    }

    std::uint64_t total = threads * static_cast<std::uint64_t>(points_per_thread_);
    if (total == 0 || total > kMaxKeysPerBatch) {
        throw std::runtime_error("DeviceBuffers::Configure exceeds batch limits");
    }

    slots_ = static_cast<std::size_t>(total);

    // Always ensure exact size match for config changes
    host_scalars_.resize(slots_);
}

DeviceBatch DeviceBuffers::PrepareBatch(const core::UInt256& start, std::uint64_t batch_size) {
    if (host_scalars_.empty()) {
        Configure(dim3(1,1,1), dim3(1,1,1), 1);
    }

    if (batch_size == 0) {
        throw std::runtime_error("DeviceBuffers::PrepareBatch called with zero batch size");
    }

    // Ensure we have enough slots configured for the requested batch size
    if (batch_size > slots_) {
        // Calculate required grid/block to accommodate batch size
        std::uint64_t threads_needed = batch_size;
        dim3 new_grid(1, 1, 1);
        dim3 new_block(32, 1, 1); // Start with warp size
        int ppt = 1;

        // Increase block size first, then grid size
        while (static_cast<std::uint64_t>(new_block.x) * ppt < threads_needed && new_block.x < 1024) {
            new_block.x = std::min(new_block.x * 2, 1024u);
        }
        while (static_cast<std::uint64_t>(new_grid.x) * new_block.x * ppt < threads_needed) {
            new_grid.x++;
        }

        Configure(new_grid, new_block, ppt);
    }

    std::uint64_t span = std::min<std::uint64_t>(batch_size, slots_);

    core::UInt256 current = start;
    for (std::uint64_t i = 0; i < span; ++i) {
        host_scalars_[i] = current;
        current = Increment(current, 1);
    }

    // Always ensure batch_scalars_ matches exactly the span size
    if (batch_scalars_.size() != span) {
        batch_scalars_.resize(span);
    }

    std::copy(host_scalars_.begin(), host_scalars_.begin() + span, batch_scalars_.begin());

    DeviceBatch batch;
    batch.scalars = batch_scalars_; // This shares the internal data
    return batch;
}

void DeviceBuffers::Clear() {
    host_scalars_.clear();
    host_scalars_.shrink_to_fit();
    batch_scalars_.clear();
    batch_scalars_.shrink_to_fit();
    grid_ = dim3(0, 0, 0);
    block_ = dim3(0, 0, 0);
    points_per_thread_ = 1;
    slots_ = 0;
}

}  // namespace puzzle71::gpu
