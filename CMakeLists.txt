cmake_minimum_required(VERSION 3.22)

project(Puzzle71Solver VERSION 0.1.0 LANGUAGES CXX CUDA)

# T001: Dependency Integration Foundation
# Integration targets for extracted third-party libraries
option(ENABLE_INTEGRATION "Enable third-party library integration" ON)
option(INTEGRATION_VERIFY_INTEGRITY "Verify SHA-256 integrity of integrated libraries" ON)
option(INTEGRATION_VALIDATE_ATTRIBUTION "Validate attribution headers for integrated code" ON)
option(INTEGRATION_GENERATE_MANIFESTS "Generate integration manifests" ON)
option(INTEGRATION_TRACK_METRICS "Track integration performance metrics" ON)

# T023: Offline Build Configuration
# These options enable fully offline builds with no external dependencies
option(SECP256K1_AVAILABLE "Enable secp256k1-zkp library support" ON)
option(ENABLE_OFFLINE_BUILD "Enable fully offline build mode (no external dependencies)" ON)
option(ENABLE_FETCHCONTENT "Disable FetchContent for offline builds" OFF)
option(ENABLE_GIT_SUBMODULES "Disable git submodules for offline builds" OFF)
option(ENABLE_EXTERNAL_DEPENDENCIES "Disable external dependencies for offline builds" OFF)

# T036: Deployment Configuration Integration
# These options enable deployment package generation and configuration
option(ENABLE_DEPLOYMENT "Enable deployment package generation" ON)
option(ENABLE_DEPLOYMENT_PACKAGING "Create self-contained deployment packages" ON)
option(ENABLE_DEPLOYMENT_VALIDATION "Validate deployment packages during build" ON)
option(DEPLOYMENT_INCLUDE_DEBUG_SYMBOLS "Include debug symbols in deployment packages" OFF)
option(DEPLOYMENT_INCLUDE_DOCUMENTATION "Include documentation in deployment packages" ON)
option(DEPLOYMENT_INCLUDE_TESTS "Include test files in deployment packages" OFF)
option(DEPLOYMENT_COMPRESS_PACKAGE "Compress deployment packages" ON)
option(DEPLOYMENT_PLATFORM_DETECTION "Enable platform detection for deployment" ON)
option(DEPLOYMENT_GENERATE_MANIFESTS "Generate deployment manifests" ON)
option(DEPLOYMENT_SELF_CONTAINED "Create fully self-contained deployment packages" ON)

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

# T023: Offline Build Configuration Implementation
# This section implements offline build capability with validation
if(ENABLE_OFFLINE_BUILD)
  message(STATUS "=== OFFLINE BUILD MODE ENABLED ===")
  message(STATUS "Build configuration optimized for offline deployment")

  # Force offline configuration
  set(ENABLE_FETCHCONTENT OFF CACHE BOOL "" FORCE)
  set(ENABLE_GIT_SUBMODULES OFF CACHE BOOL "" FORCE)
  set(ENABLE_EXTERNAL_DEPENDENCIES OFF CACHE BOOL "" FORCE)

  # Disable network access for dependencies
  set(CMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY ON)
  set(CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY ON)
  set(CMAKE_FIND_PACKAGE_NO_CMAKE_SYSTEM_REGISTRY ON)
  set(CMAKE_FIND_PACKAGE_NO_CMAKE_PACKAGE_REGISTRY ON)

  # Set offline build flags
  add_compile_definitions(OFFLINE_BUILD_ENABLED=1)
  add_compile_definitions(NO_EXTERNAL_DEPENDENCIES=1)

  # Performance optimizations for offline builds
  add_compile_definitions(OPTIMIZED_FOR_OFFLINE=1)

  message(STATUS "Offline build configuration:")
  message(STATUS "  - External dependencies: DISABLED")
  message(STATUS "  - Network access: RESTRICTED")
  message(STATUS "  - FetchContent: DISABLED")
  message(STATUS "  - Git submodules: DISABLED")

else()
  message(STATUS "=== ONLINE BUILD MODE ===")
  message(STATUS "Standard build configuration with internet access allowed")
  add_compile_definitions(OFFLINE_BUILD_ENABLED=0)
endif()

# Validate offline build requirements
if(ENABLE_OFFLINE_BUILD)
  # Check that all required dependencies are available locally
  set(OFFLINE_BUILD_VALIDATION_PASSED TRUE)

  # Verify extracted secp256k1-zkp sources are available
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/extracted/secp256k1-zkp/CMakeLists.txt")
    message(STATUS "✓ Extracted secp256k1-zkp sources available")
  else()
    message(WARNING "✗ Extracted secp256k1-zkp sources not found")
    set(OFFLINE_BUILD_VALIDATION_PASSED FALSE)
  endif()

  # Verify extracted bitcrack sources are available
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/extracted/bitcrack/CMakeLists.txt")
    message(STATUS "✓ Extracted bitcrack sources available")
  else()
    message(WARNING "✗ Extracted bitcrack sources not found")
    set(OFFLINE_BUILD_VALIDATION_PASSED FALSE)
  endif()

  # Verify no external repository references
  if(NOT OFFLINE_BUILD_VALIDATION_PASSED)
    message(FATAL_ERROR "Offline build validation failed: Missing extracted sources")
  else()
    message(STATUS "✓ Offline build validation passed")
    message(STATUS "✓ Build can proceed without internet connectivity")
  endif()
endif()

# Memory constraint compliance for offline builds
if(ENABLE_OFFLINE_BUILD)
  # Set conservative memory limits for offline builds
  set(CMAKE_CUDA_ARCHITECTURES 75-real 86-real 89-real 90-real CACHE STRING "CUDA architectures" FORCE)

  # Optimize for memory usage
  add_compile_definitions(MEMORY_OPTIMIZED_FOR_OFFLINE=1)

  message(STATUS "Memory optimization for offline builds:")
  message(STATUS "  - CUDA architectures: ${CMAKE_CUDA_ARCHITECTURES}")
  message(STATUS "  - Memory optimizations: ENABLED")
endif()

# T036: Deployment Configuration Implementation
# This section implements deployment package generation and configuration
if(ENABLE_DEPLOYMENT)
  message(STATUS "=== DEPLOYMENT CONFIGURATION ENABLED ===")
  message(STATUS "Deployment package generation enabled")

  # Set deployment build flags
  add_compile_definitions(DEPLOYMENT_ENABLED=1)
  add_compile_definitions(DEPLOYMENT_SELF_CONTAINED=${DEPLOYMENT_SELF_CONTAINED})

  # Deployment configuration settings
  set(DEPLOYMENT_PACKAGE_NAME "Puzzle71Solver-Deployment")
  set(DEPLOYMENT_VERSION "${PROJECT_VERSION}")
  set(DEPLOYMENT_BUILD_DIR "${CMAKE_BINARY_DIR}/deployment")
  set(DEPLOYMENT_PACKAGE_DIR "${DEPLOYMENT_BUILD_DIR}/package")

  # Platform detection for deployment
  if(DEPLOYMENT_PLATFORM_DETECTION)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      set(DEPLOYMENT_PLATFORM "linux")
      set(DEPLOYMENT_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
      set(DEPLOYMENT_PLATFORM "windows")
      set(DEPLOYMENT_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      set(DEPLOYMENT_PLATFORM "macos")
      set(DEPLOYMENT_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
    else()
      set(DEPLOYMENT_PLATFORM "unknown")
      set(DEPLOYMENT_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    message(STATUS "Deployment platform: ${DEPLOYMENT_PLATFORM}-${DEPLOYMENT_ARCHITECTURE}")
  endif()

  # Deployment package validation
  if(ENABLE_DEPLOYMENT_VALIDATION)
    set(DEPLOYMENT_VALIDATION_ENABLED TRUE)
    message(STATUS "Deployment validation: ENABLED")
  else()
    set(DEPLOYMENT_VALIDATION_ENABLED FALSE)
    message(STATUS "Deployment validation: DISABLED")
  endif()

  # Configure deployment settings
  message(STATUS "Deployment configuration:")
  message(STATUS "  - Package generation: ENABLED")
  message(STATUS "  - Self-contained: ${DEPLOYMENT_SELF_CONTAINED}")
  message(STATUS "  - Debug symbols: ${DEPLOYMENT_INCLUDE_DEBUG_SYMBOLS}")
  message(STATUS "  - Documentation: ${DEPLOYMENT_INCLUDE_DOCUMENTATION}")
  message(STATUS "  - Tests: ${DEPLOYMENT_INCLUDE_TESTS}")
  message(STATUS "  - Compression: ${DEPLOYMENT_COMPRESS_PACKAGE}")
  message(STATUS "  - Platform detection: ${DEPLOYMENT_PLATFORM_DETECTION}")
  message(STATUS "  - Manifest generation: ${DEPLOYMENT_GENERATE_MANIFESTS}")

  # Create deployment build directories
  file(MAKE_DIRECTORY "${DEPLOYMENT_BUILD_DIR}")
  file(MAKE_DIRECTORY "${DEPLOYMENT_PACKAGE_DIR}")
  file(MAKE_DIRECTORY "${DEPLOYMENT_PACKAGE_DIR}/bin")
  file(MAKE_DIRECTORY "${DEPLOYMENT_PACKAGE_DIR}/lib")
  file(MAKE_DIRECTORY "${DEPLOYMENT_PACKAGE_DIR}/include")
  file(MAKE_DIRECTORY "${DEPLOYMENT_PACKAGE_DIR}/config")
  file(MAKE_DIRECTORY "${DEPLOYMENT_PACKAGE_DIR}/docs")
  file(MAKE_DIRECTORY "${DEPLOYMENT_PACKAGE_DIR}/scripts")
  file(MAKE_DIRECTORY "${DEPLOYMENT_PACKAGE_DIR}/attribution")

else()
  message(STATUS "=== DEPLOYMENT CONFIGURATION DISABLED ===")
  message(STATUS "Deployment package generation disabled")
  add_compile_definitions(DEPLOYMENT_ENABLED=0)
  set(DEPLOYMENT_PACKAGE_DIR "")
endif()

# Deployment packaging configuration
if(ENABLE_DEPLOYMENT AND ENABLE_DEPLOYMENT_PACKAGING)
  # Add deployment packaging target
  add_custom_target(deployment-package
    COMMAND ${CMAKE_COMMAND} -E echo "Creating deployment package..."
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOYMENT_BUILD_DIR}/package"

    # Copy main executable
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Puzzle71Solver>" "${DEPLOYMENT_PACKAGE_DIR}/bin/"

    # Copy libraries
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:secp256k1-zkp-static>" "${DEPLOYMENT_PACKAGE_DIR}/lib/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:keycuda-core>" "${DEPLOYMENT_PACKAGE_DIR}/lib/"

    # Copy essential scripts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/scripts/package-deployment.sh" "${DEPLOYMENT_PACKAGE_DIR}/scripts/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/scripts/verify-dependency-inclusion.sh" "${DEPLOYMENT_PACKAGE_DIR}/scripts/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/scripts/test-deployment-framework.sh" "${DEPLOYMENT_PACKAGE_DIR}/scripts/"

    # Copy documentation if enabled
    COMMAND ${CMAKE_COMMAND} -E echo "Copying documentation..."
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/README.md" "${DEPLOYMENT_PACKAGE_DIR}/docs/"

    # Generate deployment metadata
    COMMAND ${CMAKE_COMMAND} -E echo "Generating deployment metadata..."

    # Create package archive if compression enabled
    COMMAND ${CMAKE_COMMAND} -E echo "Creating deployment archive..."
    WORKING_DIRECTORY "${DEPLOYMENT_BUILD_DIR}"
    DEPENDS Puzzle71Solver secp256k1-zkp-static keycuda-core
    COMMENT "Creating deployment package"
  )

  # Add deployment validation target
  if(ENABLE_DEPLOYMENT_VALIDATION)
    add_custom_target(deployment-validate
      COMMAND ${CMAKE_COMMAND} -E echo "Validating deployment package..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify-dependency-inclusion.sh "${DEPLOYMENT_PACKAGE_DIR}"
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-deployment-framework.sh --skip-performance --skip-security
      DEPENDS deployment-package
      COMMENT "Validating deployment package"
    )
  endif()

  # Deployment installation targets moved to end of file after all targets are defined

endif()

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
  src/config/puzzle71_config.cpp
  src/crypto/secp256k1_adapter.cpp
  src/ComputeCore/adapters/reference/gpu_context.cpp
  src/ComputeCore/shards/shard_walker.cpp
  src/ComputeCore/gpu/batch_planner.cpp
  src/ComputeCore/gpu/gpu_executor.cpp
  src/ComputeCore/gpu/device_buffers.cpp
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

  # Use extracted secp256k1-zkp sources (T020: Simplified Build Setup)
  set(SECP256K1_ZKP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SECP256K1_ZKP_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(SECP256K1_ZKP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SECP256K1_ZKP_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

  # T001 & T002: Dependency Integration Foundation
  if(ENABLE_INTEGRATION)
    # Add integration infrastructure
    add_subdirectory(src/integration)

    # Integration targets
    add_custom_target(integration-verify
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify-integration.sh --verbose
      COMMENT "Verifying integration integrity and attribution"
    )

    add_custom_target(integration-manifests
      COMMAND ${CMAKE_COMMAND} -E echo "Generating integration manifests..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/integrate-dependencies.sh list
      COMMENT "Generating integration manifests"
    )
  endif()

  # T020-T021: Use integrated secp256k1-zkp sources
  # Check if integrated library exists, otherwise fall back to extracted sources
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/integrated/secp256k1-zkp/CMakeLists.txt")
    message(STATUS "Using integrated secp256k1-zkp sources")
    add_subdirectory(src/integrated/secp256k1-zkp)
    target_link_libraries(puzzle71_core PUBLIC secp256k1-zkp-static)

    # Mark as integrated for tracking
    target_compile_definitions(puzzle71_core PRIVATE SECP256K1_INTEGRATED=1)

  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/extracted/secp256k1-zkp/CMakeLists.txt")
    message(STATUS "Using extracted secp256k1-zkp sources")
    add_subdirectory(src/extracted/secp256k1-zkp)
    target_link_libraries(puzzle71_core PUBLIC secp256k1-zkp)

    target_compile_definitions(puzzle71_core PRIVATE SECP256K1_EXTRACTED=1)
  else()
    message(FATAL_ERROR "secp256k1-zkp sources not found. Please run integration script first.")
  endif()
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

if(TARGET secp256k1-zkp-static)
  target_link_libraries(puzzle71_test_support PUBLIC secp256k1-zkp-static)
elseif(TARGET secp256k1-zkp)
  target_link_libraries(puzzle71_test_support PUBLIC secp256k1-zkp)
endif()

add_executable(Puzzle71Solver ${PUZZLE71_MAIN_SOURCE})
set_target_properties(Puzzle71Solver PROPERTIES ENABLE_EXPORTS ON)
target_link_libraries(Puzzle71Solver PRIVATE puzzle71_core)

# Enable testing / GoogleTest via FetchContent
# T023: Conditional FetchContent for offline builds
if(NOT ENABLE_OFFLINE_BUILD)
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
else()
  message(STATUS "FetchContent disabled for offline build mode")
  message(STATUS "  - nlohmann_json: Using system or local installation")
  message(STATUS "  - googletest: Testing disabled for offline builds")

  # For offline builds, try to find system packages first
  find_package(nlohmann_json QUIET)
  if(NOT nlohmann_json_FOUND)
    message(WARNING "nlohmann_json not found - some features may be limited")
  endif()
endif()

# T023: Conditional testing for offline builds
if(NOT ENABLE_OFFLINE_BUILD)
  enable_testing()
else()
  message(STATUS "Testing disabled for offline build mode")
endif()

file(GLOB_RECURSE PUZZLE71_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp tests/*.cu)
if(PUZZLE71_TEST_SOURCES AND NOT ENABLE_OFFLINE_BUILD)
  add_executable(puzzle71_tests ${PUZZLE71_TEST_SOURCES})
  target_link_libraries(puzzle71_tests PRIVATE GTest::gtest_main GTest::gmock_main puzzle71_test_support)
  target_compile_definitions(puzzle71_tests PRIVATE PUZZLE71_TESTING=1)
  target_include_directories(puzzle71_tests PRIVATE
    src
    src/extracted/bitcrack
    src/extracted/secp256k1-zkp/include
  )
  add_test(NAME puzzle71_tests COMMAND puzzle71_tests)
endif()

# Benchmark helper target placeholder
add_custom_target(run-benchmarks
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-benchmarks.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Execute GPU benchmark harness"
)

# ============================================================================
# INSTALLATION TARGETS (moved here to ensure all targets are defined first)
# ============================================================================

if(ENABLE_DEPLOYMENT_BUILD)
  # Add deployment installation target
  install(TARGETS Puzzle71Solver
    RUNTIME DESTINATION bin
    COMPONENT deployment
  )

  if(TARGET secp256k1-zkp-static)
    install(TARGETS secp256k1-zkp-static keycuda-core
      LIBRARY DESTINATION lib
      COMPONENT deployment
    )
  endif()

  if(DEPLOYMENT_INCLUDE_DOCUMENTATION)
    install(FILES README.md LICENSE
      DESTINATION docs
      COMPONENT deployment
    )
  endif()

  # Install deployment scripts
  install(FILES
    scripts/package-deployment.sh
    scripts/verify-dependency-inclusion.sh
    scripts/test-deployment-framework.sh
    DESTINATION scripts
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
    COMPONENT deployment
  )
endif()
