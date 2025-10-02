#include "KeyhuntCore/shards/shard_walker.h"

#include <stdexcept>

namespace puzzle71::shards {

ShardWalker::ShardWalker(const core::UInt256& start, const core::UInt256& end)
    : start_(start), end_(end), next_(start) {
    if (start_.Compare(end_) > 0) {
        throw std::invalid_argument("ShardWalker start must be <= end");
    }
}

bool ShardWalker::Done() const {
    return next_.Compare(end_) > 0;
}

core::UInt256 ShardWalker::Remaining() const {
    if (Done()) {
        return core::UInt256::Zero();
    }
    core::UInt256 remaining = core::Difference(end_, next_);
    remaining.AddUint64(1);
    return remaining;
}

std::uint64_t ShardWalker::RemainingAsUint64(std::uint64_t fallback) const {
    core::UInt256 remaining = Remaining();
    if (!remaining.FitsInUint64()) {
        return fallback;
    }
    return remaining.ToUint64();
}

void ShardWalker::Advance(std::uint64_t processed_keys) {
    last_processed_ = processed_keys;
    if (processed_keys == 0 || Done()) {
        return;
    }

    core::UInt256 remaining = Remaining();
    std::uint64_t advance = processed_keys;
    if (remaining.FitsInUint64()) {
        std::uint64_t remaining64 = remaining.ToUint64();
        if (advance >= remaining64) {
            next_ = core::Incremented(end_, 1);
            return;
        }
    }

    next_ = core::Incremented(next_, advance);
}

void ShardWalker::Reset(const core::UInt256& resume_scalar) {
    core::UInt256 end_plus_one = core::Incremented(end_, 1);
    if (resume_scalar.Compare(start_) < 0 || resume_scalar.Compare(end_plus_one) > 0) {
        throw std::invalid_argument("Resume scalar outside shard range");
    }
    next_ = resume_scalar;
    last_processed_ = 0;
}

}  // namespace puzzle71::shards
