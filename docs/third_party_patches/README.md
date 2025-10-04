# Third-Party Patches

This directory contains patches applied to third-party dependencies.

## BitCrack Local Changes

**File**: `bitcrack_local_changes.patch`
**Base Commit**: `50853c4d7e54e290202aa5432c46d9fb2529f53f`
**Base Message**: "Fix: Reset _step in init() to prevent base point table overflow"

### Changes Summary

Added `CudaDeviceKeys::updatePrivateKeys()` method to enable runtime private key updates without full reinitialization.

**Modified Files**:
- `CudaKeySearchDevice/CudaDeviceKeys.cu` - Implementation of updatePrivateKeys()
- `CudaKeySearchDevice/CudaDeviceKeys.h` - Method declaration

**Functionality**:
- Allows updating GPU device memory with new private key set
- Validates input size matches expected batch dimensions
- Resets chain state and step counter to maintain consistency
- Provides proper error handling for invalid inputs

**Rationale**:
This enhancement is required for PuzzleKeyhunt's deterministic replay system, which needs to reinitialize GPU state from checkpoint manifests without destroying and recreating device contexts.

### Applying the Patch

```bash
cd third_party/BitCrack
git apply ../../docs/third_party_patches/bitcrack_local_changes.patch
```

### Upstream Status

This patch has not been submitted upstream to BitCrack as it is specific to PuzzleKeyhunt's checkpoint/replay architecture.
