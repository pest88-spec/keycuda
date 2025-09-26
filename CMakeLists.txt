cmake_minimum_required(VERSION 3.24)

project(Puzzle71Solver VERSION 0.1.0 LANGUAGES CXX CUDA)

option(SECP256K1_AVAILABLE "Enable bitcoin-core/secp256k1 parity checks" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Reproducible builds
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CUDA_ARCHITECTURES 75-real 86-real 89-real)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

add_compile_options(
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wextra>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>"
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>"
  "$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-fno-omit-frame-pointer>"
  "$<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>"
  "$<$<COMPILE_LANGUAGE:CUDA>:-lineinfo>"
)

set(PUZZLE71_CORE_SOURCES
  src/main.cpp
  src/solver.cpp
  src/puzzle71_kernel.cu
  src/core/uint256.cpp
  src/checkpoint_manifest.cpp
  src/utils/digest_verifier.cpp
  src/utils/checkpoint_crypto.cpp
  src/utils/telemetry_logger.cpp
  src/utils/prometheus_exporter.cpp
  src/scheduler/range_scheduler.cpp
  src/scan/puzzle71_partition.cpp
  src/services/device_metrics.cpp
  src/config/puzzle71_config.cpp
  src/crypto/secp256k1_adapter.cpp
  src/KeyhuntCore/gpu/device_buffers.cpp
  src/KeyhuntCore/adapters/bitcrack/conversions.cpp
  src/KeyhuntCore/adapters/bitcrack/keyfinder_adapter.cpp
)

add_executable(Puzzle71Solver ${PUZZLE71_CORE_SOURCES})
set_target_properties(Puzzle71Solver PROPERTIES ENABLE_EXPORTS ON)
target_include_directories(Puzzle71Solver PRIVATE
  src
  src/models
  src/utils
  src/config
  src/scheduler
  src/scan
  src/services
  src/compare
  src/core
  third_party/VanitySearch/GPU
  third_party/CudaBrainSecp/GPU
  third_party/BitCrack
  third_party/BitCrack/AddressUtil
  third_party/BitCrack/CryptoUtil
  third_party/BitCrack/KeyFinderLib
  third_party/BitCrack/CudaKeySearchDevice
  third_party/BitCrack/cudaMath
  third_party/BitCrack/cudaUtil
  third_party/BitCrack/util
  third_party/BitCrack/Logger
  third_party/BitCrack/secp256k1lib
)

if(SECP256K1_AVAILABLE)
  target_compile_definitions(Puzzle71Solver PRIVATE SECP256K1_AVAILABLE=1)
  set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/bitcoin-core-secp256k1)
  target_link_libraries(Puzzle71Solver PRIVATE secp256k1)
else()
  target_compile_definitions(Puzzle71Solver PRIVATE SECP256K1_AVAILABLE=0)
endif()

set_target_properties(Puzzle71Solver PROPERTIES CUDA_ARCHITECTURES "75;86;89")

find_package(OpenSSL REQUIRED)
target_link_libraries(Puzzle71Solver PRIVATE OpenSSL::SSL OpenSSL::Crypto nlohmann_json::nlohmann_json)

set(BITCRACK_SOURCES
  third_party/BitCrack/CudaKeySearchDevice/CudaAtomicList.cu
  third_party/BitCrack/CudaKeySearchDevice/CudaDeviceKeys.cu
  third_party/BitCrack/CudaKeySearchDevice/CudaHashLookup.cu
  third_party/BitCrack/CudaKeySearchDevice/cudabridge.cu
  third_party/BitCrack/CudaKeySearchDevice/CudaKeySearchDevice.cu
  third_party/BitCrack/CudaKeySearchDevice/CudaKeySearchDevice.cpp
  third_party/BitCrack/cudaUtil/cudaUtil.cpp
  third_party/BitCrack/AddressUtil/Base58.cpp
  third_party/BitCrack/AddressUtil/hash.cpp
  third_party/BitCrack/CryptoUtil/hash.cpp
  third_party/BitCrack/CryptoUtil/ripemd160.cpp
  third_party/BitCrack/CryptoUtil/sha256.cpp
  third_party/BitCrack/CryptoUtil/Rng.cpp
  third_party/BitCrack/util/util.cpp
  third_party/BitCrack/Logger/Logger.cpp
  third_party/BitCrack/secp256k1lib/secp256k1.cpp
)

target_sources(Puzzle71Solver PRIVATE ${BITCRACK_SOURCES})

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
  target_link_libraries(puzzle71_tests PRIVATE GTest::gtest_main GTest::gmock_main Puzzle71Solver)
  target_compile_definitions(puzzle71_tests PRIVATE PUZZLE71_TESTING=1)
  add_test(NAME puzzle71_tests COMMAND puzzle71_tests)
endif()

# Benchmark helper target placeholder
add_custom_target(run-benchmarks
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-benchmarks.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Execute GPU benchmark harness"
)
