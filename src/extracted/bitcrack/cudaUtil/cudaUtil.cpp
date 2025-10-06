/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/cudaUtil/cudaUtil.cpp
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#include "cudaUtil.h"


cuda::CudaDeviceInfo cuda::getDeviceInfo(int device)
{
	cuda::CudaDeviceInfo devInfo;

	cudaDeviceProp properties;
	cudaError_t err = cudaSuccess;

	err = cudaSetDevice(device);

	if(err) {
		throw cuda::CudaException(err);
	}

	err = cudaGetDeviceProperties(&properties, device);
	
	if(err) {
		throw cuda::CudaException(err);
	}

	devInfo.id = device;
	devInfo.major = properties.major;
	devInfo.minor = properties.minor;
	devInfo.mpCount = properties.multiProcessorCount;
	devInfo.mem = properties.totalGlobalMem;
	devInfo.name = std::string(properties.name);

	int cores = 0;
	switch(devInfo.major) {
	case 1:
		cores = 8;
		break;
	case 2:
        if(devInfo.minor == 0) {
            cores = 32;
        } else {
            cores = 48;
        }
		break;
	case 3:
		cores = 192;
		break;
	case 5:
		cores = 128;
		break;
	case 6:
        if(devInfo.minor == 1 || devInfo.minor == 2) {
            cores = 128;
        } else {
            cores = 64;
        }
        break;
	case 7:
		cores = 64;
		break;
    default:
        cores = 8;
        break;
	}
	devInfo.cores = cores;

	return devInfo;
}


std::vector<cuda::CudaDeviceInfo> cuda::getDevices()
{
	int count = getDeviceCount();

	std::vector<CudaDeviceInfo> devList;

	for(int device = 0; device < count; device++) {
		devList.push_back(getDeviceInfo(device));
	}

	return devList;
}

int cuda::getDeviceCount()
{
	int count = 0;

	cudaError_t err = cudaGetDeviceCount(&count);

    if(err) {
        throw cuda::CudaException(err);
    }

	return count;
}