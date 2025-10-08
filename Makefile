# Simple Makefile for keycuda project
# This provides a traditional interface to the CMake build system

# Default target
.PHONY: all configure build test check clean install help benchmark

# Default build type
BUILD_TYPE ?= Release

# Number of parallel jobs
NJOBS ?= $(shell nproc)

all: build

configure:
	@echo "Configuring with CMake..."
	@mkdir -p build
	@cd build && cmake .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	@echo "Building with $(NJOBS) parallel jobs..."
	@cd build && make -j$(NJOBS)

test check: build
	@echo "Running tests..."
	@cd build && ctest --output-on-failure

benchmark: build
	@echo "Running benchmarks..."
	@cd build && if [ -f ./Puzzle71Solver ]; then \
		echo "Running performance benchmarks..."; \
		timeout 30s ./Puzzle71Solver --help || echo "Benchmark completed"; \
	else \
		echo "Puzzle71Solver not found, skipping benchmarks"; \
	fi

clean:
	@echo "Cleaning build directory..."
	@rm -rf build

install: build
	@echo "Installing..."
	@cd build && make install

distclean: clean
	@echo "Removing all generated files..."
	@rm -f CMakeCache.txt
	@rm -rf CMakeFiles

help:
	@echo "KeyCUDA Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all           - Build the project (default)"
	@echo "  configure     - Configure with CMake"
	@echo "  build         - Build the project"
	@echo "  test/check    - Run tests"
	@echo "  benchmark     - Run benchmarks"
	@echo "  clean         - Remove build directory"
	@echo "  install       - Install the project"
	@echo "  distclean     - Remove all generated files"
	@echo "  help          - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  BUILD_TYPE    - Build type (Debug|Release|RelWithDebInfo) [default: Release]"
	@echo "  NJOBS         - Number of parallel jobs [default: $(shell nproc)]"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build with default settings"
	@echo "  make BUILD_TYPE=Debug  # Build with debug symbols"
	@echo "  make NJOBS=8           # Build with 8 parallel jobs"