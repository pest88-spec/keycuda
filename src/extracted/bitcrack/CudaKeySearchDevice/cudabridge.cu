/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/CudaKeySearchDevice/cudabridge.cu
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#include "cudabridge.h"

#include "puzzle71_kernel.h"

void callKeyFinderKernel(int blocks, int threads, int points, bool /*useDouble*/, int compression)
{
    dim3 grid(static_cast<unsigned int>(blocks), 1, 1);
    dim3 block(static_cast<unsigned int>(threads), 1, 1);

    cudaError_t launch_status = puzzle71::kernel::LaunchFusedKernel(grid, block, points, compression);
    if (launch_status != cudaSuccess) {
        throw cuda::CudaException(launch_status);
    }

    cudaError_t sync_status = cudaDeviceSynchronize();
    if (sync_status != cudaSuccess) {
        throw cuda::CudaException(sync_status);
    }
}

void waitForKernel()
{
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        throw cuda::CudaException(err);
    }

    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        throw cuda::CudaException(err);
    }
}
