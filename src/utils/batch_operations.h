/**
 * Unified Batch Operations
 * 
 * Implements P2-001: Code Duplication Optimization
 * - Unified batch addition, batch inverse interfaces
 * - CPU and GPU implementations
 * - DRY principle compliance
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/utils/batch_operations.h
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P2-001 code duplication optimization
 * @spdx_license_identifier MIT
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace puzzle71 {
namespace utils {

/**
 * Unified Batch Operations
 * 
 * Provides unified interfaces for batch ECC operations.
 * Supports both CPU and GPU implementations.
 */
class BatchOperations {
public:
    /**
     * Batch point addition (CPU)
     * 
     * Computes P[i] = P[i] + Q for all i in parallel
     * 
     * @param points Array of points (modified in-place)
     * @param count Number of points
     * @param q Point to add to all points
     * @return True if successful
     */
    static bool batch_point_add_cpu(
        uint32_t* points_x,
        uint32_t* points_y,
        size_t count,
        const uint32_t* q_x,
        const uint32_t* q_y
    );
    
    /**
     * Batch modular inverse (CPU)
     * 
     * Computes inv[i] = 1/values[i] mod p for all i
     * Uses Montgomery batch inversion algorithm
     * 
     * @param values Input values
     * @param inv Output: modular inverses
     * @param count Number of values
     * @param modulus Modulus p
     * @return True if successful
     */
    static bool batch_mod_inverse_cpu(
        const uint32_t* values,
        uint32_t* inv,
        size_t count,
        const uint32_t* modulus
    );
    
    /**
     * Batch scalar multiplication (CPU)
     * 
     * Computes P[i] = k[i] * G for all i
     * 
     * @param scalars Array of scalars
     * @param points_x Output: x coordinates
     * @param points_y Output: y coordinates
     * @param count Number of scalars
     * @return True if successful
     */
    static bool batch_scalar_mul_cpu(
        const uint32_t* scalars,
        uint32_t* points_x,
        uint32_t* points_y,
        size_t count
    );

#ifdef __CUDACC__
    /**
     * Batch point addition (GPU)
     * 
     * CUDA kernel for parallel point addition
     * 
     * @param points_x Device array of x coordinates
     * @param points_y Device array of y coordinates
     * @param count Number of points
     * @param q_x Point to add (x coordinate)
     * @param q_y Point to add (y coordinate)
     * @return True if successful
     */
    static bool batch_point_add_gpu(
        uint32_t* points_x,
        uint32_t* points_y,
        size_t count,
        const uint32_t* q_x,
        const uint32_t* q_y
    );
    
    /**
     * Batch modular inverse (GPU)
     * 
     * CUDA kernel for parallel modular inversion
     * 
     * @param values Device array of input values
     * @param inv Device array of output inverses
     * @param count Number of values
     * @param modulus Modulus p
     * @return True if successful
     */
    static bool batch_mod_inverse_gpu(
        const uint32_t* values,
        uint32_t* inv,
        size_t count,
        const uint32_t* modulus
    );
    
    /**
     * Batch scalar multiplication (GPU)
     * 
     * CUDA kernel for parallel scalar multiplication
     * 
     * @param scalars Device array of scalars
     * @param points_x Device array of output x coordinates
     * @param points_y Device array of output y coordinates
     * @param count Number of scalars
     * @return True if successful
     */
    static bool batch_scalar_mul_gpu(
        const uint32_t* scalars,
        uint32_t* points_x,
        uint32_t* points_y,
        size_t count
    );
#endif // __CUDACC__
};

} // namespace utils
} // namespace puzzle71

