#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace puzzle71::core {

struct UInt256 {
    std::array<std::uint64_t, 4> limbs{};  // limbs[0] = least significant 64 bits

    static UInt256 Zero();
    static std::optional<UInt256> FromHex(std::string_view hex);

    std::string ToHex() const;
    int Compare(const UInt256& other) const;  // <0 if *this < other
    bool IsZero() const;
    bool FitsInUint64() const;
    std::uint64_t ToUint64() const;

    UInt256& Add(const UInt256& other);
    UInt256& AddUint64(std::uint64_t value);
    UInt256& Sub(const UInt256& other);

    UInt256 DivUint64(std::uint64_t value, std::uint64_t* remainder = nullptr) const;
    UInt256 SubtractOne() const;
};

UInt256 Difference(const UInt256& end, const UInt256& start);
UInt256 Incremented(const UInt256& value, std::uint64_t delta);

}  // namespace puzzle71::core
