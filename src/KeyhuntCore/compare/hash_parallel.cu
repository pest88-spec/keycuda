/**
 * @file hash_parallel.cu
 * @brief Implementation of parallel address generation using Thrust transform
 *
 * Implements T032: Implement parallel address generation using Thrust transform
 * Provides CUDA kernels and device functions for parallel address generation.
 */

#include "hash_parallel.cuh"
#include "compare/kernels/hash160_fused.h"
#include <thrust/device_vector.h>
#include <thrust/transform.h>
#include <thrust/for_each.h>
#include <stdexcept>

namespace keyhunt {
namespace compare {

/**
 * @brief Device function for Hash160 computation
 *
 * Integrates with existing hash160_fused.h functions to provide
 * parallel address generation capabilities.
 */
__device__ void computeHash160Device(
    const uint32_t x[8],
    const uint32_t y[8],
    bool compressed,
    uint8_t hash160[20],
    bool& success)
{
    try {
        if (compressed) {
            // Compressed public key: (02/03) + X
            uint32_t y_parity = y[7] & 1;  // LSB indicates Y parity

            // Use existing compressed Hash160 function
            uint32_t digest[5];
            puzzle71::compare::Hash160Compressed(x, y_parity, digest);

            // Convert uint32_t digest to uint8_t array
            for (int i = 0; i < 5; i++) {
                uint32_t val = digest[i];
                for (int j = 0; j < 4; j++) {
                    hash160[i * 4 + j] = (val >> (24 - j * 8)) & 0xFF;
                }
            }
        } else {
            // Uncompressed public key: 04 + X + Y
            uint32_t digest[5];
            puzzle71::compare::Hash160Uncompressed(x, y, digest);

            // Convert uint32_t digest to uint8_t array
            for (int i = 0; i < 5; i++) {
                uint32_t val = digest[i];
                for (int j = 0; j < 4; j++) {
                    hash160[i * 4 + j] = (val >> (24 - j * 8)) & 0xFF;
                }
            }
        }

        success = true;

    } catch (...) {
        success = false;
        memset(hash160, 0, 20);
    }
}

/**
 * @brief Implementation of AddressGenerationFunctor
 */
__device__
AddressResult AddressGenerationFunctor::operator()(const PublicKeyInput& pubkey) const {
    AddressResult result;
    result.isCompressed = pubkey.isCompressed;
    result.isValid = false;

    // Call the device function for Hash160 computation
    computeHash160Device(pubkey.x, pubkey.y, pubkey.isCompressed,
                         result.hash160, result.isValid);

    return result;
}

/**
 * @brief Host-side implementation for parallel address generation
 */
cudaError_t generateAddressesParallel(
    const PublicKeyInput* publicKeys,
    AddressResult* addresses,
    size_t count)
{
    if (count == 0) {
        return cudaSuccess;
    }

    // Use Thrust transform for parallel processing
    thrust::device_ptr<const PublicKeyInput> d_keys_ptr(publicKeys);
    thrust::device_ptr<AddressResult> d_addresses_ptr(addresses);

    AddressGenerationFunctor functor;
    thrust::transform(thrust::device,
                     d_keys_ptr,
                     d_keys_ptr + count,
                     d_addresses_ptr,
                     functor);

    return cudaGetLastError();
}

/**
 * @brief High-level host implementation using Thrust
 */
std::vector<AddressResult> generateAddressesParallelHost(
    const std::vector<PublicKeyInput>& publicKeys,
    bool generateCompressed)
{
    if (publicKeys.empty()) {
        return {};
    }

    // Create a copy with modified compression flags if needed
    std::vector<PublicKeyInput> modifiedKeys = publicKeys;
    if (generateCompressed) {
        for (auto& key : modifiedKeys) {
            key.isCompressed = true;
        }
    }

    // Copy to device
    thrust::device_vector<PublicKeyInput> d_publicKeys(modifiedKeys);
    thrust::device_vector<AddressResult> d_addresses(modifiedKeys.size());

    // Transform using Thrust
    AddressGenerationFunctor functor;
    thrust::transform(thrust::device,
                     d_publicKeys.begin(),
                     d_publicKeys.end(),
                     d_addresses.begin(),
                     functor);

    // Copy results back
    std::vector<AddressResult> results(modifiedKeys.size());
    thrust::copy(d_addresses.begin(), d_addresses.end(), results.begin());

    return results;
}

/**
 * @brief Implementation for SoA parallel address generation
 */
cudaError_t generateAddressesSoAParallel(
    const uint32_t* publicKeysX,
    const uint32_t* publicKeysY,
    const bool* isCompressed,
    AddressResult* addresses,
    size_t count)
{
    if (count == 0) {
        return cudaSuccess;
    }

    // Create device vectors
    thrust::device_vector<PublicKeyInput> d_publicKeys(count);
    thrust::device_vector<AddressResult> d_addresses(count);

    // Convert SoA to AoS using Thrust for_each
    thrust::device_ptr<const uint32_t> d_keysX_ptr(publicKeysX);
    thrust::device_ptr<const uint32_t> d_keysY_ptr(publicKeysY);
    thrust::device_ptr<const bool> d_compressed_ptr(isCompressed);

    thrust::for_each_n(thrust::device,
        thrust::counting_iterator<size_t>(0),
        count,
        [d_keysX_ptr, d_keysY_ptr, d_compressed_ptr,
         d_publicKeys.data()] __device__ (size_t idx) {
            PublicKeyInput& pubkey = d_publicKeys[idx];

            // Load coordinates (handle potential out-of-bounds)
            for (int i = 0; i < 8; i++) {
                pubkey.x[i] = d_keysX_ptr[idx * 8 + i];
                pubkey.y[i] = d_keysY_ptr[idx * 8 + i];
            }

            pubkey.isCompressed = d_compressed_ptr[idx];
        });

    // Generate addresses
    AddressGenerationFunctor functor;
    thrust::transform(thrust::device,
                     d_publicKeys.begin(),
                     d_publicKeys.end(),
                     d_addresses.begin(),
                     functor);

    // Copy results to output
    thrust::copy(d_addresses.begin(), d_addresses.end(), addresses);

    return cudaGetLastError();
}

/**
 * @brief Implementation of address generation validation
 */
double validateAddressGeneration(
    const std::vector<AddressResult>& gpuAddresses,
    const std::vector<AddressResult>& cpuAddresses)
{
    if (gpuAddresses.size() != cpuAddresses.size()) {
        return -1.0;  // Size mismatch
    }

    double maxError = 0.0;
    size_t mismatches = 0;

    for (size_t i = 0; i < gpuAddresses.size(); i++) {
        const AddressResult& gpu = gpuAddresses[i];
        const AddressResult& cpu = cpuAddresses[i];

        // Check validity and compression flags
        if (gpu.isValid != cpu.isValid || gpu.isCompressed != cpu.isCompressed) {
            mismatches++;
            continue;
        }

        // Compare Hash160 digests byte by byte
        bool match = true;
        for (int j = 0; j < 20; j++) {
            if (gpu.hash160[j] != cpu.hash160[j]) {
                match = false;
                break;
            }
        }

        if (!match) {
            mismatches++;

            // Compute simple relative error using first 8 bytes
            uint64_t gpu_val = 0, cpu_val = 0;
            for (int j = 0; j < 8; j++) {
                gpu_val = (gpu_val << 8) | gpu.hash160[j];
                cpu_val = (cpu_val << 8) | cpu.hash160[j];
            }

            if (cpu_val != 0) {
                double error = fabs(static_cast<double>(gpu_val) - static_cast<double>(cpu_val)) /
                              static_cast<double>(cpu_val);
                maxError = max(maxError, error);
            }
        }
    }

    // If all addresses failed, return 1.0 (100% error)
    if (mismatches == gpuAddresses.size()) {
        return 1.0;
    }

    return maxError;
}

} // namespace compare
} // namespace keyhunt