# Puzzle71Solver Quick Start Guide

This guide provides quick instructions for building and running Puzzle71Solver with the new simplified setup process.

## 🚀 One-Command Build (Recommended)

The fastest way to get started:

```bash
# Clone the repository
git clone https://github.com/your-org/keycuda.git
cd keycuda

# Build everything in one command
./scripts/build-all-in-one.sh
```

That's it! The script will:
- ✓ Check and install system dependencies
- ✓ Verify extracted libraries
- ✓ Configure the build
- ✓ Compile the project
- ✓ Run tests (optional)
- ✓ Verify the build

## 📦 Prerequisites

### System Requirements
- **OS**: Linux (Ubuntu 18.04+, CentOS 7+, or similar)
- **RAM**: 8GB minimum, 16GB recommended
- **Storage**: 2GB free space

### Required Software
- **CMake**: 3.22 or higher
- **CUDA Toolkit**: 12.0 or higher
- **C++ Compiler**: GCC 9+ or Clang 10+
- **OpenSSL**: Development libraries
- **Git**: For version control (optional)

### GPU Requirements
- **NVIDIA GPU**: Turing (RTX 20xx) or newer
- **Compute Capability**: 7.5 or higher
- **GPU Memory**: 8GB minimum, 12GB recommended

## 🔧 Build Options

### Option 1: All-in-One Script (Easiest)
```bash
# Basic release build
./scripts/build-all-in-one.sh

# Clean debug build with testing
./scripts/build-all-in-one.sh --clean --type Debug --enable-tests

# Offline build (no network access)
./scripts/build-all-in-one.sh --offline --clean

# Custom build options
./scripts/build-all-in-one.sh --type Release --jobs 8 --dir my-build
```

### Option 2: Two-Step Build
```bash
# Step 1: Setup dependencies
./scripts/setup-simplified.sh

# Step 2: Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Option 3: Offline Build
```bash
# For environments without internet access
./scripts/build-offline.sh
```

### Option 4: Manual Build
```bash
# Traditional CMake workflow
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DOFFLINE_BUILD=ON
make -j$(nproc)
```

## 🏃 Running the Solver

After successful build, run the solver:

```bash
cd build  # or your build directory
./Puzzle71Solver --help
```

### Basic Usage Examples

```bash
# Generate default configuration
./Puzzle71Solver --generate-config default.conf

# Run with configuration file
./Puzzle71Solver --config default.conf

# Run on specific GPU
./Puzzle71Solver --device 0 --config default.conf

# Run with custom parameters
./Puzzle71Solver \
    --range "0x0000000000000000...0x1000000000000000" \
    --difficulty 56 \
    --threads 4 \
    --output "found_keys.txt"
```

## 🛠 Advanced Configuration

### Environment Variables
```bash
# Set build parallelism
export JOBS=8
./scripts/build-all-in-one.sh

# Custom build directory
export BUILD_DIR=custom-build
./scripts/build-all-in-one.sh
```

### CMake Options
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DOFFLINE_BUILD=ON \
    -DENABLE_INTEGRATION_SYSTEM=ON \
    -DSTRICT_ATTRIBUTION=ON \
    -DCMAKE_CUDA_ARCHITECTURES="75;86;89;90"  # GPU architectures
```

## 🔍 Troubleshooting

### Common Issues

**1. Missing CUDA Toolkit**
```bash
# Check CUDA installation
nvcc --version

# If not found, install from:
# https://developer.nvidia.com/cuda-downloads
```

**2. Build Failures**
```bash
# Clean build and retry
rm -rf build
./scripts/build-all-in-one.sh --clean
```

**3. Permission Issues**
```bash
# Build as regular user (not root)
sudo chown -R $USER:$USER /path/to/keycuda
./scripts/build-all-in-one.sh
```

**4. Missing OpenSSL**
```bash
# Ubuntu/Debian
sudo apt-get install libssl-dev

# CentOS/RHEL
sudo yum install openssl-devel
```

### Getting Help

```bash
# Check system requirements
./scripts/setup-simplified.sh

# Verbose build output
./scripts/build-all-in-one.sh --clean 2>&1 | tee build.log

# Test basic functionality
cd build && ./Puzzle71Solver --help
```

## 📊 Performance Tips

### GPU Optimization
- Use RTX 30xx/40xx series for best performance
- Ensure GPU is not running other intensive tasks
- Monitor GPU temperature during long runs

### Build Optimization
- Use `Release` build type for production
- Enable parallel builds (`-j$(nproc)`)
- Use NVMe SSD for faster compilation

### Runtime Optimization
- Adjust thread count based on GPU memory
- Use appropriate difficulty targets for your hardware
- Monitor power consumption and thermals

## 🔬 Testing and Validation

### Run Basic Tests
```bash
# If built with test support
cd build && ./puzzle71_tests
```

### Validate Installation
```bash
# Check library dependencies
ldd build/Puzzle71Solver

# Verify GPU detection
./Puzzle71Solver --list-devices

# Test small search range
./Puzzle71Solver --range "0x0...0x1000" --test-mode
```

## 📚 Documentation

### Detailed Guides
- [Build System Guide](docs/OFFLINE_BUILD.md)
- [Submodule-Free Build](docs/SUBMODULE_FREE_BUILD.md)
- [Research & Analysis](specs/002-/research.md)
- [Implementation Tasks](specs/002-/tasks.md)

### Configuration
- [CMake Options](CMakeLists.txt)
- [Default Configuration](src/config/puzzle71_config.cpp)
- [GPU Settings](src/ComputeCore/gpu/)

### Integration
- [Dependency Management](src/integration/)
- [Attribution Compliance](src/integration/verification/)
- [Performance Metrics](src/integration/metrics/)

## 🤝 Contributing

### Development Setup
```bash
# Clone with development setup
git clone --recurse-submodules https://github.com/your-org/keycuda.git
cd keycuda

# Development build
./scripts/build-all-in-one.sh --type Debug --enable-tests
```

### Running Integration Tests
```bash
# Verify attribution compliance
make verify-attribution

# Test integration system
make verify-integration

# Run all verification tests
make test-integration
```

## 🌟 Features

### ✅ Build Features
- **One-command build**: No complex setup required
- **Offline capable**: Works without internet access
- **Submodule-free**: No git submodule initialization needed
- **Cross-platform**: Works on major Linux distributions
- **Optimized**: Release builds with performance optimizations

### ✅ Runtime Features
- **GPU acceleration**: CUDA-enabled GPU computing
- **Multi-threading**: CPU parallel processing
- **Range partitioning**: Efficient key space division
- **Checkpoint system**: Resume interrupted searches
- **Multiple algorithms**: Support for various search strategies

### ✅ Development Features
- **Integration system**: Comprehensive dependency management
- **Attribution compliance**: 100% license compliance
- **Performance monitoring**: Real-time metrics and telemetry
- **Testing framework**: Unit and integration tests
- **Documentation**: Comprehensive guides and references

## 🎯 Next Steps

1. **First Build**: Run the one-command build script
2. **Configuration**: Generate and customize your config file
3. **Test Run**: Execute a small test search
4. **Full Search**: Run with your target parameters
5. **Monitor**: Use built-in monitoring and logging

For additional support, check the documentation or open an issue on the project repository.

---

**Happy key hunting! 🎉**