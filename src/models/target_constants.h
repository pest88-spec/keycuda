#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace puzzle71::constants {

constexpr std::string_view kTargetAddress = "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU";
constexpr std::array<std::uint32_t, 5> kTargetHash160 = {
    0xd70104b4u, 0x9902133bu, 0x7ef4a795u, 0x046e8c5eu, 0x0d87780du};

constexpr std::uint64_t kKeyspaceStartHigh = 0x4000000000000000ULL;
constexpr std::uint64_t kKeyspaceEndHigh   = 0x7fffffffffffffffULL;

struct OperatorMetadata {
    std::string operator_id;
    std::string operator_purpose;
};

struct KeyspaceRange {
    std::string start_hex;
    std::string end_hex;
};

inline const KeyspaceRange kDefaultKeyspace{"0x400000000000000000", "0x7fffffffffffffffff"};

[[nodiscard]] inline bool IsCanonicalTargetAddress(std::string_view address) noexcept {
    return address == kTargetAddress;
}

/// Placeholder hash function hook populated later by integration tests.
[[nodiscard]] inline std::array<std::uint32_t, 5> Hash160FromBase58(std::string_view /*address*/) {
    return kTargetHash160;
}

}  // namespace puzzle71::constants
