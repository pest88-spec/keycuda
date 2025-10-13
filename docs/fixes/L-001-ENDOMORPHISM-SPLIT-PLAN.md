# L-001: Endomorphism Split Validation Test - Implementation Plan

**Date**: 2025-10-13  
**Task**: Implement CUDA vs CPU scalar split validation using secp256k1 reference  
**Status**: 📋 **PLANNED** (Ready for implementation)  
**Priority**: Low (L-001)

---

## 📋 Task Overview

### Current State
**File**: `tests/validation/test_endomorphism_split.cpp`  
**Lines**: 7 (DISABLED test with TODO comment)

```cpp
#include <gtest/gtest.h>

TEST(EndomorphismSplitTest, DISABLED_GpuMatchesCpuScalarSplit) {
    // TODO: Implement CUDA vs CPU scalar split validation using secp256k1 reference.
    FAIL() << "Not implemented";
}
```

### Goal
Implement comprehensive validation test that verifies GPU endomorphism scalar split matches CPU reference implementation from secp256k1-zkp.

---

## 🔍 Technical Analysis

### Available Implementations

#### 1. CPU Reference: secp256k1-zkp
**Location**: `third_party/secp256k1-zkp/src/scalar_impl.h:138`

```c
static void secp256k1_scalar_split_lambda(
    secp256k1_scalar * SECP256K1_RESTRICT r1,
    secp256k1_scalar * SECP256K1_RESTRICT r2,
    const secp256k1_scalar * SECP256K1_RESTRICT k
) {
    // Splits k into r1 and r2 such that:
    // - r1 + lambda * r2 == k (mod n)
    // - either r1 < 2^128 or -r1 mod n < 2^128
    // - either r2 < 2^128 or -r2 mod n < 2^128
}
```

#### 2. VanitySearch Implementation
**Location**: `external/VanitySearch/SECP256k1.cpp`

```cpp
void Secp256K1::SplitScalar(Int *k, Int *k1, Int *k2) {
    // GLV decomposition algorithm
    // k = k1 + k2 * lambda
    // where |k1|, |k2| ≈ sqrt(n)
}
```

#### 3. GLVEndomorphismAdapter
**Location**: `src/core/ECC/glv_endomorphism_adapter.{h,cpp}`

Wraps VanitySearch implementation for project use.

---

## 🎯 Implementation Plan

### Phase 1: Setup Test Infrastructure (30 min)

1. **Include Required Headers**
   ```cpp
   #include <gtest/gtest.h>
   #include "core/ECC/glv_endomorphism_adapter.h"
   #include "secp256k1.h"
   #include "secp256k1_preallocated.h"
   #include <random>
   #include <vector>
   ```

2. **Create Test Fixture**
   ```cpp
   class EndomorphismSplitTest : public ::testing::Test {
   protected:
       void SetUp() override {
           // Initialize secp256k1 context
           // Initialize GLVEndomorphismAdapter
       }
       
       void TearDown() override {
           // Cleanup
       }
       
       secp256k1_context* ctx_;
       std::unique_ptr<GLVEndomorphismAdapter> adapter_;
   };
   ```

### Phase 2: Implement CPU Reference Split (30 min)

```cpp
struct ScalarSplit {
    std::array<uint8_t, 32> k1;
    std::array<uint8_t, 32> k2;
};

ScalarSplit SplitScalarCPU(const std::array<uint8_t, 32>& k) {
    secp256k1_scalar scalar, r1, r2;
    
    // Convert bytes to secp256k1_scalar
    secp256k1_scalar_set_b32(&scalar, k.data(), nullptr);
    
    // Perform split using secp256k1-zkp reference
    secp256k1_scalar_split_lambda(&r1, &r2, &scalar);
    
    // Convert back to bytes
    ScalarSplit result;
    secp256k1_scalar_get_b32(result.k1.data(), &r1);
    secp256k1_scalar_get_b32(result.k2.data(), &r2);
    
    return result;
}
```

### Phase 3: Implement GPU Split Wrapper (30 min)

```cpp
ScalarSplit SplitScalarGPU(const std::array<uint8_t, 32>& k) {
    // Use GLVEndomorphismAdapter or VanitySearch
    // This wraps the existing GPU implementation
    
    // For now, use VanitySearch's CPU implementation
    // (GPU implementation will be added in future CUDA kernel work)
    
    Int scalar, k1, k2;
    // Convert bytes to Int
    // Call SplitScalar
    // Convert back to bytes
    
    return result;
}
```

### Phase 4: Implement Validation Tests (1 hour)

#### Test 1: Known Test Vectors
```cpp
TEST_F(EndomorphismSplitTest, KnownTestVectors) {
    // Test with known scalar values
    std::vector<std::array<uint8_t, 32>> test_vectors = {
        // Vector 1: k = 1
        {0, 0, ..., 0, 1},
        // Vector 2: k = n-1 (curve order - 1)
        {...},
        // Vector 3: k = 2^128
        {...},
    };
    
    for (const auto& k : test_vectors) {
        auto cpu_split = SplitScalarCPU(k);
        auto gpu_split = SplitScalarGPU(k);
        
        EXPECT_EQ(cpu_split.k1, gpu_split.k1);
        EXPECT_EQ(cpu_split.k2, gpu_split.k2);
    }
}
```

#### Test 2: Random Scalar Validation
```cpp
TEST_F(EndomorphismSplitTest, RandomScalarValidation) {
    constexpr size_t kTestCount = 1000;
    std::mt19937_64 rng(12345);  // Fixed seed for reproducibility
    
    for (size_t i = 0; i < kTestCount; ++i) {
        // Generate random scalar
        std::array<uint8_t, 32> k;
        for (auto& byte : k) {
            byte = static_cast<uint8_t>(rng() & 0xFF);
        }
        
        auto cpu_split = SplitScalarCPU(k);
        auto gpu_split = SplitScalarGPU(k);
        
        EXPECT_EQ(cpu_split.k1, gpu_split.k1) 
            << "k1 mismatch for scalar " << i;
        EXPECT_EQ(cpu_split.k2, gpu_split.k2)
            << "k2 mismatch for scalar " << i;
    }
}
```

#### Test 3: Verify Split Property
```cpp
TEST_F(EndomorphismSplitTest, VerifySplitProperty) {
    // Verify that k = k1 + k2 * lambda (mod n)
    
    std::array<uint8_t, 32> k = {...};  // Test scalar
    auto split = SplitScalarCPU(k);
    
    // Compute k1 + k2 * lambda using secp256k1
    secp256k1_scalar k_scalar, k1_scalar, k2_scalar, lambda_scalar, result;
    
    secp256k1_scalar_set_b32(&k_scalar, k.data(), nullptr);
    secp256k1_scalar_set_b32(&k1_scalar, split.k1.data(), nullptr);
    secp256k1_scalar_set_b32(&k2_scalar, split.k2.data(), nullptr);
    
    // lambda = 0x5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72
    // (secp256k1 endomorphism constant)
    uint8_t lambda_bytes[32] = {...};
    secp256k1_scalar_set_b32(&lambda_scalar, lambda_bytes, nullptr);
    
    // result = k1 + k2 * lambda
    secp256k1_scalar_mul(&result, &k2_scalar, &lambda_scalar);
    secp256k1_scalar_add(&result, &result, &k1_scalar);
    
    // Verify result == k
    EXPECT_TRUE(secp256k1_scalar_eq(&result, &k_scalar))
        << "Split property verification failed: k != k1 + k2 * lambda";
}
```

---

## 🔒 Iron Cage Protocol v5.0 Compliance

### NO-CRYPTO-REINVENTION ✅
- Uses secp256k1-zkp as CPU reference (authoritative)
- Wraps VanitySearch implementation (proven)
- No custom cryptography

### TEST-FIRST-CUDA ✅
- Tests written before GPU implementation
- Validates against CPU reference
- Comprehensive test coverage

### DETERMINISM-FIRST ✅
- Fixed seed for random tests (reproducible)
- Known test vectors
- Deterministic validation

---

## 📊 Expected Results

### Test Coverage
- ✅ Known test vectors (edge cases)
- ✅ Random scalar validation (1000+ cases)
- ✅ Split property verification (mathematical correctness)

### Pass Criteria
- 100% CPU-GPU parity
- All test vectors pass
- Split property verified for all cases

---

## 🚀 Next Steps

1. **Implement test infrastructure** (30 min)
2. **Implement CPU reference wrapper** (30 min)
3. **Implement GPU wrapper** (30 min)
4. **Write and run tests** (1 hour)
5. **Enable test** (remove DISABLED prefix)
6. **Verify all tests pass**

**Total Estimated Time**: 3 hours

---

## 📝 Notes

- GPU implementation may use VanitySearch's CPU version initially
- Full CUDA kernel implementation deferred to future work
- Focus on validation framework and CPU reference correctness
- Ensure all tests are deterministic and reproducible

---

**Status**: 📋 **READY FOR IMPLEMENTATION**  
**Next Session**: Implement according to this plan  
**Documentation**: Complete


