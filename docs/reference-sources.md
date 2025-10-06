# Reference Sources Documentation

This document tracks all third-party code extracted and integrated into the Puzzle71Solver project.

## Overview

As part of the **Source Code Fusion Architecture**, we extract proven cryptographic implementations from established projects rather than reimplementing from scratch. This approach follows the **NO-CRYPTO-REINVENTION** principle defined in `puzzle71_constraints.md`.

## Extracted Sources

### 1. BitCrack

**Project**: BitCrack - Bitcoin private key cracker for CUDA devices
**Author**: brichard19 (Ben Richard)
**Repository**: https://github.com/brichard19/BitCrack
**License**: MIT
**Commit**: de3c15bcbe5d36e31d7ac969784773af1cd81a84
**Extraction Date**: 2025-10-06
**Extracted By**: Puzzle71Solver Team

**License File**: [docs/licenses/BitCrack-LICENSE.MIT](licenses/BitCrack-LICENSE.MIT)

**Extracted Components**:

| Module | Source Path | Destination Path | Purpose |
|--------|-------------|------------------|---------|
| **cudaMath** | `third_party/BitCrack/cudaMath/` | `src/extracted/bitcrack/cudaMath/` | CUDA secp256k1 ECC operations, SHA256, RIPEMD160 |
| **CudaKeySearchDevice** | `third_party/BitCrack/CudaKeySearchDevice/` | `src/extracted/bitcrack/CudaKeySearchDevice/` | GPU key search device kernels and utilities |
| **AddressUtil** | `third_party/BitCrack/AddressUtil/` | `src/extracted/bitcrack/AddressUtil/` | Bitcoin address generation and Base58 encoding |
| **CryptoUtil** | `third_party/BitCrack/CryptoUtil/` | `src/extracted/bitcrack/CryptoUtil/` | Cryptographic utilities (SHA256, RIPEMD160, RNG) |
| **cudaUtil** | `third_party/BitCrack/cudaUtil/` | `src/extracted/bitcrack/cudaUtil/` | CUDA utility functions and error handling |
| **KeyFinderLib** | `third_party/BitCrack/KeyFinderLib/` | `src/extracted/bitcrack/KeyFinderLib/` | Key search type definitions and interfaces |
| **Logger** | `third_party/BitCrack/Logger/` | `src/extracted/bitcrack/Logger/` | Logging framework |
| **secp256k1lib** | `third_party/BitCrack/secp256k1lib/` | `src/extracted/bitcrack/secp256k1lib/` | secp256k1 elliptic curve host library |
| **util** | `third_party/BitCrack/util/` | `src/extracted/bitcrack/util/` | General utility functions |

**Extracted Files** (40 total):

```
src/extracted/bitcrack/
├── cudaMath/
│   ├── ptx.cuh                  # PTX inline assembly primitives
│   ├── ripemd160.cuh            # RIPEMD160 hash implementation
│   ├── secp256k1.cuh            # secp256k1 ECC point operations
│   └── sha256.cuh               # SHA256 hash implementation
├── CudaKeySearchDevice/
│   ├── CudaAtomicList.{cu,cuh,h}     # Atomic list for device-host communication
│   ├── cudabridge.{cu,h}             # Bridge between host and device code
│   ├── CudaDeviceKeys.{cu,cuh,h}     # Device-side key management
│   ├── CudaHashLookup.{cu,cuh,h}     # Hash comparison and lookup
│   └── CudaKeySearchDevice.{cu,cpp,h} # Main key search device implementation
├── AddressUtil/
│   ├── AddressUtil.h            # Address generation interface
│   ├── Base58.cpp               # Base58 encoding/decoding
│   └── hash.cpp                 # Address hashing utilities
├── CryptoUtil/
│   ├── checksum.cpp             # Checksum calculation
│   ├── CryptoUtil.h             # Crypto utilities interface
│   ├── hash.cpp                 # Generic hash utilities
│   ├── ripemd160.cpp            # RIPEMD160 host implementation
│   ├── Rng.cpp                  # Random number generator
│   └── sha256.cpp               # SHA256 host implementation
├── cudaUtil/
│   ├── cudaUtil.cpp             # CUDA utility functions
│   └── cudaUtil.h               # CUDA utility headers
├── KeyFinderLib/
│   ├── KeyFinder.{cpp,h}        # Key finder main class
│   ├── KeyFinderShared.h        # Shared constants and types
│   ├── KeySearchDevice.h        # Device interface
│   └── KeySearchTypes.h         # Type definitions for key search
├── Logger/
│   ├── Logger.cpp               # Logging implementation
│   └── Logger.h                 # Logging interface
├── secp256k1lib/
│   ├── secp256k1.cpp            # secp256k1 host implementation
│   └── secp256k1.h              # secp256k1 interface
└── util/
    ├── util.cpp                 # General utilities
    └── util.h                   # Utility headers
```

**Key Functions Extracted**:

- **ECC Operations**: `_pointAddition`, `_pointDouble`, `_scalarMultiplication` (cudaMath/secp256k1.cuh)
- **SHA256**: `sha256PublicKey`, `sha256PublicKeyCompressed` (cudaMath/sha256.cuh)
- **RIPEMD160**: `ripemd160sha256NoFinal` (cudaMath/ripemd160.cuh)
- **Base58**: `encodeBase58`, `decodeBase58` (AddressUtil/Base58.cpp)
- **Address Generation**: `fromPublicKey`, `verifyAddress` (AddressUtil/AddressUtil.h)

**Modifications**:
- All files retain original logic unchanged
- Added @origin attribution headers as per `puzzle71_constraints.md` Section 6.2
- Files relocated from `third_party/BitCrack/` to `src/extracted/bitcrack/` for direct integration
- No algorithmic modifications (strict compliance with NO-CRYPTO-REINVENTION rule)

## Retained Dependencies

### bitcoin-core/secp256k1

**Project**: Bitcoin Core secp256k1 library
**Repository**: https://github.com/bitcoin-core/secp256k1
**License**: MIT
**Location**: `third_party/bitcoin-core-secp256k1/` (submodule)
**Purpose**: CPU reference implementation for validation (linked via CMake)
**Status**: ✅ Retained as Git submodule for CPU validation

## Removed Dependencies

The following dependencies were evaluated but found to be unused in the current codebase:

- **CudaBrainSecp** (`third_party/CudaBrainSecp/`) - Removed (no actual usage found)
- **VanitySearch** (`third_party/VanitySearch/`) - Removed (only TODO comments, no implementation)

## Attribution Compliance

All extracted files comply with `puzzle71_constraints.md` Section 6 requirements:

✅ **P0级强制要求** (Mandatory):
1. License compatibility check (MIT → MIT ✅)
2. NO-CRYPTO-REINVENTION check (using proven implementations ✅)
3. Attribution recording (all files have @origin headers ✅)

✅ **Attribution Headers** (Section 6.2):
- `@origin`: Repository URL
- `@origin_path`: Original file path in source repository
- `@origin_commit`: Exact commit hash (de3c15b)
- `@origin_license`: Original license (MIT)
- `@extracted_date`: Extraction date (2025-10-06)
- `@extracted_by`: Team identifier
- `@modifications`: Description of changes
- `@spdx_license_identifier`: SPDX license ID

## License Compatibility Matrix

| Source Project | License | Puzzle71Solver License | Compatible? |
|----------------|---------|------------------------|-------------|
| BitCrack | MIT | MIT | ✅ Yes |
| bitcoin-core/secp256k1 | MIT | MIT | ✅ Yes |

## Verification

**Extraction Audit Trail**:
```bash
# Verify all files have @origin attribution
grep -r "@origin" src/extracted/bitcrack/ | wc -l
# Expected: 40 files

# Verify license file exists
ls -la docs/licenses/BitCrack-LICENSE.MIT
# Expected: MIT license file

# Verify commit hash
cd third_party/BitCrack && git log -1 --format="%H"
# Expected: de3c15bcbe5d36e31d7ac969784773af1cd81a84
```

## Update History

| Date | Version | Changes | Commit |
|------|---------|---------|--------|
| 2025-10-06 | 0.2.0 | Initial extraction of BitCrack code (40 files) | TBD |

## References

- **Project Constraints**: [puzzle71_constraints.md](../puzzle71_constraints.md) Section 6
- **License Files**: [docs/licenses/](licenses/)
- **BitCrack Repository**: https://github.com/brichard19/BitCrack
- **Rollback Plan**: [ROLLBACK-PLAN-A.md](../ROLLBACK-PLAN-A.md)

---

**Document Version**: 1.0
**Last Updated**: 2025-10-06
**Maintained By**: Puzzle71Solver Team
**Review Frequency**: After each code extraction
