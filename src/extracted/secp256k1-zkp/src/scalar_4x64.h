/**
 * Extracted from secp256k1-zkp by Blockstream Research
 *
 * @origin       https://github.com/BlockstreamResearch/secp256k1-zkp
 * @origin_path  src/src/scalar_4x64.h
 * @origin_commit 42e75b613bc2c6b23d1ff75de49b9011f542baee
 * @origin_license MIT
 * @extracted_date   2025-10-09
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Namespace adaptation, integration into Puzzle71Solver build system
 * @spdx_license_identifier MIT
 */

/***********************************************************************
 * Copyright (c) 2014 Pieter Wuille                                    *
 * Distributed under the MIT software license, see the accompanying    *
 * file COPYING or https://www.opensource.org/licenses/mit-license.php.*
 ***********************************************************************/

#ifndef SECP256K1_SCALAR_REPR_H
#define SECP256K1_SCALAR_REPR_H

#include <stdint.h>

/** A scalar modulo the group order of the secp256k1 curve. */
typedef struct {
    uint64_t d[4];
} secp256k1_scalar;

#define SECP256K1_SCALAR_CONST(d7, d6, d5, d4, d3, d2, d1, d0) {{((uint64_t)(d1)) << 32 | (d0), ((uint64_t)(d3)) << 32 | (d2), ((uint64_t)(d5)) << 32 | (d4), ((uint64_t)(d7)) << 32 | (d6)}}

#endif /* SECP256K1_SCALAR_REPR_H */
