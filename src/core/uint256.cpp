#include "core/uint256.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <limits>

namespace puzzle71::core {

namespace {

constexpr std::size_t kNibbleCount = 64;  // 256 bits / 4 bits

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

UInt256 UInt256::Zero() {
    return UInt256{};
}

std::optional<UInt256> UInt256::FromHex(std::string_view hex) {
    UInt256 value = UInt256::Zero();
    if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) {
        hex.remove_prefix(2);
    }
    if (hex.empty()) {
        return value;
    }
    if (hex.size() > kNibbleCount) {
        return std::nullopt;
    }

    for (char c : hex) {
        int digit = HexValue(c);
        if (digit < 0) {
            return std::nullopt;
        }
        unsigned __int128 carry = static_cast<unsigned __int128>(digit);
        for (std::size_t i = 0; i < value.limbs.size(); ++i) {
            unsigned __int128 shifted = (static_cast<unsigned __int128>(value.limbs[i]) << 4) | carry;
            value.limbs[i] = static_cast<std::uint64_t>(shifted);
            carry = shifted >> 64;
        }
        if (carry != 0) {
            return std::nullopt;  // overflow beyond 256 bits
        }
    }
    return value;
}

std::string UInt256::ToHex() const {
    // Fixed-width output: always 64 hex digits (256 bits / 4 bits per digit)
    // This prevents loss of leading zeros which caused private key corruption
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0');
    for (std::size_t i = limbs.size(); i-- > 0;) {
        oss << std::setw(16) << limbs[i];
    }
    return oss.str();
}

int UInt256::Compare(const UInt256& other) const {
    for (std::size_t i = limbs.size(); i-- > 0;) {
        if (limbs[i] < other.limbs[i]) return -1;
        if (limbs[i] > other.limbs[i]) return 1;
    }
    return 0;
}

bool UInt256::IsZero() const {
    return std::all_of(limbs.begin(), limbs.end(), [](std::uint64_t v) { return v == 0; });
}

bool UInt256::FitsInUint64() const {
    return limbs[1] == 0 && limbs[2] == 0 && limbs[3] == 0;
}

std::uint64_t UInt256::ToUint64() const {
    if (!FitsInUint64()) {
        throw std::runtime_error("UInt256 does not fit in uint64_t");
    }
    return limbs[0];
}

HOST_DEVICE UInt256& UInt256::Add(const UInt256& other) {
    unsigned __int128 carry = 0;
    for (std::size_t i = 0; i < limbs.size(); ++i) {
        unsigned __int128 sum = static_cast<unsigned __int128>(limbs[i]) + other.limbs[i] + carry;
        limbs[i] = static_cast<std::uint64_t>(sum);
        carry = sum >> 64;
    }
    return *this;
}

HOST_DEVICE UInt256& UInt256::AddUint64(std::uint64_t value) {
    unsigned __int128 sum = static_cast<unsigned __int128>(limbs[0]) + value;
    limbs[0] = static_cast<std::uint64_t>(sum);
    unsigned __int128 carry = sum >> 64;
    for (std::size_t i = 1; i < limbs.size() && carry; ++i) {
        unsigned __int128 tmp = static_cast<unsigned __int128>(limbs[i]) + carry;
        limbs[i] = static_cast<std::uint64_t>(tmp);
        carry = tmp >> 64;
    }
    return *this;
}

HOST_DEVICE UInt256& UInt256::Sub(const UInt256& other) {
    unsigned __int128 borrow = 0;
    for (std::size_t i = 0; i < limbs.size(); ++i) {
        unsigned __int128 minuend = static_cast<unsigned __int128>(limbs[i]);
        unsigned __int128 subtrahend = static_cast<unsigned __int128>(other.limbs[i]) + borrow;
        if (minuend >= subtrahend) {
            limbs[i] = static_cast<std::uint64_t>(minuend - subtrahend);
            borrow = 0;
        } else {
            limbs[i] = static_cast<std::uint64_t>((static_cast<unsigned __int128>(1) << 64) + minuend - subtrahend);
            borrow = 1;
        }
    }
    return *this;
}

UInt256 UInt256::DivUint64(std::uint64_t value, std::uint64_t* remainder) const {
    if (value == 0) {
        throw std::runtime_error("division by zero");
    }
    UInt256 result = UInt256::Zero();
    unsigned __int128 rem = 0;
    for (std::size_t i = limbs.size(); i-- > 0;) {
        unsigned __int128 dividend = (rem << 64) | limbs[i];
        std::uint64_t q = static_cast<std::uint64_t>(dividend / value);
        rem = dividend % value;
        result.limbs[i] = q;
    }
    if (remainder) {
        *remainder = static_cast<std::uint64_t>(rem);
    }
    return result;
}

UInt256 UInt256::SubtractOne() const {
    UInt256 tmp = *this;
    bool underflow = true;
    for (std::size_t i = 0; i < tmp.limbs.size(); ++i) {
        if (tmp.limbs[i] > 0) {
            --tmp.limbs[i];
            underflow = false;
            break;
        }
        tmp.limbs[i] = std::numeric_limits<std::uint64_t>::max();
    }
    if (underflow) {
        return UInt256::Zero();
    }
    return tmp;
}

UInt256 Difference(const UInt256& end, const UInt256& start) {
    UInt256 diff = end;
    diff.Sub(start);
    return diff;
}

UInt256 Incremented(const UInt256& value, std::uint64_t delta) {
    UInt256 result = value;
    result.AddUint64(delta);
    return result;
}

}  // namespace puzzle71::core
