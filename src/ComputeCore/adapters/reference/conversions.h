#pragma once

#include "core/uint256.h"
#include "secp256k1lib/secp256k1.h"

namespace reference_adapter {

secp256k1::uint256 ToReferenceFormat(const puzzle71::core::UInt256& value);

puzzle71::core::UInt256 FromReferenceFormat(const secp256k1::uint256& value);

std::array<unsigned char, 32> UInt256ToBytes(const puzzle71::core::UInt256& value);

}  // namespace reference_adapter
