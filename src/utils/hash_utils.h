/**
 * Unified Hash Utilities
 * 
 * Implements P2-001: Code Duplication Optimization
 * - Unified SHA256, RIPEMD160, Hash160 interfaces
 * - Adapter pattern for reference implementations
 * - DRY principle compliance
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/utils/hash_utils.h
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P2-001 code duplication optimization
 * @spdx_license_identifier MIT
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace puzzle71 {
namespace utils {

/**
 * Unified Hash Utilities
 * 
 * Provides unified interfaces for SHA256, RIPEMD160, and Hash160
 * calculations. Uses adapter pattern to wrap reference implementations
 * from external/ directories.
 */
class HashUtils {
public:
    /**
     * Compute SHA256 hash
     * 
     * @param data Input data
     * @param length Data length in bytes
     * @param digest Output: 32-byte SHA256 digest
     * @return True if successful
     */
    static bool sha256(const uint8_t* data, size_t length, uint8_t* digest);
    
    /**
     * Compute RIPEMD160 hash
     * 
     * @param data Input data
     * @param length Data length in bytes
     * @param digest Output: 20-byte RIPEMD160 digest
     * @return True if successful
     */
    static bool ripemd160(const uint8_t* data, size_t length, uint8_t* digest);
    
    /**
     * Compute Hash160 (RIPEMD160(SHA256(data)))
     * 
     * This is the standard Bitcoin address generation hash.
     * 
     * @param data Input data (typically public key)
     * @param length Data length in bytes
     * @param digest Output: 20-byte Hash160 digest
     * @return True if successful
     */
    static bool hash160(const uint8_t* data, size_t length, uint8_t* digest);
    
    /**
     * Compute SHA256 hash of a file
     * 
     * @param filepath Path to file
     * @param digest Output: 32-byte SHA256 digest
     * @return True if successful
     */
    static bool sha256_file(const char* filepath, uint8_t* digest);
    
    /**
     * Convert digest to hex string
     * 
     * @param digest Digest bytes
     * @param length Digest length
     * @return Hex string representation
     */
    static std::string to_hex(const uint8_t* digest, size_t length);
    
    /**
     * Convert hex string to digest
     * 
     * @param hex_str Hex string
     * @param digest Output: digest bytes
     * @param max_length Maximum digest length
     * @return Number of bytes written
     */
    static size_t from_hex(const std::string& hex_str, uint8_t* digest, size_t max_length);
};

} // namespace utils
} // namespace puzzle71

