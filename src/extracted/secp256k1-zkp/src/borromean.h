/**
 * Extracted from secp256k1-zkp by Blockstream Research
 *
 * @origin       https://github.com/BlockstreamResearch/secp256k1-zkp
 * @origin_path  src/src/borromean.h
 * @origin_commit 42e75b613bc2c6b23d1ff75de49b9011f542baee
 * @origin_license MIT
 * @extracted_date   2025-10-09
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Namespace adaptation, integration into Puzzle71Solver build system
 * @spdx_license_identifier MIT
 */

/**********************************************************************
 * Copyright (c) 2014, 2015 Gregory Maxwell                          *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/


#ifndef SECP256K1_BORROMEAN_H
#define SECP256K1_BORROMEAN_H

#include "../../scalar.h"
#include "../../field.h"
#include "../../group.h"
#include "../../ecmult.h"
#include "../../ecmult_gen.h"

static int secp256k1_borromean_verify(secp256k1_scalar *evalues, const unsigned char *e0, const secp256k1_scalar *s,
 const secp256k1_gej *pubs, const size_t *rsizes, size_t nrings, const unsigned char *m, size_t mlen);

static int secp256k1_borromean_sign(const secp256k1_ecmult_gen_context *ecmult_gen_ctx,
 unsigned char *e0, secp256k1_scalar *s, const secp256k1_gej *pubs, const secp256k1_scalar *k, const secp256k1_scalar *sec,
 const size_t *rsizes, const size_t *secidx, size_t nrings, const unsigned char *m, size_t mlen);

#endif
