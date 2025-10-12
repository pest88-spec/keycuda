/**
 * @file parity_checker.cu
 * @brief Implementation of parallel validation utilities
 *
 * Implements T030: Replace serial validation loop with parallel Thrust reduce
 * Provides GPU-accelerated validation statistics computation.
 */

#include "parity_checker.cuh"
#include <thrust/device_vector.h>
#include <thrust/host_vector.h>
#include <thrust/transform.h>
#include <thrust/reduce.h>
#include <thrust/iterator/zip_iterator.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/iterator/constant_iterator.h>
#include <thrust/logical.h>
#include <thrust/execution_policy.h>

#include <cuda_runtime.h>
#include <stdexcept>
#include <iostream>
#include <cmath>

namespace keyhunt {
namespace validation {

/**
 * @brief CUDA kernel for computing validation metrics in parallel
 *
 * Each thread processes one key comparison and computes:
 * - Whether keys match (pass/fail)
 * - Relative error if they don't match
 */
__global__ void computeValidationMetricsKernel(
    const uint8_t* __restrict__ gpuPublicKeys,
    const uint8_t* __restrict__ cpuPublicKeys,
    const bool* __restrict__ cpuSuccessFlags,
    double* __restrict__ relativeErrors,
    bool* __restrict__ matchResults,
    size_t keyCount,
    size_t keySize)
{
    size_t tid = blockIdx.x * blockDim.x + threadIdx.x;

    if (tid >= keyCount) {
        return;
    }

    const uint8_t* gpuKey = &gpuPublicKeys[tid * keySize];
    const uint8_t* cpuKey = &cpuPublicKeys[tid * keySize];
    bool cpuSuccess = cpuSuccessFlags[tid];

    // Initialize results
    relativeErrors[tid] = 0.0;
    matchResults[tid] = false;

    // Skip if CPU computation failed
    if (!cpuSuccess) {
        matchResults[tid] = true;  // Don't count as failure
        return;
    }

    // Compare byte-by-byte
    bool match = true;
    for (size_t i = 0; i < keySize; i++) {
        if (gpuKey[i] != cpuKey[i]) {
            match = false;
            break;
        }
    }

    matchResults[tid] = match;

    if (!match) {
        // Compute relative error using first 8 bytes as approximation
        uint64_t exp_val = 0, act_val = 0;
        size_t compareBytes = std::min(keySize, size_t(8));

        for (size_t i = 1; i < compareBytes; i++) {  // Skip 0x04 prefix
            exp_val = (exp_val << 8) | cpuKey[i];
            act_val = (act_val << 8) | gpuKey[i];
        }

        if (exp_val == 0) {
            relativeErrors[tid] = (act_val == 0) ? 0.0 : 1.0;
        } else {
            relativeErrors[tid] = fabs(static_cast<double>(act_val) - static_cast<double>(exp_val)) /
                                   static_cast<double>(exp_val);
        }
    }
}

ValidationMetrics validateParityParallel(
    const uint8_t* d_gpuPublicKeys,
    const std::vector<uint8_t>& h_cpuPublicKeys,
    const std::vector<bool>& h_cpuSuccessFlags,
    size_t keyCount,
    size_t keySize)
{
    ValidationMetrics result;

    if (keyCount == 0) {
        return result;
    }

    // Validate input sizes
    if (h_cpuPublicKeys.size() != keyCount * keySize) {
        throw std::invalid_argument("CPU public keys size mismatch");
    }
    if (h_cpuSuccessFlags.size() != keyCount) {
        throw std::invalid_argument("CPU success flags size mismatch");
    }

    // Allocate device memory for CPU data
    uint8_t* d_cpuPublicKeys;
    bool* d_cpuSuccessFlags;
    double* d_relativeErrors;
    bool* d_matchResults;

    cudaError_t err;

    err = cudaMalloc(&d_cpuPublicKeys, h_cpuPublicKeys.size());
    if (err != cudaSuccess) {
        throw std::runtime_error("Failed to allocate device memory for CPU public keys");
    }

    err = cudaMalloc(&d_cpuSuccessFlags, h_cpuSuccessFlags.size() * sizeof(bool));
    if (err != cudaSuccess) {
        cudaFree(d_cpuPublicKeys);
        throw std::runtime_error("Failed to allocate device memory for CPU success flags");
    }

    err = cudaMalloc(&d_relativeErrors, keyCount * sizeof(double));
    if (err != cudaSuccess) {
        cudaFree(d_cpuPublicKeys);
        cudaFree(d_cpuSuccessFlags);
        throw std::runtime_error("Failed to allocate device memory for relative errors");
    }

    err = cudaMalloc(&d_matchResults, keyCount * sizeof(bool));
    if (err != cudaSuccess) {
        cudaFree(d_cpuPublicKeys);
        cudaFree(d_cpuSuccessFlags);
        cudaFree(d_relativeErrors);
        throw std::runtime_error("Failed to allocate device memory for match results");
    }

    try {
        // Copy CPU data to device
        err = cudaMemcpy(d_cpuPublicKeys, h_cpuPublicKeys.data(),
                        h_cpuPublicKeys.size(), cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            throw std::runtime_error("Failed to copy CPU public keys to device");
        }

        err = cudaMemcpy(d_cpuSuccessFlags, h_cpuSuccessFlags.data(),
                        h_cpuSuccessFlags.size() * sizeof(bool), cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            throw std::runtime_error("Failed to copy CPU success flags to device");
        }

        // Launch kernel to compute validation metrics
        int blockSize = 256;
        int gridSize = (keyCount + blockSize - 1) / blockSize;

        computeValidationMetricsKernel<<<gridSize, blockSize>>>(
            d_gpuPublicKeys, d_cpuPublicKeys, d_cpuSuccessFlags,
            d_relativeErrors, d_matchResults, keyCount, keySize);

        err = cudaGetLastError();
        if (err != cudaSuccess) {
            throw std::runtime_error("Kernel launch failed: " + std::string(cudaGetErrorString(err)));
        }

        err = cudaDeviceSynchronize();
        if (err != cudaSuccess) {
            throw std::runtime_error("Kernel synchronization failed");
        }

        // Use Thrust device vectors for parallel reduction
        thrust::device_ptr<double> d_errors_ptr(d_relativeErrors);
        thrust::device_ptr<bool> d_matches_ptr(d_matchResults);

        thrust::device_vector<double> d_errors(d_errors_ptr, d_errors_ptr + keyCount);
        thrust::device_vector<bool> d_matches(d_matches_ptr, d_matches_ptr + keyCount);

        result = computeValidationStatistics(d_errors, d_matches);

    } catch (...) {
        // Clean up on exception
        cudaFree(d_cpuPublicKeys);
        cudaFree(d_cpuSuccessFlags);
        cudaFree(d_relativeErrors);
        cudaFree(d_matchResults);
        throw;
    }

    // Clean up device memory
    cudaFree(d_cpuPublicKeys);
    cudaFree(d_cpuSuccessFlags);
    cudaFree(d_relativeErrors);
    cudaFree(d_matchResults);

    return result;
}

ValidationMetrics validateParityParallelHost(
    const std::vector<uint8_t>& gpuPublicKeys,
    const std::vector<uint8_t>& cpuPublicKeys,
    const std::vector<bool>& cpuSuccessFlags)
{
    if (gpuPublicKeys.size() != cpuPublicKeys.size()) {
        throw std::invalid_argument("GPU and CPU public key vectors must have same size");
    }

    size_t keyCount = cpuSuccessFlags.size();
    size_t keySize = (keyCount > 0) ? (gpuPublicKeys.size() / keyCount) : 0;

    if (keySize == 0) {
        return ValidationMetrics();
    }

    // Allocate device memory for GPU public keys
    uint8_t* d_gpuPublicKeys;
    cudaError_t err = cudaMalloc(&d_gpuPublicKeys, gpuPublicKeys.size());
    if (err != cudaSuccess) {
        throw std::runtime_error("Failed to allocate device memory for GPU public keys");
    }

    try {
        // Copy GPU public keys to device
        err = cudaMemcpy(d_gpuPublicKeys, gpuPublicKeys.data(),
                        gpuPublicKeys.size(), cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            throw std::runtime_error("Failed to copy GPU public keys to device");
        }

        // Call the main validation function
        return validateParityParallel(d_gpuPublicKeys, cpuPublicKeys,
                                    cpuSuccessFlags, keyCount, keySize);

    } catch (...) {
        cudaFree(d_gpuPublicKeys);
        throw;
    }

    cudaFree(d_gpuPublicKeys);
}

ValidationMetrics computeValidationStatistics(
    thrust::device_vector<double>& errors,
    thrust::device_vector<bool>& matches)
{
    ValidationMetrics result;
    result.totalComparisons = errors.size();

    if (result.totalComparisons == 0) {
        return result;
    }

    // Count matches using parallel reduction
    result.passedComparisons = thrust::reduce(
        thrust::device, matches.begin(), matches.end(), 0u, thrust::plus<uint32_t>());

    result.failedComparisons = result.totalComparisons - result.passedComparisons;

    // Compute maximum relative error using parallel reduction
    result.maxRelativeError = thrust::reduce(
        thrust::device, errors.begin(), errors.end(), 0.0, thrust::maximum<double>());

    // Compute sum of relative errors using parallel reduction
    result.sumRelativeError = thrust::reduce(
        thrust::device, errors.begin(), errors.end(), 0.0, thrust::plus<double>());

    // Compute mean relative error for failed cases
    result.meanRelativeError = (result.failedComparisons > 0) ?
        (result.sumRelativeError / result.failedComparisons) : 0.0;

    return result;
}

} // namespace validation
} // namespace keyhunt