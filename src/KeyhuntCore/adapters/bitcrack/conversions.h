#pragma once

#include "core/uint256.h"
#include "secp256k1lib/secp256k1.h"

namespace bitcrack_adapter {

secp256k1::uint256 ToBitCrack(const puzzle71::core::UInt256& value);

puzzle71::core::UInt256 FromBitCrack(const secp256k1::uint256& value);

std::array<unsigned char, 32> UInt256ToBytes(const puzzle71::core::UInt256& value);

}  // namespace bitcrack_adapter
