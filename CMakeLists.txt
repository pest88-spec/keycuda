cmake_minimum_required(VERSION 3.22)

project(Puzzle71Solver VERSION 0.1.0 LANGUAGES CXX CUDA)

option(SECP256K1_AVAILABLE "Enable bitcoin-core/secp256k1 parity checks" ON)
option(ENABLE_INTEGRATION_SYSTEM "Enable third-party library integration system" ON)
option(OFFLINE_BUILD "Configure for offline builds without external dependencies" OFF)
option(STRICT_ATTRIBUTION "Enforce 100% attribution coverage for extracted libraries" ON)
option(ENABLE_DEPLOYMENT_SYSTEM "Enable deployment package generation and testing" ON)
option(DEPLOYMENT_SELF_CONTAINED "Create self-contained deployment packages" ON)
option(DEPLOYMENT_INCLUDE_DOCS "Include documentation in deployment packages" ON)
option(DEPLOYMENT_COMPRESS_PACKAGE "Compress deployment packages" ON)

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

add_compile_options(
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wextra>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>"
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>"
  "$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-fno-omit-frame-pointer>"
  "$<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>"
  "$<$<COMPILE_LANGUAGE:CUDA>:-lineinfo>"
)

# Integration management sources
set(INTEGRATION_SOURCES
  src/integration/integration_manager.cpp
  src/integration/logging/integration_logger.cpp
  src/integration/verification/integrity_verifier.cpp
  src/integration/verification/digest_verifier.cpp
  src/integration/metrics/baseline_measurer.cpp
  src/integration/deployment_config_logger.cpp
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

add_executable(Puzzle71Solver ${PUZZLE71_CORE_SOURCES})
set_target_properties(Puzzle71Solver PROPERTIES ENABLE_EXPORTS ON)

# Find CUDA and add include directories for both .cu and .cpp files
find_package(CUDAToolkit REQUIRED)
target_include_directories(Puzzle71Solver PRIVATE
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
  src/integration
  src/integration/logging
  src/integration/verification
  src/integration/config
  src/integration/metrics
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
  # secp256k1-zkp integration (when available)
  src/extracted/secp256k1-zkp/include
  src/extracted/secp256k1-zkp/src
)
target_link_libraries(Puzzle71Solver PRIVATE CUDA::cudart)

# Integration system configuration (moved after target creation)
if(ENABLE_INTEGRATION_SYSTEM)
  target_compile_definitions(Puzzle71Solver PRIVATE ENABLE_INTEGRATION_SYSTEM=1)
  message(STATUS "Integration system enabled")
else()
  target_compile_definitions(Puzzle71Solver PRIVATE ENABLE_INTEGRATION_SYSTEM=0)
  message(STATUS "Integration system disabled")
endif()

if(OFFLINE_BUILD)
  target_compile_definitions(Puzzle71Solver PRIVATE OFFLINE_BUILD=1)
  message(STATUS "Configuring for offline build")
endif()

if(STRICT_ATTRIBUTION)
  target_compile_definitions(Puzzle71Solver PRIVATE STRICT_ATTRIBUTION=1)
  message(STATUS "Strict attribution enforcement enabled")
endif()

# Deployment system configuration
if(ENABLE_DEPLOYMENT_SYSTEM)
  target_compile_definitions(Puzzle71Solver PRIVATE ENABLE_DEPLOYMENT_SYSTEM=1)
  message(STATUS "Deployment system enabled")

  # Configure deployment-specific compile definitions
  if(DEPLOYMENT_SELF_CONTAINED)
    target_compile_definitions(Puzzle71Solver PRIVATE DEPLOYMENT_SELF_CONTAINED=1)
    message(STATUS "Self-contained deployment packages enabled")
  endif()

  # Set deployment configuration variables
  set(DEPLOYMENT_VERSION "${PROJECT_VERSION}")
  set(DEPLOYMENT_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
  set(DEPLOYMENT_TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")

  # Get current git commit for deployment tracking
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_COMMIT_HASH
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(GIT_COMMIT_HASH)
      set(DEPLOYMENT_GIT_COMMIT "${GIT_COMMIT_HASH}")
    endif()
  endif()

  message(STATUS "Deployment version: ${DEPLOYMENT_VERSION}")
  message(STATUS "Deployment build type: ${DEPLOYMENT_BUILD_TYPE}")
  message(STATUS "Deployment target arch: ${DEPLOYMENT_TARGET_ARCH}")
  if(DEPLOYMENT_GIT_COMMIT)
    message(STATUS "Deployment git commit: ${DEPLOYMENT_GIT_COMMIT}")
  endif()

  # Additional deployment configuration variables
  set(DEPLOYMENT_SELF_CONTAINED "${DEPLOYMENT_SELF_CONTAINED}")
  set(DEPLOYMENT_INCLUDE_DOCS "${DEPLOYMENT_INCLUDE_DOCS}")
  set(DEPLOYMENT_COMPRESS_PACKAGE "${DEPLOYMENT_COMPRESS_PACKAGE}")

  # Generate timestamps for deployment tracking
  string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC")
  string(TIMESTAMP CONFIGURE_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC")
  set(BUILD_TIMESTAMP "${BUILD_TIMESTAMP}")
  set(CONFIGURE_TIMESTAMP "${CONFIGURE_TIMESTAMP}")

  # Package naming configuration
  set(DEPLOYMENT_CONFIG_PACKAGE_SUFFIX "-${DEPLOYMENT_BUILD_TYPE}-${DEPLOYMENT_TARGET_ARCH}")
  set(DEPLOYMENT_CONFIG_PACKAGE_NAME "${PROJECT_NAME}")
  set(DEPLOYMENT_CONFIG_PACKAGE_VERSION "${DEPLOYMENT_VERSION}")

  # Deployment validation configuration
  set(DEPLOYMENT_CONFIG_VERIFICATION_ENABLED "ON")
  set(DEPLOYMENT_CONFIG_TEST_ENABLED "ON")
  set(DEPLOYMENT_CONFIG_PARALLEL_TESTING "ON")

  # Logging configuration
  set(DEPLOYMENT_CONFIG_LOG_LEVEL "INFO")
  set(DEPLOYMENT_CONFIG_LOG_FORMAT "JSON")
  set(DEPLOYMENT_CONFIG_LOG_OUTPUT "console")

  # Performance configuration
  set(DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT "300")
  set(DEPLOYMENT_CONFIG_MAX_PARALLEL_JOBS "4")
  set(DEPLOYMENT_CONFIG_MEMORY_LIMIT "4096")

  # Security configuration
  set(DEPLOYMENT_CONFIG_CHECKSUM_ALGORITHM "SHA256")
  set(DEPLOYMENT_CONFIG_SIGNATURE_REQUIRED "OFF")
  set(DEPLOYMENT_CONFIG_INTEGRITY_CHECK "ON")
else()
  message(STATUS "Deployment system disabled")
endif()


set_target_properties(Puzzle71Solver PROPERTIES CUDA_ARCHITECTURES "75;86;89;90")

# Link dependencies (order matters: CUDA runtime must be linked before OpenSSL)
find_package(OpenSSL REQUIRED)
target_link_libraries(Puzzle71Solver PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  nlohmann_json::nlohmann_json
)

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

target_sources(Puzzle71Solver PRIVATE ${BITCRACK_SOURCES})

# Suppress warnings for extracted BitCrack code (preserve original behavior)
set_source_files_properties(${BITCRACK_SOURCES} PROPERTIES
  COMPILE_FLAGS "-w"  # Disable all warnings for extracted code
)

# secp256k1-zkp extracted sources (when available)
set(SECP256K1_ZKP_SOURCES
  src/extracted/secp256k1-zkp/src/secp256k1.c
  src/extracted/secp256k1-zkp/src/precompute_ecmult.c
  src/extracted/secp256k1-zkp/src/precompute_ecmult_gen.c
)

# Add all header files for dependency tracking
file(GLOB_RECURSE SECP256K1_ZKP_HEADERS
  CONFIGURE_DEPENDS
  src/extracted/secp256k1-zkp/include/*.h
  src/extracted/secp256k1-zkp/src/*.h
)

# Add secp256k1-zkp sources if they exist
set(AVAILABLE_SECP256K1_ZKP_SOURCES "")
foreach(source_file ${SECP256K1_ZKP_SOURCES})
  if(EXISTS ${CMAKE_SOURCE_DIR}/${source_file})
    list(APPEND AVAILABLE_SECP256K1_ZKP_SOURCES ${source_file})
  endif()
endforeach()

# Add header files if sources exist
if(AVAILABLE_SECP256K1_ZKP_SOURCES)
  list(APPEND AVAILABLE_SECP256K1_ZKP_SOURCES ${SECP256K1_ZKP_HEADERS})
  target_sources(Puzzle71Solver PRIVATE ${AVAILABLE_SECP256K1_ZKP_SOURCES})

  # Suppress warnings for extracted secp256k1-zkp code (preserve original behavior)
  set_source_files_properties(${AVAILABLE_SECP256K1_ZKP_SOURCES} PROPERTIES
    COMPILE_FLAGS "-w"  # Disable all warnings for extracted code
  )

  list(LENGTH AVAILABLE_SECP256K1_ZKP_SOURCES secp256k1_zkp_file_count)
  message(STATUS "Added ${secp256k1_zkp_file_count} secp256k1-zkp files (sources + headers)")
endif()

# Configure secp256k1 support based on offline mode and availability (moved after source processing)
# Check if bitcoin-core submodule is available and initialized
set(BITCOIN_CORE_SECP256K1_AVAILABLE FALSE)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/bitcoin-core-secp256k1/CMakeLists.txt")
  set(BITCOIN_CORE_SECP256K1_AVAILABLE TRUE)
endif()

if(SECP256K1_AVAILABLE AND NOT OFFLINE_BUILD AND BITCOIN_CORE_SECP256K1_AVAILABLE)
  target_compile_definitions(Puzzle71Solver PRIVATE SECP256K1_AVAILABLE=1)
  set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/bitcoin-core-secp256k1)
  target_link_libraries(Puzzle71Solver PRIVATE secp256k1)
  message(STATUS "Using bitcoin-core secp256k1 (external submodule)")
elseif(AVAILABLE_SECP256K1_ZKP_SOURCES)
  # Use extracted secp256k1-zkp instead of external dependency
  target_compile_definitions(Puzzle71Solver PRIVATE SECP256K1_AVAILABLE=1 SECP256K1_ZKP_EXTRACTED=1)
  message(STATUS "Using extracted secp256k1-zkp (offline/submodule-free mode)")
else()
  target_compile_definitions(Puzzle71Solver PRIVATE SECP256K1_AVAILABLE=0)
  message(STATUS "secp256k1 support disabled (submodule not available)")
endif()

# Enable testing / GoogleTest via FetchContent (conditional on offline build)
if(NOT OFFLINE_BUILD)
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
  message(STATUS "Offline build: Skipping FetchContent dependencies (nlohmann_json, googletest)")
  # Create stub targets for offline mode
  if(NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE /usr/include)  # System fallback
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
  endif()
endif()

# Enable testing only when not in offline mode
if(NOT OFFLINE_BUILD)
  enable_testing()

  file(GLOB_RECURSE PUZZLE71_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp tests/*.cu)
  if(PUZZLE71_TEST_SOURCES AND TARGET GTest::gtest_main)
    add_executable(puzzle71_tests ${PUZZLE71_TEST_SOURCES})
    target_link_libraries(puzzle71_tests PRIVATE GTest::gtest_main GTest::gmock_main Puzzle71Solver)
    target_link_libraries(puzzle71_tests PRIVATE
      OpenSSL::SSL
      OpenSSL::Crypto
      nlohmann_json::nlohmann_json
      CUDA::cudart
    )
    if(TARGET secp256k1)
      target_link_libraries(puzzle71_tests PRIVATE secp256k1)
    endif()
    target_compile_definitions(puzzle71_tests PRIVATE PUZZLE71_TESTING=1)
    target_include_directories(puzzle71_tests PRIVATE
      src
      src/extracted/bitcrack
    )
    # Include bitcoin-core secp256k1 only if available
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/bitcoin-core-secp256k1/include")
      target_include_directories(puzzle71_tests PRIVATE third_party/bitcoin-core-secp256k1/include)
    endif()
    add_test(NAME puzzle71_tests COMMAND puzzle71_tests)
  endif()
else()
  message(STATUS "Offline build: Testing disabled")
endif()

# Benchmark helper target placeholder
add_custom_target(run-benchmarks
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-benchmarks.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Execute GPU benchmark harness"
)

# Integration system targets
if(ENABLE_INTEGRATION_SYSTEM)
  # Integration verification target
  add_custom_target(verify-integration
    COMMAND ${CMAKE_COMMAND} -E echo "Verifying integration system..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify-integration.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Verify third-party library integration"
  )

  # Attribution verification target
  add_custom_target(verify-attribution
    COMMAND ${CMAKE_COMMAND} -E echo "Verifying attribution coverage..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify-attribution.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Verify attribution coverage for all integrated libraries"
  )

  # Integration setup target
  add_custom_target(setup-integration
    COMMAND ${CMAKE_COMMAND} -E echo "Setting up integration infrastructure..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/setup-integration.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Setup third-party library integration infrastructure"
  )

  # Dependency integration target
  add_custom_target(integrate-dependencies
    COMMAND ${CMAKE_COMMAND} -E echo "Integrating third-party dependencies..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/integrate-dependencies.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Integrate all third-party dependencies locally"
  )

  # Integration test target
  add_custom_target(test-integration
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target verify-integration
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target verify-attribution
    COMMENT "Run all integration verification tests"
  )

  # Create integration dependencies
  add_dependencies(test-integration verify-integration verify-attribution)

  # Dependency version management targets (T045)
  # Initialize dependency version management
  add_custom_target(init-dependency-version
    COMMAND ${CMAKE_COMMAND} -E echo "Initializing dependency version management..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/update-dependencies.sh init
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Initialize dependency version management system (T045)"
  )

  # Detect current dependency versions
  add_custom_target(detect-dependency-versions
    COMMAND ${CMAKE_COMMAND} -E echo "Detecting current dependency versions..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/update-dependencies.sh detect
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Detect current dependency versions (T045)"
  )

  # Check for available dependency updates
  add_custom_target(check-dependency-updates
    COMMAND ${CMAKE_COMMAND} -E echo "Checking for dependency updates..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/update-dependencies.sh check-updates
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Check for available dependency updates (T045)"
  )

  # Validate dependency compatibility
  add_custom_target(validate-dependency-compatibility
    COMMAND ${CMAKE_COMMAND} -E echo "Validating dependency compatibility..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/update-dependencies.sh validate
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Validate dependency version compatibility (T045)"
  )

  # Update all dependencies
  add_custom_target(update-dependencies
    COMMAND ${CMAKE_COMMAND} -E echo "Updating all dependencies..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/update-dependencies.sh update --force
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Update all available dependencies (T045)"
  )

  # Update external dependencies only
  add_custom_target(update-external-dependencies
    COMMAND ${CMAKE_COMMAND} -E echo "Updating external dependencies..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/update-dependencies.sh update-external --force
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Update external dependencies only (T045)"
  )

  # Update CMake dependencies only
  add_custom_target(update-cmake-dependencies
    COMMAND ${CMAKE_COMMAND} -E echo "Updating CMake dependencies..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/update-dependencies.sh update-cmake --force
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Update CMake FetchContent dependencies only (T045)"
  )

  # Generate dependency version report (JSON)
  add_custom_target(generate-dependency-report
    COMMAND ${CMAKE_COMMAND} -E echo "Generating dependency version report..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/update-dependencies.sh report json
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generate dependency version report in JSON format (T045)"
  )

  # Generate dependency summary report
  add_custom_target(generate-dependency-summary
    COMMAND ${CMAKE_COMMAND} -E echo "Generating dependency summary report..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/update-dependencies.sh report summary
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generate dependency version summary report (T045)"
  )

  # List available backup points
  add_custom_target(list-dependency-backups
    COMMAND ${CMAKE_COMMAND} -E echo "Listing dependency backup points..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/update-dependencies.sh list-backups
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "List available dependency backup points (T045)"
  )

  # Complete dependency management workflow
  add_custom_target(manage-dependencies
    COMMAND ${CMAKE_COMMAND} -E echo "Running complete dependency management workflow..."
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target init-dependency-version
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target detect-dependency-versions
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target validate-dependency-compatibility
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-dependency-updates
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target generate-dependency-summary
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Complete dependency management workflow (T045)"
  )

  # Automated update scheduling targets (T046)
  # Initialize update schedule system
  add_custom_target(init-update-schedule
    COMMAND ${CMAKE_COMMAND} -E echo "Initializing automated update schedule..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/schedule-dependency-updates.sh init
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Initialize automated dependency update schedule (T046)"
  )

  # Schedule daily dependency check
  add_custom_target(schedule-daily-check
    COMMAND ${CMAKE_COMMAND} -E echo "Scheduling daily dependency check..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/schedule-dependency-updates.sh check daily
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Schedule daily dependency check (T046)"
  )

  # Schedule weekly dependency check
  add_custom_target(schedule-weekly-check
    COMMAND ${CMAKE_COMMAND} -E echo "Scheduling weekly dependency check..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/schedule-dependency-updates.sh check weekly
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Schedule weekly dependency check (T046)"
  )

  # Schedule security scan
  add_custom_target(schedule-security-scan
    COMMAND ${CMAKE_COMMAND} -E echo "Scheduling security dependency scan..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/schedule-dependency-updates.sh check security
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Schedule security dependency scan (T046)"
  )

  # Process update queue
  add_custom_target(process-update-queue
    COMMAND ${CMAKE_COMMAND} -E echo "Processing dependency update queue..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/schedule-dependency-updates.sh queue
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Process dependency update queue (T046)"
  )

  # Generate update status report
  add_custom_target(generate-update-status
    COMMAND ${CMAKE_COMMAND} -E echo "Generating dependency update status report..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/schedule-dependency-updates.sh status detailed
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generate dependency update status report (T046)"
  )

  # Clean up old update records
  add_custom_target(cleanup-update-records
    COMMAND ${CMAKE_COMMAND} -E echo "Cleaning up old update records..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/schedule-dependency-updates.sh cleanup
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Clean up old update records (T046)"
  )

  # Enable automated updates
  add_custom_target(enable-automated-updates
    COMMAND ${CMAKE_COMMAND} -E echo "Enabling automated dependency updates..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/schedule-dependency-updates.sh enable
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Enable automated dependency updates (T046)"
  )

  # Disable automated updates
  add_custom_target(disable-automated-updates
    COMMAND ${CMAKE_COMMAND} -E echo "Disabling automated dependency updates..."
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/schedule-dependency-updates.sh disable
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Disable automated dependency updates (T046)"
  )

  # Complete automated update workflow
  add_custom_target(automate-dependency-updates
    COMMAND ${CMAKE_COMMAND} -E echo "Running automated dependency update workflow..."
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target init-update-schedule
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target schedule-daily-check
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target process-update-queue
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target generate-update-status
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Complete automated dependency update workflow (T046)"
  )

  # Deployment system targets
  if(ENABLE_DEPLOYMENT_SYSTEM)
    # Package deployment target
    add_custom_target(package-deployment
      COMMAND ${CMAKE_COMMAND} -E echo "Creating deployment package..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/package-deployment.sh
        --version "${DEPLOYMENT_VERSION}"
        --build-type "${DEPLOYMENT_BUILD_TYPE}"
        --arch "${DEPLOYMENT_TARGET_ARCH}"
        --commit "${DEPLOYMENT_GIT_COMMIT}"
        ${DEPLOYMENT_SELF_CONTAINED_ARG}
        ${DEPLOYMENT_INCLUDE_DOCS_ARG}
        ${DEPLOYMENT_COMPRESS_ARG}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Create deployment package with all dependencies"
    )

    # Verify deployment target
    add_custom_target(verify-deployment
      COMMAND ${CMAKE_COMMAND} -E echo "Verifying deployment package..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify-deployment.sh
        --package-dir "${CMAKE_BINARY_DIR}/deployment"
        --strict
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Verify deployment package integrity and dependencies"
    )

    # Test deployment target
    add_custom_target(test-deployment
      COMMAND ${CMAKE_COMMAND} -E echo "Testing deployment package..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-deployment.sh
        --package-dir "${CMAKE_BINARY_DIR}/deployment"
        --environments "minimal,standard,production"
        --parallel
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Test deployment package across different environments"
    )

    # Complete deployment pipeline target
    add_custom_target(deploy
      COMMAND ${CMAKE_COMMAND} -E echo "Running complete deployment pipeline..."
      DEPENDS package-deployment verify-deployment test-deployment
      COMMENT "Run complete deployment pipeline: package, verify, and test"
    )

    # Deployment documentation generation
    add_custom_target(generate-deployment-docs
      COMMAND ${CMAKE_COMMAND} -E echo "Generating deployment documentation..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/generate-deployment-report.sh
        --output-dir "${CMAKE_BINARY_DIR}/deployment/docs"
        --include-build-info
        --include-dependencies
        --include-test-results
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Generate comprehensive deployment documentation"
    )

    # Deployment cleanup target
    add_custom_target(clean-deployment
      COMMAND ${CMAKE_COMMAND} -E echo "Cleaning deployment artifacts..."
      COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/deployment"
      COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_SOURCE_DIR}/deployment"
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Clean all deployment artifacts and packages"
    )

    # Set deployment script arguments based on CMake options
    if(DEPLOYMENT_SELF_CONTAINED)
      set(DEPLOYMENT_SELF_CONTAINED_ARG "--self-contained")
    endif()

    if(DEPLOYMENT_INCLUDE_DOCS)
      set(DEPLOYMENT_INCLUDE_DOCS_ARG "--include-docs")
    endif()

    if(DEPLOYMENT_COMPRESS_PACKAGE)
      set(DEPLOYMENT_COMPRESS_ARG "--compress")
    endif()

    # Create deployment directory structure
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/deployment")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/deployment/packages")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/deployment/docs")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/deployment/test-results")

    # Configure deployment configuration file
    configure_file(
      "${CMAKE_SOURCE_DIR}/cmake/deployment-config.cmake.in"
      "${CMAKE_BINARY_DIR}/deployment/deployment-config.cmake"
      @ONLY
    )

    message(STATUS "Deployment targets: package-deployment, verify-deployment, test-deployment, deploy")

    # Configuration logging target
    add_custom_target(log-deployment-config
      COMMAND ${CMAKE_COMMAND} -E echo "Logging deployment configuration decisions..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/log-deployment-config.sh
        --config-log "${CMAKE_BINARY_DIR}/deployment/deployment-config.log"
        --build-dir "${CMAKE_BINARY_DIR}"
        --output-format json
        --verbose
        log-cmake
        log-features
        log-dependencies
        log-environment
        log-build
        log-deployment
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Log all deployment configuration decisions for visibility"
    )

    # Configuration report generation target
    add_custom_target(generate-deployment-report
      COMMAND ${CMAKE_COMMAND} -E echo "Generating deployment configuration report..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/log-deployment-config.sh
        --config-log "${CMAKE_BINARY_DIR}/deployment/deployment-config.log"
        --build-dir "${CMAKE_BINARY_DIR}"
        --output-format markdown
        generate-report
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Generate comprehensive deployment configuration report"
    )

    # Enhanced deployment pipeline with configuration logging
    add_custom_target(deploy-with-logging
      COMMAND ${CMAKE_COMMAND} -E echo "Running deployment pipeline with configuration logging..."
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target log-deployment-config
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package-deployment
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target generate-deployment-report
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target verify-deployment
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test-deployment
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Run complete deployment pipeline with comprehensive configuration logging"
    )

    # Resource-constrained deployment targets
    add_custom_target(deploy-minimal
      COMMAND ${CMAKE_COMMAND} -E echo "Creating minimal deployment package..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/package-deployment.sh
        --mode minimal --memory-limit 1024 --cpu-cores 1
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Create minimal deployment package for resource-constrained environments"
    )

    add_custom_target(deploy-iot
      COMMAND ${CMAKE_COMMAND} -E echo "Creating IoT deployment package..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/package-deployment.sh
        --mode minimal --memory-limit 512 --cpu-cores 1 --disk-limit 2048
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Create IoT-optimized deployment package"
    )

    add_custom_target(deploy-embedded
      COMMAND ${CMAKE_COMMAND} -E echo "Creating embedded deployment package..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/package-deployment.sh
        --mode minimal --memory-limit 256 --cpu-cores 1 --disk-limit 1024
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Create embedded-optimized deployment package"
    )

    # Resource deployment testing target
    add_custom_target(test-resource-deployment
      COMMAND ${CMAKE_COMMAND} -E echo "Testing resource-constrained deployment..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/deploy-resource-constrained.sh
        --dry-run --verbose --iot-profile ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Test resource-constrained deployment configuration"
    )

    # Complete resource deployment pipeline
    add_custom_target(deploy-resources
      COMMAND ${CMAKE_COMMAND} -E echo "Running resource-constrained deployment pipeline..."
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package-deployment
      COMMAND ${CMAKE_COMMAND} -E echo "Testing resource deployment configurations..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/deploy-resource-constrained.sh
        --dry-run --memory-limit 1024 --cpu-cores 2 ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Create and test resource-constrained deployment packages"
    )

    # Cross-platform verification target (T040)
    add_custom_target(verify-cross-platform
      COMMAND ${CMAKE_COMMAND} -E echo "Verifying cross-platform compatibility..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify-cross-platform.sh
        --platforms "linux-x86_64,linux-aarch64,windows-x86_64,macos-x86_64,macos-arm64"
        --scenarios "basic,dependencies,performance,integration"
        --parallel ${DEPLOYMENT_CONFIG_MAX_PARALLEL_JOBS}
        --min-score 80
        --output "${CMAKE_BINARY_DIR}/deployment/cross-platform-report.md"
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Verify deployment package cross-platform compatibility"
    )

    # Quick cross-platform compatibility check
    add_custom_target(check-platform-compatibility
      COMMAND ${CMAKE_COMMAND} -E echo "Checking platform compatibility..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify-cross-platform.sh
        --platforms "linux-x86_64"
        --scenarios "basic"
        --dry-run
        --verbose
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Quick cross-platform compatibility check"
    )

    # Full cross-platform deployment verification
    add_custom_target(deploy-cross-platform
      COMMAND ${CMAKE_COMMAND} -E echo "Running full cross-platform deployment verification..."
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package-deployment
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target verify-cross-platform
      COMMAND ${CMAKE_COMMAND} -E echo "Cross-platform deployment verification complete"
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Package and verify cross-platform deployment compatibility"
    )

    # Deployment startup verification target (T041)
    add_custom_target(verify-startup
      COMMAND ${CMAKE_COMMAND} -E echo "Verifying deployment startup without external dependencies..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify-deployment-startup.sh
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        --memory-limit ${DEPLOYMENT_CONFIG_MEMORY_LIMIT}
        --retries 3
        --check-deps
        --verbose
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Verify deployment packages start successfully without external dependencies (T041)"
    )

    # Quick startup verification
    add_custom_target(check-startup
      COMMAND ${CMAKE_COMMAND} -E echo "Quick startup verification..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify-deployment-startup.sh
        --dry-run
        --timeout 30
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Quick deployment startup verification"
    )

    # Complete one-click deployment verification
    add_custom_target(verify-one-click-deployment
      COMMAND ${CMAKE_COMMAND} -E echo "Running complete one-click deployment verification..."
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package-deployment
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target verify-deployment
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target verify-startup
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-library-availability
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target verify-cross-platform
      COMMAND ${CMAKE_COMMAND} -E echo "One-click deployment verification complete"
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Complete one-click deployment verification pipeline"
    )

    # Library availability verification target (T042)
    add_custom_target(check-library-availability
      COMMAND ${CMAKE_COMMAND} -E echo "Checking library availability in deployment packages..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-library-availability.sh
        --verbose
        --check-all
        --output-format both
        --parallel ${DEPLOYMENT_CONFIG_MAX_PARALLEL_JOBS}
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Verify all required libraries are available locally in deployment packages (T042)"
    )

    # Quick library availability check
    add_custom_target(check-libs-quick
      COMMAND ${CMAKE_COMMAND} -E echo "Quick library availability check..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify-local-libraries.sh
        --quick
        --report-format summary
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Quick library availability verification"
    )

    # Comprehensive library availability check
    add_custom_target(check-libs-comprehensive
      COMMAND ${CMAKE_COMMAND} -E echo "Comprehensive library availability check..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-library-availability.sh
        --check-all
        --strict
        --output-format json
        --parallel ${DEPLOYMENT_CONFIG_MAX_PARALLEL_JOBS}
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Comprehensive library availability verification"
    )

    # Library availability analysis target
    add_custom_target(analyze-library-coverage
      COMMAND ${CMAKE_COMMAND} -E echo "Analyzing library coverage in deployment packages..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-library-availability.sh
        --check-all
        --output-format json
        --parallel ${DEPLOYMENT_CONFIG_MAX_PARALLEL_JOBS}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      COMMAND ${CMAKE_COMMAND} -E echo "Generating library coverage analysis..."
      COMMAND ${CMAKE_COMMAND} -E echo "Library availability analysis completed. Check logs/verification/ for detailed reports."
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Analyze library coverage and generate detailed availability reports"
    )

    # Validate library inclusion target
    add_custom_target(validate-library-inclusion
      COMMAND ${CMAKE_COMMAND} -E echo "Validating library inclusion requirements..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-library-availability.sh
        --strict
        --check-all
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        --output-format both
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Validate that all required libraries are included in deployment packages"
    )

    # Generate library inventory report
    add_custom_target(generate-library-inventory
      COMMAND ${CMAKE_COMMAND} -E echo "Generating library inventory report..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-library-availability.sh
        --check-all
        --output-format json
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      COMMAND ${CMAKE_COMMAND} -E echo "Library inventory report generated in logs/verification/"
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Generate comprehensive library inventory report for deployment packages"
    )

    # Cross-environment deployment testing target (T043)
    add_custom_target(test-cross-environment
      COMMAND ${CMAKE_COMMAND} -E echo "Testing cross-environment deployment consistency..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-cross-environment-deployment.sh
        --environments "minimal,standard,production"
        --scenarios "startup,dependencies,config,performance"
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        --parallel ${DEPLOYMENT_CONFIG_MAX_PARALLEL_JOBS}
        --generate-reports
        --output-format both
        --verbose
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Test consistent deployment behavior across multiple target environments (T043)"
    )

    # Quick cross-environment test
    add_custom_target(check-environment-consistency
      COMMAND ${CMAKE_COMMAND} -E echo "Quick cross-environment consistency check..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-cross-environment-deployment.sh
        --environments "minimal,standard"
        --scenarios "startup,dependencies"
        --timeout 60
        --output-format summary
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Quick cross-environment deployment consistency check"
    )

    # Comprehensive cross-environment test
    add_custom_target(test-all-environments
      COMMAND ${CMAKE_COMMAND} -E echo "Testing all environments comprehensively..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-cross-environment-deployment.sh
        --environments "minimal,standard,production,resource-constrained,development"
        --scenarios "startup,dependencies,config,performance,stress,security"
        --strict-fail
        --generate-reports
        --output-format both
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        --parallel ${DEPLOYMENT_CONFIG_MAX_PARALLEL_JOBS}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Comprehensive cross-environment deployment testing with strict validation"
    )

    # Environment-specific testing targets
    add_custom_target(test-minimal-environment
      COMMAND ${CMAKE_COMMAND} -E echo "Testing minimal environment deployment..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-cross-environment-deployment.sh
        --environments "minimal"
        --scenarios "startup,dependencies,config"
        --output-format both
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Test deployment in minimal resource environment"
    )

    add_custom_target(test-production-environment
      COMMAND ${CMAKE_COMMAND} -E echo "Testing production environment deployment..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-cross-environment-deployment.sh
        --environments "production"
        --scenarios "startup,dependencies,config,performance,stress,security"
        --output-format both
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Test deployment in production environment"
    )

    add_custom_target(test-resource-constrained-environment
      COMMAND ${CMAKE_COMMAND} -E echo "Testing resource-constrained environment deployment..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-cross-environment-deployment.sh
        --environments "resource-constrained"
        --scenarios "startup,dependencies,resource-constrained"
        --output-format both
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Test deployment in resource-constrained environment"
    )

    # Consistency analysis target
    add_custom_target(analyze-deployment-consistency
      COMMAND ${CMAKE_COMMAND} -E echo "Analyzing deployment consistency across environments..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-cross-environment-deployment.sh
        --environments "minimal,standard,production,resource-constrained"
        --scenarios "startup,dependencies,config,performance"
        --baseline "standard"
        --generate-reports
        --output-format json
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      COMMAND ${CMAKE_COMMAND} -E echo "Deployment consistency analysis completed. Check logs/verification/ for detailed reports."
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Analyze deployment consistency across multiple environments with baseline comparison"
    )

    # Environment compatibility validation
    add_custom_target(validate-environment-compatibility
      COMMAND ${CMAKE_COMMAND} -E echo "Validating environment compatibility..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-cross-environment-deployment.sh
        --environments "minimal,standard,production,resource-constrained,embedded"
        --scenarios "startup,dependencies,config"
        --strict-fail
        --output-format both
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Validate deployment package compatibility across multiple environments"
    )

  # Self-contained deployment validation target (T044)
    add_custom_target(validate-self-contained
      COMMAND ${CMAKE_COMMAND} -E echo "Validating self-contained deployment packages..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/validate-self-contained-deployment.sh
        --categories "external_dependencies,absolute_paths,library_inclusion,portability_checks"
        --portability-test
        --scenarios "isolated_environment,minimal_runtime_environment"
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        --output-format both
        --generate-report
        --verbose
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Validate deployment packages are self-contained and portable (T044)"
    )

    # Quick self-contained validation
    add_custom_target(check-self-contained
      COMMAND ${CMAKE_COMMAND} -E echo "Quick self-contained validation check..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/validate-self-contained-deployment.sh
        --categories "external_dependencies,library_inclusion"
        --timeout 60
        --output-format summary
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Quick self-contained deployment validation check"
    )

    # Comprehensive self-contained validation
    add_custom_target(validate-complete-self-contained
      COMMAND ${CMAKE_COMMAND} -E echo "Comprehensive self-contained deployment validation..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/validate-self-contained-deployment.sh
        --strict
        --isolation-test
        --portability-test
        --categories "external_dependencies,absolute_paths,system_specific_config,runtime_dependencies,library_inclusion,configuration_files,executable_integrity,documentation_completeness,portability_checks,resource_encapsulation"
        --scenarios "isolated_environment,different_filesystems,different_user_contexts,network_isolation,minimal_runtime_environment"
        --generate-report
        --keep-extracted
        --output-format both
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Comprehensive self-contained deployment validation with strict requirements"
    )

    # External dependency validation
    add_custom_target(validate-external-dependencies
      COMMAND ${CMAKE_COMMAND} -E echo "Validating external dependencies..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/validate-self-contained-deployment.sh
        --categories "external_dependencies,runtime_dependencies"
        --isolation-test
        --output-format both
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Validate deployment packages have no external dependencies"
    )

    # Portability validation
    add_custom_target(validate-portability
      COMMAND ${CMAKE_COMMAND} -E echo "Validating deployment package portability..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/validate-self-contained-deployment.sh
        --categories "absolute_paths,portability_checks"
        --portability-test
        --scenarios "isolated_environment,different_filesystems"
        --output-format both
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Validate deployment package portability across environments"
    )

    # Resource encapsulation validation
    add_custom_target(validate-resource-encapsulation
      COMMAND ${CMAKE_COMMAND} -E echo "Validating resource encapsulation..."
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/validate-self-contained-deployment.sh
        --categories "library_inclusion,configuration_files,resource_encapsulation"
        --isolation-test
        --output-format both
        --timeout ${DEPLOYMENT_CONFIG_DEFAULT_TIMEOUT}
        ${CMAKE_BINARY_DIR}/deployment/packages/*.tar.gz
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Validate complete resource encapsulation in deployment packages"
    )

    # One-click deployment validation (complete T041-T044)
    add_custom_target(validate-one-click-deployment
      COMMAND ${CMAKE_COMMAND} -E echo "Running complete one-click deployment validation..."
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target verify-startup
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-library-availability
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test-cross-environment
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target validate-self-contained
      COMMAND ${CMAKE_COMMAND} -E echo "One-click deployment validation complete - all requirements validated"
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Complete one-click deployment validation (T041-T044)"
    )

    # Deployment package quality assurance
    add_custom_target(qa-deployment-packages
      COMMAND ${CMAKE_COMMAND} -E echo "Running deployment package quality assurance..."
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package-deployment
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target verify-deployment
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target verify-startup
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-library-availability
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test-cross-environment
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target validate-self-contained
      COMMAND ${CMAKE_COMMAND} -E echo "Deployment package QA complete - all validations passed"
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Complete deployment package quality assurance pipeline"
    )

    message(STATUS "Integration targets: verify-integration, verify-attribution, test-integration, setup-integration, integrate-dependencies")
    message(STATUS "Dependency management targets: init-dependency-version, detect-dependency-versions, check-dependency-updates, validate-dependency-compatibility, update-dependencies, update-external-dependencies, update-cmake-dependencies, generate-dependency-report, generate-dependency-summary, list-dependency-backups, manage-dependencies")
    message(STATUS "Deployment targets: package-deployment, verify-deployment, test-deployment, deploy, log-deployment-config, generate-deployment-report, deploy-with-logging, deploy-minimal, deploy-iot, deploy-embedded, test-resource-deployment, deploy-resources, verify-cross-platform, check-platform-compatibility, deploy-cross-platform, verify-startup, check-startup, check-library-availability, check-libs-quick, check-libs-comprehensive, analyze-library-coverage, validate-library-inclusion, generate-library-inventory, test-cross-environment, check-environment-consistency, test-all-environments, test-minimal-environment, test-production-environment, test-resource-constrained-environment, analyze-deployment-consistency, validate-environment-compatibility, validate-self-contained, check-self-contained, validate-complete-self-contained, validate-external-dependencies, validate-portability, validate-resource-encapsulation, validate-one-click-deployment, qa-deployment-packages, verify-one-click-deployment")
  endif()
endif()
