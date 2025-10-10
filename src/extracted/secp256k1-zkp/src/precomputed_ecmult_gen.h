/**
 * Extracted from secp256k1-zkp by Blockstream Research
 *
 * @origin       https://github.com/BlockstreamResearch/secp256k1-zkp
 * @origin_path  src/src/precomputed_ecmult_gen.h
 * @origin_commit 42e75b613bc2c6b23d1ff75de49b9011f542baee
 * @origin_license MIT
 * @extracted_date   2025-10-09
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Namespace adaptation, integration into Puzzle71Solver build system
 * @spdx_license_identifier MIT
 */

/*********************************************************************************
 * Copyright (c) 2013, 2014, 2015, 2021 Thomas Daede, Cory Fields, Pieter Wuille *
 * Distributed under the MIT software license, see the accompanying              *
 * file COPYING or https://www.opensource.org/licenses/mit-license.php.          *
 *********************************************************************************/

#ifndef SECP256K1_PRECOMPUTED_ECMULT_GEN_H
#define SECP256K1_PRECOMPUTED_ECMULT_GEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "group.h"
#include "ecmult_gen.h"
#ifdef EXHAUSTIVE_TEST_ORDER
static secp256k1_ge_storage secp256k1_ecmult_gen_prec_table[ECMULT_GEN_PREC_N(ECMULT_GEN_PREC_BITS)][ECMULT_GEN_PREC_G(ECMULT_GEN_PREC_BITS)];
#else
extern const secp256k1_ge_storage secp256k1_ecmult_gen_prec_table[ECMULT_GEN_PREC_N(ECMULT_GEN_PREC_BITS)][ECMULT_GEN_PREC_G(ECMULT_GEN_PREC_BITS)];
#endif /* defined(EXHAUSTIVE_TEST_ORDER) */

#ifdef __cplusplus
}
#endif

#endif /* SECP256K1_PRECOMPUTED_ECMULT_GEN_H */
