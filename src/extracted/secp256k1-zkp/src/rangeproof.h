/**
 * Extracted from secp256k1-zkp by Blockstream Research
 *
 * @origin       https://github.com/BlockstreamResearch/secp256k1-zkp
 * @origin_path  src/src/rangeproof.h
 * @origin_commit 42e75b613bc2c6b23d1ff75de49b9011f542baee
 * @origin_license MIT
 * @extracted_date   2025-10-09
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Namespace adaptation, integration into Puzzle71Solver build system
 * @spdx_license_identifier MIT
 */

/**********************************************************************
 * Copyright (c) 2015 Gregory Maxwell                                 *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#ifndef SECP256K1_RANGEPROOF_H
#define SECP256K1_RANGEPROOF_H

#include "../../scalar.h"
#include "../../group.h"
#include "../../ecmult.h"
#include "../../ecmult_gen.h"

static int secp256k1_rangeproof_verify_impl(const secp256k1_ecmult_gen_context* ecmult_gen_ctx,
 unsigned char *blindout, uint64_t *value_out, unsigned char *message_out, size_t *outlen, const unsigned char *nonce,
 uint64_t *min_value, uint64_t *max_value, const secp256k1_ge *commit, const unsigned char *proof, size_t plen,
 const unsigned char *extra_commit, size_t extra_commit_len, const secp256k1_ge* genp);

#endif
