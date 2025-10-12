/**
 * @file adaptive_batch.cuh
 * @brief Header for adaptive batch sizing
 */

#pragma once

#include <cuda_runtime.h>
#include <cstddef>

namespace keyhunt {
namespace gpu {

struct AdaptiveBatchParams;
class AdaptiveBatchManager;

// Public API
void initializeAdaptiveBatch(const AdaptiveBatchParams& params = AdaptiveBatchParams());
size_t getNextAdaptiveBatchSize();
void reportBatchPerformance(size_t batchSize, float kernelTime, float throughput);
void setTargetThroughput(float targetThroughput, float minAcceptable = 0.0f);

} // namespace gpu
} // namespace keyhunt