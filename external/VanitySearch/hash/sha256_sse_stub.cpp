#include <cstdint>
#include "sha256.h"
#include "ripemd160.h"

// Stub implementations for SSE functions
// These are simple wrappers around the standard C++ versions

void sha256sse_1B(uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
                  uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3) {
    // Call standard version for each input
    sha256(d0, 64, d0);
    sha256(d1, 64, d1);
    sha256(d2, 64, d2);
    sha256(d3, 64, d3);
}

void sha256sse_2B(uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
                  uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3) {
    // Call standard version for each input
    sha256(d0, 128, d0);
    sha256(d1, 128, d1);
    sha256(d2, 128, d2);
    sha256(d3, 128, d3);
}

// Overloaded version for single input (used in some places)
void sha256sse_checksum(uint8_t *input, int length, uint8_t *checksum) {
    sha256_checksum(input, length, checksum);
}

void sha256sse_checksum(uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
                        uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3) {
    // Call standard version for each input
    sha256_checksum(d0, 25, d0);
    sha256_checksum(d1, 25, d1);
    sha256_checksum(d2, 25, d2);
    sha256_checksum(d3, 25, d3);
}

// Overloaded version for single input
void ripemd160sse_32(uint8_t *input, uint8_t *output) {
    ripemd160(input, 32, output);
}

void ripemd160sse_32(uint8_t *i0, uint8_t *i1, uint8_t *i2, uint8_t *i3,
                     uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3) {
    // Call standard version for each input
    ripemd160(i0, 32, d0);
    ripemd160(i1, 32, d1);
    ripemd160(i2, 32, d2);
    ripemd160(i3, 32, d3);
}

