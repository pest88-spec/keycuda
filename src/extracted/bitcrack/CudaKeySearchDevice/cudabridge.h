/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/CudaKeySearchDevice/cudabridge.h
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#ifndef _BRIDGE_H
#define _BRIDGE_H

#include<cuda.h>
#include<cuda_runtime.h>
#include<string>
#include "cudaUtil.h"
#include "secp256k1.h"


void callKeyFinderKernel(int blocks, int threads, int points, bool useDouble, int compression);

void waitForKernel();

cudaError_t setIncrementorPoint(const secp256k1::uint256 &x, const secp256k1::uint256 &y);
cudaError_t allocateChainBuf(unsigned int count);
void cleanupChainBuf();

#endif