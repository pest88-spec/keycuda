cmake_minimum_required(VERSION 3.22)

project(Puzzle71Solver VERSION 0.2.1 LANGUAGES CXX CUDA)

option(SECP256K1_AVAILABLE "Enable bitcoin-core/secp256k1 parity checks" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Reproducible builds
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Support Turing (75), Ampere (86), Ada Lovelace (89), Hopper (90)
set(CMAKE_CUDA_ARCHITECTURES 75-real 86-real 89-real 90-real)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# C++ compile options
add_compile_options(
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wextra>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>"
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>"
)

# CUDA compile options
add_compile_options(
  "$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-fno-omit-frame-pointer>"
  "$<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>"
  "$<$<COMPILE_LANGUAGE:CUDA>:-lineinfo>"
  # Basic performance optimizations
  "$<$<COMPILE_LANGUAGE:CUDA>:-use_fast_math>"
  "$<$<COMPILE_LANGUAGE:CUDA>:-ftz=true>"
  "$<$<COMPILE_LANGUAGE:CUDA>:-prec-div=false>"
  "$<$<COMPILE_LANGUAGE:CUDA>:-prec-sqrt=false>"
  "$<$<COMPILE_LANGUAGE:CUDA>:-fmad=true>"
)

set(PUZZLE71_CORE_SOURCES
  src/solver.cpp
  src/puzzle71_kernel.cu
  src/core/uint256.cpp
  src/checkpoint_manifest.cpp
  src/utils/digest_verifier.cpp
  src/utils/checkpoint_crypto.cpp
  src/utils/telemetry_logger.cpp
  src/utils/prometheus_exporter.cpp
  src/scheduler/range_scheduler.cpp
  src/traversal/puzzle71_partition.cpp
  src/services/device_metrics.cpp
  src/services/operator_metadata_validator.cpp
  src/config/puzzle71_config.cpp
  src/crypto/secp256k1_adapter.cpp
  src/ComputeCore/adapters/reference/gpu_context.cpp
  src/ComputeCore/shards/shard_walker.cpp
  src/ComputeCore/gpu/batch_planner.cpp
  src/ComputeCore/gpu/gpu_executor.cpp
  src/ComputeCore/gpu/device_buffers.cpp
  src/ComputeCore/gpu/memory_manager.cpp
  src/ComputeCore/gpu/performance/gpu_performance_manager.cpp
  src/ComputeCore/gpu/performance/synchronization_optimizer.cpp
  src/ComputeCore/gpu/performance/memory_optimizer.cpp
  src/ComputeCore/gpu/performance/adaptive_parallelism_scaling.cpp
  src/ComputeCore/gpu/performance/accuracy_validator.cpp
  src/ComputeCore/gpu/performance/performance_benchmark.cpp
  src/ComputeCore/gpu/performance/resource_profiler.cpp
  src/ComputeCore/gpu/performance/error_handler.cpp
  src/ComputeCore/gpu/performance/metrics_collector.cpp
  src/ComputeCore/gpu/performance/bandwidth_validator.cpp
  src/ComputeCore/gpu/performance/fused_initialization_kernel.cpp
  src/ComputeCore/gpu/performance/fallback_manager.cpp
  src/ComputeCore/gpu/performance/performance_logger.cpp
  src/ComputeCore/gpu/performance/worm_audit_logger.cpp
  src/ComputeCore/adapters/reference/conversions.cpp
)

set(PUZZLE71_MAIN_SOURCE src/main.cpp)

set(BITCRACK_SOURCES
  src/extracted/bitcrack/CudaKeySearchDevice/CudaAtomicList.cu
  src/extracted/bitcrack/CudaKeySearchDevice/CudaDeviceKeys.cu
  src/extracted/bitcrack/CudaKeySearchDevice/CudaHashLookup.cu
  src/extracted/bitcrack/CudaKeySearchDevice/cudabridge.cu
  src/extracted/bitcrack/CudaKeySearchDevice/CudaKeySearchDevice.cu
  src/extracted/bitcrack/CudaKeySearchDevice/CudaKeySearchDevice.cpp
  src/extracted/bitcrack/cudaUtil/cudaUtil.cpp
  src/extracted/bitcrack/AddressUtil/Base58.cpp
  src/extracted/bitcrack/AddressUtil/hash.cpp
  src/extracted/bitcrack/CryptoUtil/hash.cpp
  src/extracted/bitcrack/CryptoUtil/ripemd160.cpp
  src/extracted/bitcrack/CryptoUtil/sha256.cpp
  src/extracted/bitcrack/CryptoUtil/Rng.cpp
  src/extracted/bitcrack/util/util.cpp
  src/extracted/bitcrack/Logger/Logger.cpp
  src/extracted/bitcrack/secp256k1lib/secp256k1.cpp
)

# Build shared core library for solver and tests
add_library(puzzle71_core STATIC ${PUZZLE71_CORE_SOURCES} ${BITCRACK_SOURCES})

# Find CUDA and add include directories for both .cu and .cpp files
find_package(CUDAToolkit REQUIRED)
target_include_directories(puzzle71_core PUBLIC
  ${CUDAToolkit_INCLUDE_DIRS}
  src
  src/models
  src/utils
  src/config
  src/scheduler
  src/traversal
  src/services
  src/compare
  src/core
  src/ComputeCore
  src/ComputeCore/gpu
  src/ComputeCore/gpu/performance
  src/extracted/bitcrack
  src/extracted/bitcrack/AddressUtil
  src/extracted/bitcrack/CryptoUtil
  src/extracted/bitcrack/KeyFinderLib
  src/extracted/bitcrack/CudaKeySearchDevice
  src/extracted/bitcrack/cudaMath
  src/extracted/bitcrack/cudaUtil
  src/extracted/bitcrack/util
  src/extracted/bitcrack/Logger
  src/extracted/bitcrack/secp256k1lib
)
target_link_libraries(puzzle71_core PUBLIC CUDA::cudart)

if(SECP256K1_AVAILABLE)
  target_compile_definitions(puzzle71_core PUBLIC SECP256K1_AVAILABLE=1)
  set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/bitcoin-core-secp256k1)
  target_link_libraries(puzzle71_core PUBLIC secp256k1)
else()
  target_compile_definitions(puzzle71_core PUBLIC SECP256K1_AVAILABLE=0)
endif()
set_target_properties(puzzle71_core PROPERTIES CUDA_ARCHITECTURES "75;86;89;90")

# Link dependencies (order matters: CUDA runtime must be linked before OpenSSL)
find_package(OpenSSL REQUIRED)
target_link_libraries(puzzle71_core PUBLIC
  OpenSSL::SSL
  OpenSSL::Crypto
  nlohmann_json::nlohmann_json
)

# Suppress warnings for extracted BitCrack code (preserve original behavior)
set_source_files_properties(${BITCRACK_SOURCES} PROPERTIES
  COMPILE_FLAGS "-w"  # Disable all warnings for extracted code
)

set(PUZZLE71_TEST_SUPPORT_SOURCES
  src/checkpoint_manifest.cpp
  src/utils/digest_verifier.cpp
  src/utils/checkpoint_crypto.cpp
  src/utils/telemetry_logger.cpp
  src/utils/prometheus_exporter.cpp
)

add_library(puzzle71_test_support STATIC ${PUZZLE71_TEST_SUPPORT_SOURCES})
target_include_directories(puzzle71_test_support PUBLIC
  ${CUDAToolkit_INCLUDE_DIRS}
  src
  src/utils
  src/config
  src/services
)
target_link_libraries(puzzle71_test_support PUBLIC
  CUDA::cudart
  OpenSSL::SSL
  OpenSSL::Crypto
  nlohmann_json::nlohmann_json
)

if(TARGET secp256k1)
  target_link_libraries(puzzle71_test_support PUBLIC secp256k1)
endif()

add_executable(Puzzle71Solver ${PUZZLE71_MAIN_SOURCE})
set_target_properties(Puzzle71Solver PROPERTIES ENABLE_EXPORTS ON)
target_link_libraries(Puzzle71Solver PRIVATE puzzle71_core)

# Enable testing / GoogleTest via FetchContent
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()

file(GLOB_RECURSE PUZZLE71_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp tests/*.cu)
if(PUZZLE71_TEST_SOURCES)
  add_executable(puzzle71_tests ${PUZZLE71_TEST_SOURCES})
  target_link_libraries(puzzle71_tests PRIVATE GTest::gtest_main GTest::gmock_main puzzle71_test_support)
  target_compile_definitions(puzzle71_tests PRIVATE PUZZLE71_TESTING=1)
  target_include_directories(puzzle71_tests PRIVATE
    src
    src/extracted/bitcrack
    third_party/bitcoin-core-secp256k1/include
  )
  add_test(NAME puzzle71_tests COMMAND puzzle71_tests)
endif()

# Benchmark helper target placeholder
add_custom_target(run-benchmarks
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-benchmarks.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Execute GPU benchmark harness"
)
