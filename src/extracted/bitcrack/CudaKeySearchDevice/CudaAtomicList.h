/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/CudaKeySearchDevice/CudaAtomicList.h
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#ifndef _ATOMIC_LIST_HOST_H
#define _ATOMIC_LIST_HOST_H

#include <cuda_runtime.h>

/**
 A list that multiple device threads can append items to. Items can be
 read and removed by the host
 */
class CudaAtomicList {

private:
	void *_devPtr;

	void *_hostPtr;

	unsigned int *_countHostPtr;

	unsigned int *_countDevPtr;

	unsigned int _maxSize;

	unsigned int _itemSize;

public:

	CudaAtomicList()
	{
		_devPtr = NULL;
		_hostPtr = NULL;
		_countHostPtr = NULL;
		_countDevPtr = NULL;
		_maxSize = 0;
		_itemSize = 0;
	}

	~CudaAtomicList()
	{
		cleanup();
	}

	cudaError_t init(unsigned int itemSize, unsigned int maxItems);

	unsigned int read(void *dest, unsigned int count);

	unsigned int size();

	void clear();

    void cleanup();

};

#endif