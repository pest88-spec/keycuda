# Developer Quickstart Guide

Get up and running with Puzzle71Solver development in minutes. This guide covers setup, building, testing, and performance optimization workflows.

## Prerequisites

### Hardware Requirements
- **GPU**: NVIDIA GPU with Compute Capability ≥ 7.5 (RTX 20 series or newer)
- **Memory**: ≥8GB RAM, GPU VRAM ≥4GB
- **Storage**: ≥10GB free space

### Software Requirements

| Component | Minimum Version | Installation Check |
|-----------|----------------|-------------------|
| **Operating System** | Ubuntu 20.04+ | `uname -a` |
| **NVIDIA Driver** | 535+ | `nvidia-smi` |
| **CUDA Toolkit** | 11.8+ | `nvcc --version` |
| **CMake** | 3.18+ | `cmake --version` |
| **Git** | 2.25+ | `git --version` |
| **Compiler** | GCC 10+ or Clang 12+ | `gcc --version` |

## Quick Setup (5 minutes)

### 1. Clone and Initialize

```bash
# Clone repository
git clone https://github.com/pest88-spec/keycuda.git
cd keycuda

# Switch to development branch
git checkout 001-implement-puzzle71solver-mred

# Initialize bitcoin-core/secp256k1 submodule (only one required)
git submodule update --init --recursive

# Verify BitCrack code is extracted (should show 33 files)
ls -la src/extracted/bitcrack/cudaMath/
```

### 2. Install Dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y build-essential cmake git pkg-config libssl-dev

# Install CUDA Toolkit (if not already installed)
sudo apt-get install -y cuda-toolkit-11-8

# Verify installation
nvcc --version
nvidia-smi
```

### 3. Build Project

```bash
# Clean build
rm -rf build
mkdir build && cd build

# Configure (Release mode with optimizations)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build (parallel compilation)
make -j$(nproc)

# Verify executables
ls -lh Puzzle71Solver puzzle71_tests
```

### 4. Run Tests

```bash
# Run all tests
./puzzle71_tests

# Or use CTest
ctest --output-on-failure

# Expected output: All tests PASSED
```

### 5. Quick Validation

```bash
# Test with known Puzzle 40 private key (5-10 seconds)
./Puzzle71Solver \
  --keyspace 0xe9ae490000:0xe9ae494000 \
  --target-address 1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv \
  --operator-id quickstart-test \
  --operator-purpose "Quickstart validation" \
  --device 0 \
  --super

# Expected: Found match with private key starting with 0x000...0e9ae4933d6
cat luck.txt
```

## Development Workflow

### Daily Development Commands

```bash
# 1. Pull latest changes
git pull origin 001-implement-puzzle71solver-mred

# 2. Clean rebuild
cd build && make clean && make -j$(nproc)

# 3. Run full test suite
ctest --output-on-failure

# 4. Run performance benchmarks
./scripts/run_benchmarks.sh auto

# 5. Check for performance regressions
./scripts/ci/performance_gate.sh rtx3090 benchmarks/baselines/rtx3090.json
```

### Testing Commands

```bash
# Unit tests (CUDA kernels)
cd build
ctest -L unit --output-on-failure

# Validation tests (CPU-GPU parity)
ctest -L validation --output-on-failure

# Performance tests
ctest -L performance --output-on-failure

# Run specific test
ctest -R "test_ecc_scalar_mul" -V
```

### Performance Optimization

```bash
# Run GPU performance analysis
./scripts/analyze_profiling.sh 0 eccScalarMulKernel

# Run comprehensive benchmarks
./scripts/run_benchmarks.sh rtx3090 benchmarks/baselines/rtx3090.json benchmarks/results/my_test.json

# View benchmark results
cat benchmarks/results/my_test_summary.txt
```

### Memory and Debugging

```bash
# Check for memory leaks
cuda-memcheck --leak-check full ./build/Puzzle71Solver --help

# Debug with cuda-gdb
cuda-gdb ./build/Puzzle71Solver

# Check GPU utilization during run
watch -n 1 nvidia-smi
```

## Project Structure

```
Puzzle71Solver/
├── src/
│   ├── KeyhuntCore/          # Main GPU engine
│   │   ├── kernels/          # CUDA kernels
│   │   ├── gpu/              # GPU management
│   │   ├── benchmarks/       # Performance benchmarking
│   │   └── utils/            # Utilities
│   ├── extracted/            # Third-party code (BitCrack)
│   └── crypto/               # CPU validation
├── tests/
│   ├── unit/                 # Kernel unit tests
│   ├── validation/           # CPU-GPU parity tests
│   └── performance/          # Performance tests
├── benchmarks/
│   ├── baselines/            # GPU-specific baselines
│   ├── results/              # Benchmark results
│   └── profiling/            # Nsight Compute reports
├── scripts/
│   ├── run_benchmarks.sh     # Performance runner
│   ├── analyze_profiling.sh  # Nsight Compute automation
│   └── ci/                   # CI automation
└── docs/
    ├── benchmarks/README.md  # Detailed benchmarking guide
    └── quickstart.md         # This file
```

## Performance Targets

The optimized GPU kernels achieve these performance targets:

| GPU Model | Target Throughput | GPU Utilization | Memory Bandwidth |
|-----------|-------------------|-----------------|------------------|
| RTX 2080 Ti | 1.0+ Gkeys/s | ≥90% | ≥70% |
| RTX 3090 | 2.0+ Gkeys/s | ≥90% | ≥70% |
| H20 | 3.5+ Gkeys/s | ≥90% | ≥70% |
| A100 | 4.0+ Gkeys/s | ≥90% | ≥70% |

## Common Development Tasks

### Adding New CUDA Kernels

1. **Write Test First** (Test-Driven Development):
   ```bash
   # Create test in tests/unit/test_my_kernel.cu
   # Ensure it FAILS initially (Red phase)
   ```

2. **Implement Kernel**:
   ```bash
   # Add kernel to src/KeyhuntCore/kernels/my_kernel.cu
   # Follow existing patterns for shared memory, SoA layout, warp primitives
   ```

3. **Make Test Pass**:
   ```bash
   # Build and test
   make -j$(nproc) && ctest -R "test_my_kernel" -V
   ```

### Performance Optimization

1. **Profile Current Performance**:
   ```bash
   ./scripts/analyze_profiling.sh 0 myKernel
   ```

2. **Check Key Metrics**:
   - Global Load Efficiency: ≥90%
   - Shared Memory Bank Conflicts: <5%
   - Achieved Occupancy: ≥50%

3. **Run Benchmark**:
   ```bash
   ./scripts/run_benchmarks.sh auto
   ```

### Updating Baselines

After performance improvements:

```bash
# Run benchmark to get new results
./scripts/run_benchmarks.sh rtx3090 benchmarks/baselines/rtx3090.json benchmarks/results/candidate.json

# Update baseline (requires explicit approval)
./scripts/ci/baseline_update.sh --approve rtx3090 benchmarks/results/candidate.json
```

## Troubleshooting

### Build Issues

**Problem**: `CUDA not found`
```bash
# Check CUDA installation
nvcc --version
which nvcc

# Set environment variables
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
```

**Problem**: `secp256k1.h not found`
```bash
# Initialize submodule
git submodule update --init --recursive

# Verify it exists
ls third_party/bitcoin-core-secp256k1/CMakeLists.txt
```

### Performance Issues

**Problem**: Low GPU utilization
```bash
# Check GPU utilization
nvidia-smi dmon -s u

# Profile memory access patterns
./scripts/analyze_profiling.sh 0 eccScalarMulKernel
```

**Problem**: Memory errors
```bash
# Check for memory leaks
cuda-memcheck --leak-check full ./build/Puzzle71Solver --help
```

### Test Failures

**Problem**: GPU tests skipped
```bash
# Check GPU availability
nvidia-smi

# Check CUDA devices
./build/Puzzle71Solver --help | grep -i gpu
```

## Best Practices

### Code Style
- Follow existing naming conventions
- Add inline comments explaining optimization rationale
- Use `CudaTestFixture` for GPU tests
- Document shared memory padding strategies

### Performance
- Always profile before optimizing
- Target ≥90% global load efficiency
- Use Structure-of-Arrays (SoA) layout
- Leverage shared memory for frequently accessed data
- Use warp shuffle for inter-thread communication

### Testing
- Write tests before implementation (TDD)
- Test with 10,000+ random keys for validation
- Verify CPU-GPU parity with <1e-10 relative error
- Run full test suite before committing

### Git Workflow
```bash
# Create feature branch
git checkout -b my-feature

# Make changes and test
# ... (development work) ...

# Run full validation
ctest --output-on-failure
./scripts/run_benchmarks.sh auto

# Commit and push
git add .
git commit -m "feat: Add my optimization"
git push origin my-feature
```

## Getting Help

### Resources
- **Detailed Benchmarking Guide**: [`docs/benchmarks/README.md`](docs/benchmarks/README.md)
- **Project Documentation**: [`README.md`](../README.md)
- **GPU Profiling**: Nsight Compute documentation

### Commands for Debugging
```bash
# Check system info
nvidia-smi
nvcc --version
cmake --version

# Run diagnostic tests
ctest -L diagnostic --output-on-failure

# Generate performance report
./scripts/analyze_profiling.sh 0 eccScalarMulKernel > profiling_report.txt
```

### Performance Verification
```bash
# Quick performance check (1 minute)
./scripts/run_benchmarks.sh auto --duration 60

# Full performance validation (10 minutes)
./scripts/run_benchmarks.sh auto

# Check regression
./scripts/ci/performance_gate.sh rtx3090 benchmarks/baselines/rtx3090.json
```

---

**Next Steps**: Once you're comfortable with the basic workflow, see the detailed [Performance Benchmarking Guide](docs/benchmarks/README.md) for advanced optimization techniques and CI integration.