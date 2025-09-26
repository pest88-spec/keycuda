#include "KeyhuntCore/adapters/bitcrack/conversions.h"

namespace bitcrack_adapter {

secp256k1::uint256 ToBitCrack(const puzzle71::core::UInt256& value) {
    unsigned int words[8];
    for (std::size_t i = 0; i < 4; ++i) {
        std::uint64_t limb = value.limbs[i];
        words[2 * i] = static_cast<unsigned int>(limb & 0xFFFFFFFFu);
        words[2 * i + 1] = static_cast<unsigned int>((limb >> 32) & 0xFFFFFFFFu);
    }
    return secp256k1::uint256(words, secp256k1::uint256::LittleEndian);
}

puzzle71::core::UInt256 FromBitCrack(const secp256k1::uint256& value) {
    unsigned int words[8];
    value.exportWords(words, 8, secp256k1::uint256::LittleEndian);
    puzzle71::core::UInt256 out = puzzle71::core::UInt256::Zero();
    for (std::size_t i = 0; i < 4; ++i) {
        std::uint64_t low = static_cast<std::uint64_t>(words[2 * i]);
        std::uint64_t high = static_cast<std::uint64_t>(words[2 * i + 1]);
        out.limbs[i] = (high << 32) | low;
    }
    return out;
}

std::array<unsigned char, 32> UInt256ToBytes(const puzzle71::core::UInt256& value) {
    std::array<unsigned char, 32> out{};
    for (std::size_t i = 0; i < value.limbs.size(); ++i) {
        std::uint64_t limb = value.limbs[i];
        for (std::size_t j = 0; j < 8; ++j) {
            out[31 - (i * 8 + j)] = static_cast<unsigned char>((limb >> (j * 8)) & 0xFF);
        }
    }
    return out;
}

}  // namespace bitcrack_adapter
