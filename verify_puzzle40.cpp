#include "src/crypto/secp256k1_adapter.h"
#include "src/core/uint256.h"
#include "third_party/BitCrack/AddressUtil/AddressUtil.h"
#include <iostream>
#include <iomanip>

int main() {
    // Known Puzzle 40 private key
    auto priv_key = puzzle71::core::UInt256::FromHex("0x0e9ae4933d6");
    if (!priv_key) {
        std::cerr << "Failed to parse private key" << std::endl;
        return 1;
    }

    std::cout << "=== Puzzle 40 Verification ===" << std::endl;
    std::cout << "Private Key: " << priv_key->ToHex() << std::endl;
    std::cout << std::endl;

    // Derive public key using secp256k1
    auto pub = puzzle71::crypto::DerivePublicKey(*priv_key);
    if (!pub || !pub->valid) {
        std::cerr << "Failed to derive public key" << std::endl;
        return 1;
    }

    std::cout << "Public Key (uncompressed):" << std::endl;
    std::cout << "  ";
    for (size_t i = 0; i < 65 && i < pub->uncompressed.size(); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)pub->uncompressed[i];
    }
    std::cout << std::dec << std::endl << std::endl;

    std::cout << "Public Key (compressed):" << std::endl;
    std::cout << "  ";
    for (size_t i = 0; i < 33 && i < pub->compressed.size(); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)pub->compressed[i];
    }
    std::cout << std::dec << std::endl << std::endl;

    // Generate address using BitCrack's Base58 encoding
    std::array<unsigned int, 5> hash160{};

    // Hash compressed public key: SHA256 -> RIPEMD160
    unsigned char sha256_result[32];
    SHA256(pub->compressed.data(), pub->compressed.size(), sha256_result);

    unsigned char ripemd160_result[20];
    RIPEMD160(sha256_result, 32, ripemd160_result);

    // Convert to uint32 array (big-endian)
    for (int i = 0; i < 5; ++i) {
        hash160[i] = (ripemd160_result[i*4] << 24) |
                     (ripemd160_result[i*4+1] << 16) |
                     (ripemd160_result[i*4+2] << 8) |
                     ripemd160_result[i*4+3];
    }

    std::cout << "HASH160: ";
    for (int i = 0; i < 5; ++i) {
        std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << hash160[i];
        if (i < 4) std::cout << " ";
    }
    std::cout << std::dec << std::endl << std::endl;

    // Generate Bitcoin address (P2PKH)
    std::string address = Base58::encodeAddress(ripemd160_result, 20);
    std::cout << "Bitcoin Address: " << address << std::endl;
    std::cout << std::endl;

    // Expected address
    std::string expected = "1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv";
    std::cout << "Expected:        " << expected << std::endl;
    std::cout << "Match: " << (address == expected ? "✓ YES" : "✗ NO") << std::endl;

    return (address == expected) ? 0 : 1;
}
