/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/CudaKeySearchDevice/CudaHashLookup.h
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#ifndef _HASH_LOOKUP_HOST_H
#define _HASH_LOOKUP_HOST_H

#include <cuda_runtime.h>

class CudaHashLookup {

private:
	unsigned int *_bloomFilterPtr;

	cudaError_t setTargetBloomFilter(const std::vector<struct hash160> &targets);
	
	cudaError_t setTargetConstantMemory(const std::vector<struct hash160> &targets);
	
	unsigned int getOptimalBloomFilterBits(double p, size_t n);

	void cleanup();

	void initializeBloomFilter(const std::vector<struct hash160> &targets, unsigned int *filter, unsigned int mask);
	
	void initializeBloomFilter64(const std::vector<struct hash160> &targets, unsigned int *filter, unsigned long long mask);

public:

	CudaHashLookup()
	{
		_bloomFilterPtr = NULL;
	}

	~CudaHashLookup()
	{
		cleanup();
	}

	cudaError_t setTargets(const std::vector<struct hash160> &targets);
};

#endif