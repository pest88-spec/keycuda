/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/cudaUtil/cudaUtil.h
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#ifndef _CUDA_UTIL_H
#define _CUDA_UTIL_H

#include <cuda.h>
#include <cuda_runtime.h>

#include <string>
#include <vector>

namespace cuda {
	typedef struct {

		int id;
		int major;
		int minor;
		int mpCount;
		int cores;
		uint64_t mem;
		std::string name;

	}CudaDeviceInfo;

	class CudaException
	{
	public:
		cudaError_t error;
		std::string msg;

		CudaException(cudaError_t err)
		{
			this->error = err;
			this->msg = std::string(cudaGetErrorString(err));
		}
	};

	CudaDeviceInfo getDeviceInfo(int device);

	std::vector<CudaDeviceInfo> getDevices();

	int getDeviceCount();
}
#endif