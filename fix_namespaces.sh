#!/bin/bash

# Fix namespace from keycuda to puzzle71 in all performance headers
echo "Fixing namespaces in performance headers..."

cd /root/keycuda

# List of files to fix
files=(
    "src/ComputeCore/gpu/performance/adaptive_parallelism_scaling.h"
    "src/ComputeCore/gpu/performance/asynchronous_stream_manager.h"
    "src/ComputeCore/gpu/performance/bandwidth_validator.h"
    "src/ComputeCore/gpu/performance/configuration_logger.h"
    "src/ComputeCore/gpu/performance/double_buffer_manager.h"
    "src/ComputeCore/gpu/performance/fused_initialization_kernel.h"
    "src/ComputeCore/gpu/performance/memory_bandwidth_profiler.h"
    "src/ComputeCore/gpu/performance/memory_coalescing_optimizer.h"
    "src/ComputeCore/gpu/performance/memory_optimizer.h"
    "src/ComputeCore/gpu/performance/memory_pool_manager.h"
    "src/ComputeCore/gpu/performance/memory_prefetch_manager.h"
    "src/ComputeCore/gpu/performance/memory_transfer_batcher.h"
)

for file in "${files[@]}"; do
    if [ -f "$file" ]; then
        echo "Processing $file..."
        # Replace keycuda namespace with puzzle71
        sed -i 's/namespace keycuda {/namespace puzzle71 {/g' "$file"
        sed -i 's/} \/\/ namespace keycuda/} \/\/ namespace puzzle71/g' "$file"
        sed -i 's/keycuda::/puzzle71::/g' "$file"
    else
        echo "Warning: $file not found"
    fi
done

echo "Namespace fixes completed!"
