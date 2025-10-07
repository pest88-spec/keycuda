#pragma once

#include "core/uint256.h"

#include <cstdint>
#include <limits>

namespace puzzle71::shards {

class ShardWalker {
public:
    ShardWalker() = default;
    ShardWalker(const core::UInt256& start, const core::UInt256& end);

    const core::UInt256& Start() const { return start_; }
    const core::UInt256& End() const { return end_; }
    const core::UInt256& Next() const { return next_; }

    bool Done() const;
    core::UInt256 Remaining() const;

    std::uint64_t RemainingAsUint64(std::uint64_t fallback = std::numeric_limits<std::uint64_t>::max()) const;

    void Advance(std::uint64_t processed_keys);
    void Reset(const core::UInt256& resume_scalar);

    std::uint64_t LastProcessed() const { return last_processed_; }

private:
    core::UInt256 start_ = core::UInt256::Zero();
    core::UInt256 end_ = core::UInt256::Zero();
    core::UInt256 next_ = core::UInt256::Zero();
    std::uint64_t last_processed_{0};
};

}  // namespace puzzle71::shards
