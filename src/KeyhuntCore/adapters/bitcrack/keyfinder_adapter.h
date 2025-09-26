#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/uint256.h"

namespace bitcrack_adapter {

struct KeySearchResult {
    puzzle71::core::UInt256 private_key;
    puzzle71::core::UInt256 x;
    puzzle71::core::UInt256 y;
    bool is_compressed;
    std::array<std::uint32_t,5> digest;
};

struct StepMetrics {
    std::uint64_t keys_processed{0};
    puzzle71::core::UInt256 next_scalar;
    bool work_remaining{false};
};

void Initialize(const puzzle71::core::UInt256& start,
                const puzzle71::core::UInt256& end,
                std::uint64_t batch_size,
                bool compressed,
                const std::array<std::uint32_t,5>& target_hash);

void RunStep();

std::vector<KeySearchResult> FetchResults();

const StepMetrics& LastStepMetrics();

bool HasWorkScheduled();

void Shutdown();

}  // namespace bitcrack_adapter
