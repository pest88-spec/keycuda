/**
 * @file msvc_compat.h
 * @brief MSVC Compatibility Layer for VanitySearch
 * 
 * This file provides MSVC-compatible implementations of GCC built-in functions
 * used in VanitySearch. This allows the code to compile on Windows with MSVC.
 * 
 * Iron Cage Protocol v5.0: NO-CRYPTO-REINVENTION
 * These are compiler intrinsic wrappers, not cryptographic algorithm implementations.
 * 
 * @date 2025-10-12
 */

#ifndef VANITYSEARCH_MSVC_COMPAT_H
#define VANITYSEARCH_MSVC_COMPAT_H

#ifdef _MSC_VER  // MSVC compiler

#include <intrin.h>
#include <stdint.h>

// Disable MSVC warnings for intrinsic function redefinition
#pragma warning(push)
#pragma warning(disable: 4164)  // intrinsic function not declared

/**
 * @brief MSVC-compatible implementation of __builtin_ia32_addcarryx_u64
 * 
 * Performs 64-bit addition with carry-in and carry-out.
 * 
 * @param carry_in Input carry flag (0 or 1)
 * @param a First operand
 * @param b Second operand
 * @param out Pointer to store the result
 * @return Output carry flag (0 or 1)
 */
inline unsigned char __builtin_ia32_addcarryx_u64(
    unsigned char carry_in,
    unsigned __int64 a,
    unsigned __int64 b,
    unsigned __int64* out
) {
    return _addcarry_u64(carry_in, a, b, out);
}

/**
 * @brief MSVC-compatible implementation of __builtin_ia32_sbb_u64
 * 
 * Performs 64-bit subtraction with borrow-in and borrow-out.
 * 
 * @param borrow_in Input borrow flag (0 or 1)
 * @param a Minuend
 * @param b Subtrahend
 * @param out Pointer to store the result
 * @return Output borrow flag (0 or 1)
 */
inline unsigned char __builtin_ia32_sbb_u64(
    unsigned char borrow_in,
    unsigned __int64 a,
    unsigned __int64 b,
    unsigned __int64* out
) {
    return _subborrow_u64(borrow_in, a, b, out);
}

/**
 * @brief MSVC-compatible implementation of __builtin_bswap64
 * 
 * Reverses the byte order of a 64-bit integer.
 * 
 * @param x Input value
 * @return Byte-swapped value
 */
inline unsigned __int64 __builtin_bswap64(unsigned __int64 x) {
    return _byteswap_uint64(x);
}

/**
 * @brief MSVC-compatible implementation of __rdtsc
 * 
 * Reads the processor's time-stamp counter.
 * 
 * @return Current value of the time-stamp counter
 */
inline unsigned __int64 __rdtsc(void) {
    return __rdtsc();  // MSVC has this intrinsic
}

#pragma warning(pop)

// MSVC alignment attribute
#define __attribute__(x)  // Remove GCC-specific attributes

#endif  // _MSC_VER

#endif  // VANITYSEARCH_MSVC_COMPAT_H

