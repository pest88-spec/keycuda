/**
 * @file optimized_scanner.cuh
 * @brief Header for production scan pipeline with all optimizations
 */

#pragma once

#include <cuda_runtime.h>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

// Forward declarations
struct uint256_t;

namespace keyhunt {
namespace scan {

struct ScanStats;
struct ScanRange;
struct Checkpoint;
struct MatchResult;
class OptimizedScanner;

// Public API
std::unique_ptr<OptimizedScanner> createOptimizedScanner(int deviceId = 0);

} // namespace scan
} // namespace keyhunt