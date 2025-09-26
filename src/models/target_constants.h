#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace puzzle71::constants {

constexpr std::string_view kTargetAddress = "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU";
constexpr std::array<std::uint32_t, 5> kTargetHash160 = {
    0xf8455b22u, 0xfa469a40u, 0x654450d3u, 0x63959a3bu, 0x932924b4u};

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
