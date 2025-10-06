/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/CudaKeySearchDevice/CudaDeviceKeys.cuh
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#ifndef _EC_CUH
#define _EC_CUH

#include <cuda_runtime.h>

namespace ec {
	__device__ unsigned int *getXPtr();

	__device__ unsigned int *getYPtr();
}

#endif