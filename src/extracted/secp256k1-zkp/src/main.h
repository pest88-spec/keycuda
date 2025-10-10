/**
 * Extracted from secp256k1-zkp by Blockstream Research
 *
 * @origin       https://github.com/BlockstreamResearch/secp256k1-zkp
 * @origin_path  src/src/main.h
 * @origin_commit 42e75b613bc2c6b23d1ff75de49b9011f542baee
 * @origin_license MIT
 * @extracted_date   2025-10-09
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Namespace adaptation, integration into Puzzle71Solver build system
 * @spdx_license_identifier MIT
 */

#ifndef SECP256K1_MODULE_BPPP_MAIN_H
#define SECP256K1_MODULE_BPPP_MAIN_H

/* this type must be completed before any of the modules/bppp includes */
struct secp256k1_bppp_generators {
    size_t n;
    /* n total generators; includes both G_i and H_i */
    /* For BP++, the generators are G_i from [0..(n - 8)] and the last 8 values
    are generators are for H_i */
    secp256k1_ge* gens;
};

#endif
