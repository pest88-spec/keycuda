#!/bin/bash

cd /root/keycuda

# Step 1: Remove conflicting struct definitions from memory_coalescing_optimizer.h
echo "Fixing memory_coalescing_optimizer.h..."
sed -i '/^enum class AccessPatternType {/,/^};/d' src/ComputeCore/gpu/performance/memory_coalescing_optimizer.h
sed -i '/^struct AccessPattern {/,/^};/d' src/ComputeCore/gpu/performance/memory_coalescing_optimizer.h

# Step 2: Remove conflicting definitions from memory_prefetch_manager.h
echo "Fixing memory_prefetch_manager.h..."
sed -i '/^enum class PrefetchStrategy {/,/^};/d' src/ComputeCore/gpu/performance/memory_prefetch_manager.h
sed -i '/^enum class AccessPatternType {/,/^};/d' src/ComputeCore/gpu/performance/memory_prefetch_manager.h
sed -i '/^struct AccessPattern {/,/^};/d' src/ComputeCore/gpu/performance/memory_prefetch_manager.h

# Step 3: Remove nested AccessPattern from memory_pool_manager.h (if it's nested inside a class, we'll handle separately)
echo "Checking memory_pool_manager.h..."
# This one might be a nested struct, so let's leave it for manual inspection

echo "Struct redefinition fix script completed!"
echo "NOTE: You will need to add forward declarations or includes to reference the common definitions from memory_optimizer.h"
