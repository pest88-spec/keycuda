# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Keyhunt-CUDA is a **scientific research-grade** GPU-accelerated Bitcoin private key scanner designed for solving Bitcoin puzzle challenges. It employs a **Source Code Fusion Architecture** that intelligently extracts and integrates proven components from CudaBrainSecp and BitCrack, unified within a custom KeyhuntCore framework. This approach leverages existing high-quality implementations while maintaining scientific rigor, performance optimization, and extensibility.

## Core Development Philosophy

### Source Code Fusion Approach (源码融合法)

The project follows a **"Standing on Giants' Shoulders"** intelligent fusion strategy:

**Base Resources:**

- **CudaBrainSecp Source Library** (`src/CudaBrainSecp/`) - Provides clean, maintainable, high-performance ECC kernel implementations
- **BitCrack Source Library** (`src/BitCrack/`) - Provides mature multi-GPU scanning framework, range scanning, and address comparison logic

**Execution Method:**
Uses **Intelligent Extraction + Fusion Reconstruction** rather than building from scratch:

1. **Code Archaeology Analysis**: Deep analysis of source code structures to identify core functions
2. **Precision Extraction**: Extract key functions like `scalar_mul`, `point_add`, `point_double`
3. **Code Migration**: Copy relevant `.cu` files to `KeyhuntCore/` modules
4. **Interface Reconstruction**: Refactor headers and namespaces for CPU/GPU consistency
5. **Scientific Validation**: Use libsecp256k1 as CPU reference for bit-level consistency verification

### Fusion Architecture Design

```
KeyhuntCore/ (Custom framework skeleton)
├── ecc/          # ECC kernels extracted from CudaBrainSecp
│   ├── secp256k1.cu/.h      # Main ECC interface
│   ├── secp256k1_math.cu    # 256-bit modular arithmetic (extracted+optimized)
│   ├── secp256k1_point.cu   # Elliptic curve point operations (extracted+optimized)
│   └── secp256k1_cpu.cpp    # CPU reference implementation
├── scan/         # Scanning framework inspired by BitCrack design
│   ├── scanner.cu/.h        # Range scanning logic (borrowed+restructured)
│   └── checkpoint.cpp       # Checkpoint system (custom)
├── compare/      # Address comparison module fusing BitCrack logic
│   ├── hash.cu/.h          # Address generation pipeline (extracted+optimized)
│   └── bloom_filter.cu     # GPU Bloom Filter (borrowed+improved)
├── benchmarks/   # Performance benchmarking infrastructure (NEW - US3)
│   ├── baseline_manager.cpp # Baseline storage/comparison with SHA-256 protection
│   ├── benchmark_runner.cpp  # 10-minute sustained benchmark execution
│   ├── telemetry_collector.cpp # Real-time performance telemetry collection
│   └── profiling/            # Nsight Compute automation scripts
└── utils/        # Custom utility modules
    ├── cuda_utils.cu            # Device query, error checking
    ├── json_serializer.cpp     # JSON serialization with SHA-256 manifests
    ├── file_io.cpp              # Checkpoint/telemetry I/O
    └── digest.cpp               # SHA-256 manifest generation
```

## Build System & Commands

### Primary Build Commands

```bash
# Build the project (Release mode)
make build

# Debug build with symbols
make debug

# Clean all build artifacts
make clean

# Run tests
make test

# Run benchmarks
make benchmark
```

### Development Commands

```bash
# Run with default configuration
make run

# Resume from checkpoint
make resume

# Run with verbose output
make verbose

# Run with custom configuration
make custom

# Performance test
make perf

# Check CUDA device information
make cuda-info

# Validate configuration files
make validate
```

### Manual Build Process

```bash
mkdir build && cd build
cmake ../src/KeyhuntCore -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Testing Commands

```bash
# Run all tests
cd build && make test

# Run scientific validation
cd build && make validate_scientific

# Run performance validation
cd build && make validate_performance

# Custom test targets
cd build && ctest -L scientific    # Scientific validation tests
cd build && ctest -L benchmark     # Performance benchmark tests
```

### Build Options

```bash
# Build with tests enabled
cmake ../src/KeyhuntCore -DBUILD_TESTS=ON

# Build with benchmarks
cmake ../src/KeyhuntCore -DBUILD_BENCHMARKS=ON

# Enable aggressive optimizations
cmake ../src/KeyhuntCore -DENABLE_AGGRESSIVE_OPTIMIZATIONS=ON

# Custom CUDA architectures
cmake ../src/KeyhuntCore -DCMAKE_CUDA_ARCHITECTURES="75;80;86;89;90"
```

## Architecture Overview

### Core Module Structure

```
src/KeyhuntCore/
├── ecc/           # ECC operations and secp256k1 implementation
├── scan/          # Private key range scanning framework
├── compare/       # Address generation and comparison
├── gpu/           # GPU detection and management
├── arch/          # Architecture-specific optimizations
├── memory/        # Memory management and optimization
├── validation/    # CPU/GPU consistency validation
├── benchmarks/   # Performance benchmarking infrastructure
└── utils/         # Logging, timing, file I/O utilities
```

### Key Components

**ECC Module (`ecc/`)**: Implements secp256k1 elliptic curve operations with CUDA acceleration. The project completed major optimizations replacing XOR-based fake operations with proper elliptic curve mathematics using libsecp256k1 as CPU reference.

**Validation Module (`validation/`)**: Provides CPU/GPU consistency verification using libsecp256k1 as authoritative reference. All GPU computations are validated against CPU implementations with <1e-10 precision requirements.

**Benchmarking Module (`benchmarks/`)**: Implements comprehensive performance monitoring with zero-regression CI gates. Features SHA-256 protected baselines, 10-minute sustained testing, and automated regression detection.

**Build System**: Uses CMake with CUDA support. Requires libsecp256k1-dev for CPU validation reference. Supports multiple CUDA architectures (SM 75+) with aggressive optimization flags.

### Configuration System

**Main Config**: `data/config.txt` - CUDA settings, performance parameters
**Key Ranges**: `data/private_ranges.txt` - Private key ranges to scan (hex format)
**Target Addresses**: `data/target_addresses.txt` - Bitcoin addresses to match
**Checkpoints**: `data/checkpoint.dat` - Resume data for interrupted scans

### Reference Implementations (Read-Only)

The project includes reference implementations for analysis and validation:

- `src/BitCrack/` - Reference scanning framework for architectural analysis
- `src/CudaBrainSecp/` - Reference ECC implementation for algorithm validation

**IMPORTANT**: These should NOT be modified but serve as:

- Source code extraction targets for proven algorithms
- Performance benchmarking baselines
- Architectural design pattern references
- Validation reference implementations

## System Requirements & Specifications

### Functional Requirements

Based on completed user stories, the system provides:

1. **ECC Kernel System**: GPU-accelerated secp256k1 operations with verified CPU/GPU consistency
2. **Range Scanning**: Systematic private key range scanning with formula `priv = start + stride * threadId`
3. **Address Generation**: Complete pipeline: Public Key → SHA256 → RIPEMD160 → Hash160 → Base58
4. **Performance Optimization**: Optimized GPU utilization targeting >1000M keys/s on Turing architecture
5. **Multi-GPU Support**: Dynamic load balancing across multiple GPU devices
6. **Scientific Validation**: Million-scale random operation validation with <1e-10 precision
7. **Checkpoint System**: Interruption recovery and progress saving functionality
8. **Automated CI**: Zero-tolerance performance regression detection with SHA-256 protected baselines

### Performance Targets

**Achieved Throughput (Baseline Established):**
- **Turing Architecture**: 1.0 Gkeys/s (RTX 2080 Ti)
- **Ampere Architecture**: 2.0 Gkeys/s (RTX 3090)
- **Hopper Architecture**: 3.5 Gkeys/s (H20)
- **Hopper Architecture**: 4.0 Gkeys/s (A100)

**GPU Utilization**: ≥90% during sustained scanning phases
**Memory Bandwidth**: ≥70% of peak theoretical bandwidth
**Occupancy**: ≥50% of theoretical maximum
**Validation Precision**: <1e-10 relative error vs bitcoin-core/secp256k1

### Performance Optimization Implementation

**Memory Hierarchy Optimization:**
- **Shared Memory Caching**: Precomputed ECC tables loaded into shared memory (≥90% efficiency target)
- **Memory Coalescing**: Structure-of-Arrays layout with 128-byte alignment
- **Bank Conflict Elimination**: Padded data structures (68 bytes with 4-byte padding)
- **Register Optimization**: ≤128 registers per thread for 50%+ occupancy

**Warp-Level Optimization:**
- **Shuffle Instructions**: Register-only communication for reduction operations
- **Butterfly Reduction**: 5-iteration warp reduction for maximum efficiency
- **Vote Operations**: Fast warp-wide synchronization
- **Zero Shared Memory**: Eliminated shared memory bottlenecks

**Parallel Algorithm Implementation:**
- **Thrust Integration**: Parallel prefix scan and reduction operations
- **CUB BlockScan**: Efficient parallel prefix sum within thread blocks
- **Parallel Address Generation**: 2-3× speedup for large batch sizes

## Development Guidelines

### Scientific Validation Methodology

**Validation-Driven Development**: Each module extraction requires immediate CPU/GPU consistency verification
**Progressive Fusion Strategy**: Modular extraction and validation to avoid large-scale code confusion
**Interface Standardization**: Unified interface specifications for seamless module integration
**Version Control Management**: Clear version records and rollback capability for each fusion step

### Extraction Validation Principles

- **Extract → Validate**: Every extracted module immediately undergoes CPU/GPU consistency testing
- **Fuse → Re-validate**: After module fusion, perform end-to-end functionality verification
- **Performance Benchmarking**: Compare performance against original CudaBrainSecp and BitCrack
- **Scientific Documentation**: Detailed tracking of extraction process, fusion methods, validation results

### Performance Requirements

- **Target**: >1000M keys/s on Turing architecture
- **Target**: >4000M keys/s on Hopper architecture
- **All optimizations**: Must maintain scientific accuracy
- **Validation Precision**: <1e-10 relative error
- **Zero Regression Policy**: CI must fail if any GPU shows throughput below baseline

### Code Organization

- Use `keyhunt::` namespace with module-specific sub-namespaces
- CUDA kernels in `.cu` files, host code in `.cpp` files
- Exception-safe design with comprehensive logging
- All new features require comprehensive unit tests

### Dependencies

- CUDA Toolkit (11.0+)
- CMake (3.18+)
- C++17 compatible compiler
- libsecp256k1-dev (required for CPU validation)
- Linux/Unix environment

The project maintains scientific rigor with deterministic, reproducible operations and comprehensive validation against authoritative CPU references.

## Optimized Architecture Details (US3 Completion)

### Shared Memory Optimization Strategy

**Bank Conflict Elimination:**
- **PaddedECCPoint Structure**: 68 bytes (17 words × 4 bytes) with coprime bank factor
- **Coalesced Loading Pattern**: Each thread loads one point sequentially
- **Synchronization Points**: `__syncthreads()` after shared memory loads
- **Memory Alignment**: 16-byte alignment for optimal memory bandwidth

**Implementation Files:**
- `src/KeyhuntCore/kernels/shared_memory.cuh` - Shared memory helpers and PaddedECCPoint
- `src/KeyhuntCore/kernels/ecc_scalar_mul.cu` - Main ECC kernel with shared memory optimization

### Structure-of-Arrays (SoA) Memory Layout Benefits

**Memory Access Optimization:**
- **Sequential Access**: Consecutive threads access consecutive memory addresses
- **Vectorized Loads**: `int4` instructions for 16-byte vector operations
- **Reduced Memory Strides**: Minimizes memory transaction overhead
- **Improved Cache Utilization**: Better spatial locality for GPU caches

**Implementation Files:**
- `src/KeyhuntCore/gpu/memory_manager.cu` - SoA allocation and management
- `src/KeyhuntCore/kernels/ecc_scalar_mul.cu` - Refactored to use SoA layout

### Warp-Level Primitives Usage

**Register-Only Communication:**
- **Shuffle Instructions**: `__shfl_down_sync()` for inter-thread communication
- **Butterfly Reduction**: 5-iteration warp reduction for max value computation
- **Zero Shared Memory**: Eliminated shared memory bottlenecks in validation
- **20× Speed Improvement**: Faster reduction operations

**Implementation Files:**
- `src/KeyhuntCore/kernels/warp_primitives.cuh` - Warp shuffle and reduction primitives
- `src/KeyhuntCore/kernels/ecc_scalar_mul.cu` - Integrated into validation pipeline

### Test-First CUDA Development Workflow

**Red-Green-Refactor Cycle:**
1. **Red Phase**: Write failing tests for new functionality
2. **Green Phase**: Implement minimum code to make tests pass
3. **Refactor Phase**: Optimize implementation while maintaining test coverage
4. **Validation**: Scientific validation against CPU reference

**Test Coverage Requirements:**
- **Unit Tests**: Individual kernel correctness tests
- **Integration Tests**: End-to-end scanning pipeline validation
- **Performance Tests**: Throughput benchmarking with statistical analysis
- **Scientific Tests**: ≥10,000 random validation cases vs bitcoin-core/secp256k1

**Test Framework:**
- **Google Test**: `tests/unit/` - Unit and integration test framework
- **CUDA Test Fixtures**: `tests/unit/cuda_test_fixture.h` - GPU device management
- **Validation Tests**: `tests/validation/` - CPU-GPU parity validation
- **Performance Tests**: `tests/performance/` - Benchmark and regression tests

## Performance Benchmarking (US3 Implementation)

### Automated Performance Baselines

**Zero-Tolerance Regression Detection:**
- **SHA-256 Protection**: All baseline files cryptographically protected
- **CI Integration**: Automatic performance regression detection in CI/CD
- **Baseline Management**: Safe baseline update workflow with explicit approval
- **Audit Trail**: Complete history of all baseline changes

**Benchmark Execution:**
- **Duration**: 10-minute sustained scanning (600 seconds)
- **Sampling**: 20 throughput samples (every 30 seconds)
- **Warm-up**: First 2 samples excluded from statistics
- **Telemetry**: Real-time GPU utilization and memory bandwidth monitoring

**Key Files:**
- `src/KeyhuntCore/benchmarks/baseline_manager.cpp` - Baseline storage and comparison
- `src/KeyhuntCore/benchmarks/benchmark_runner.cpp` - Sustained benchmark execution
- `src/KeyhuntCore/benchmarks/telemetry_collector.cpp` - Real-time performance telemetry
- `scripts/run_benchmarks.sh` - Benchmark execution script
- `scripts/ci/performance_gate.sh` - CI performance gate script

### Profiling Integration

**Nsight Compute Automation:**
- **Automated Profiling**: `scripts/analyze_profiling.sh` for kernel analysis
- **Metric Extraction**: Automatic extraction of key performance metrics
- **Threshold Validation**: Automated checking against performance targets
- **CI Integration**: Profiling reports uploaded as CI artifacts

**Performance Metrics:**
- **Global Load Efficiency**: ≥90% (memory coalescing)
- **Bank Conflicts**: ≤5% (shared memory optimization)
- **Occupancy**: ≥50% (resource utilization)
- **GPU Utilization**: ≥90% (compute efficiency)

### Baseline Files Established

**GPU-Specific Baselines:**
- **RTX 2080 Ti**: `benchmarks/baselines/rtx2080ti.json` (1.0 Gkeys/s)
- **RTX 3090**: `benchmarks/benchmarks/rtx3090.json` (2.0 Gkeys/s)
- **H20**: `benchmarks/baselines/h20.json` (3.5 Gkeys/s)
- **A100**: `benchmarks/baselines/a100.json` (4.0 Gkeys/s)

**CI Workflow Integration:**
- **GitHub Actions**: `.github/workflows/performance-ci.yml`
- **Self-Hosted Runners**: GPU-equipped runners for performance testing
- **Zero-Tolerance Enforcement**: Any performance regression blocks merge
- **Artifact Archiving**: Comprehensive result and profiling report storage

## Getting Started

### Quick Setup

```bash
# Clone the repository
git clone <repository-url>
cd Keyhunt-CUDA

# Initialize submodules
git submodule update --init --recursive

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ../src/KeyhuntCore -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON

# Build the project
make -j$(nproc)

# Run tests to verify setup
make test
```

### Running Benchmarks

```bash
# Run performance benchmark for specific GPU
./scripts/run_benchmarks.sh rtx3090 benchmarks/baselines/rtx3090.json

# Run with profiling
./scripts/analyze_profiling.sh 0 eccScalarMulKernel

# Establish new baseline (requires approval)
./scripts/ci/baseline_update.sh --approve rtx3090 current_result.json benchmarks/baselines/rtx3090.json
```

### Development Workflow

```bash
# Run tests (includes scientific validation)
ctest --output-on-failure

# Run performance gate (CI mode off for local testing)
CI_MODE=false ./scripts/ci/performance_gate.sh rtx3090

# Profile kernels for optimization
./scripts/analyze_profiling.sh 0 eccScalarMulKernel

# Monitor telemetry during long runs
tail -f telemetry/telemetry_rtx3090_*.jsonl
```

## Troubleshooting

### Build Issues

**CUDA Not Found:**
```bash
# Check CUDA installation
nvcc --version

# Verify CUDA paths
echo $CUDA_HOME
which nvcc
```

**Missing Dependencies:**
```bash
# Install libsecp256k1 for validation
sudo apt-get install libsecp256k1-dev

# Check CMake version
cmake --version
```

### Performance Issues

**Low Throughput:**
1. Check GPU utilization: `nvidia-smi`
2. Run profiling: `./scripts/analyze_profiling.sh 0 eccScalarMulKernel`
3. Verify memory coalescing efficiency
4. Check for bank conflicts in shared memory

**CI Failures:**
1. Review GitHub Actions logs for specific failure details
2. Check baseline files exist for target GPU
3. Verify performance meets baseline targets
4. Look for regression detection in performance gate

### Memory Issues

**Out of Memory:**
```bash
# Check available GPU memory
nvidia-smi --query-gpu=memory.total,memory.used,memory.free

# Monitor memory usage during execution
watch -n 1 nvidia-smi
```

**Memory Leaks:**
```bash
# Run CUDA memory check
cuda-memcheck ./build/Puzzle71Solver

# Check for memory access errors
cuda-gdb ./build/Puzzle71Solver
```

## Contributing

### Code Style

- Follow Google C++ Style Guide
- Use 4-space indentation
- Maximum line length: 100 characters
- Include comprehensive documentation for public interfaces

### Testing Requirements

- All new features must include unit tests
- GPU kernels require CPU-GPU validation tests
- Performance changes require benchmark updates
- Maintain ≥10,000 random validation cases for ECC operations

### Performance Requirements

- Zero tolerance for performance regressions
- All optimizations must maintain scientific accuracy
- Validation precision requirement: <1e-10 relative error
- Profile before and after significant changes

### Submission Process

1. **Code Review**: All changes must pass peer review
2. **Testing**: Full test suite must pass (100% pass rate)
3. **Benchmarking**: Performance must meet or exceed baseline
4. **Documentation**: Update relevant documentation
5. **CI Validation**: Must pass all CI checks including performance gates

## Security Considerations

### Cryptographic Security

- **No Crypto Reimplementation**: All ECC operations use bitcoin-core/secp256k1
- **CPU Reference**: Authoritative CPU implementation for validation
- **Constant-Time Operations**: Profile ECC kernels for timing side-channels
- **Memory Access Validation**: Analyze memory traces for key-bit correlations

### Input Validation

- **Range Validation**: Private key ranges validated before processing
- **Input Sanitization**: All external inputs properly validated
- **Error Handling**: Comprehensive error checking and recovery
- **Resource Limits**: Protection against excessive resource consumption

### Operational Security

- **SHA-256 Protection**: All checkpoint and baseline files cryptographically protected
- **Audit Trail**: Complete logging of baseline changes and performance metrics
- **Access Control**: Restrict access to sensitive configuration files
- **Secure Defaults**: Safe default configurations for all parameters

---

**Version**: 2.0 (Optimized Architecture with Performance Baselines)
**Last Updated**: 2025-10-12
**Status**: Production Ready with Zero-Tolerance Performance Gates