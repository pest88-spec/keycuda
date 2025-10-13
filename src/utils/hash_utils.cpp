/**
 * Unified Hash Utilities Implementation
 * 
 * Implements P2-001: Code Duplication Optimization
 * - Adapters for OpenSSL SHA256 and RIPEMD160
 * - Unified Hash160 calculation
 * - DRY principle compliance
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/utils/hash_utils.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P2-001 code duplication optimization
 * @spdx_license_identifier MIT
 */

#include "hash_utils.h"
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace puzzle71 {
namespace utils {

// Compute SHA256 hash
bool HashUtils::sha256(const uint8_t* data, size_t length, uint8_t* digest) {
    if (!data || !digest) {
        return false;
    }
    
    SHA256_CTX ctx;
    if (SHA256_Init(&ctx) != 1) {
        return false;
    }
    
    if (SHA256_Update(&ctx, data, length) != 1) {
        return false;
    }
    
    if (SHA256_Final(digest, &ctx) != 1) {
        return false;
    }
    
    return true;
}

// Compute RIPEMD160 hash
bool HashUtils::ripemd160(const uint8_t* data, size_t length, uint8_t* digest) {
    if (!data || !digest) {
        return false;
    }
    
    RIPEMD160_CTX ctx;
    if (RIPEMD160_Init(&ctx) != 1) {
        return false;
    }
    
    if (RIPEMD160_Update(&ctx, data, length) != 1) {
        return false;
    }
    
    if (RIPEMD160_Final(digest, &ctx) != 1) {
        return false;
    }
    
    return true;
}

// Compute Hash160 (RIPEMD160(SHA256(data)))
bool HashUtils::hash160(const uint8_t* data, size_t length, uint8_t* digest) {
    if (!data || !digest) {
        return false;
    }
    
    // Step 1: SHA256(data)
    uint8_t sha256_digest[32];
    if (!sha256(data, length, sha256_digest)) {
        return false;
    }
    
    // Step 2: RIPEMD160(SHA256(data))
    if (!ripemd160(sha256_digest, 32, digest)) {
        return false;
    }
    
    return true;
}

// Compute SHA256 hash of a file
bool HashUtils::sha256_file(const char* filepath, uint8_t* digest) {
    if (!filepath || !digest) {
        return false;
    }
    
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    SHA256_CTX ctx;
    if (SHA256_Init(&ctx) != 1) {
        return false;
    }
    
    // Read file in chunks
    const size_t buffer_size = 8192;
    uint8_t buffer[buffer_size];
    
    while (file.read(reinterpret_cast<char*>(buffer), buffer_size) || file.gcount() > 0) {
        size_t bytes_read = file.gcount();
        if (SHA256_Update(&ctx, buffer, bytes_read) != 1) {
            return false;
        }
    }
    
    if (SHA256_Final(digest, &ctx) != 1) {
        return false;
    }
    
    return true;
}

// Convert digest to hex string
std::string HashUtils::to_hex(const uint8_t* digest, size_t length) {
    if (!digest || length == 0) {
        return "";
    }
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < length; ++i) {
        ss << std::setw(2) << static_cast<int>(digest[i]);
    }
    
    return ss.str();
}

// Convert hex string to digest
size_t HashUtils::from_hex(const std::string& hex_str, uint8_t* digest, size_t max_length) {
    if (hex_str.empty() || !digest || max_length == 0) {
        return 0;
    }
    
    // Hex string must have even length
    if (hex_str.length() % 2 != 0) {
        return 0;
    }
    
    size_t digest_length = hex_str.length() / 2;
    if (digest_length > max_length) {
        digest_length = max_length;
    }
    
    for (size_t i = 0; i < digest_length; ++i) {
        std::string byte_str = hex_str.substr(i * 2, 2);
        digest[i] = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
    }
    
    return digest_length;
}

} // namespace utils
} // namespace puzzle71

