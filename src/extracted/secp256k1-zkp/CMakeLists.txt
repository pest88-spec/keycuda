cmake_minimum_required(VERSION 3.22)

# Simplified CMakeLists.txt for extracted secp256k1-zkp library
# T020 & T022: Simplified Build Setup Integration
# This replaces the complex original CMakeLists.txt with a minimal
# configuration that works within our project structure

project(secp256k1-zkp VERSION 0.4.2 DESCRIPTION "Optimized C library for ECDSA operations on curve secp256k1" LANGUAGES C)

# Build configuration for integration
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Set reasonable defaults
set(SECP256K1_ECMULT_WINDOW_SIZE 15 CACHE STRING "EC mult window size" FORCE)

# Compiler flags
if(MSVC)
  add_compile_definitions(
    __inline__=__inline
    _CRT_SECURE_NO_WARNINGS
  )
  if(NOT SECP256K1_BUILD_STATIC)
    add_compile_definitions(SECP256K1_BUILD)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wno-unused-function)
endif()

# Basic source files for core functionality
set(SECP256K1_CORE_SOURCES
  src/secp256k1.c
)

# Header files
set(SECP256K1_HEADERS
  include/secp256k1.h
  include/secp256k1_bppp.h
  include/secp256k1_ecdh.h
  include/secp256k1_ecdsa_adaptor.h
  include/secp256k1_ecdsa_s2c.h
  include/secp256k1_ellswift.h
  include/secp256k1_extrakeys.h
  include/secp256k1_generator.h
  include/secp256k1_musig.h
  include/secp256k1_preallocated.h
  include/secp256k1_rangeproof.h
  include/secp256k1_recovery.h
  include/secp256k1_schnorrsig.h
  include/secp256k1_schnorrsig_halfagg.h
  include/secp256k1_surjectionproof.h
  include/secp256k1_whitelist.h
)

# Create the library target
add_library(secp256k1-zkp STATIC
  ${SECP256K1_CORE_SOURCES}
)

# Set include directories
target_include_directories(secp256k1-zkp
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Add compile definitions
target_compile_definitions(secp256k1-zkp
  PRIVATE
    ECMULT_WINDOW_SIZE=${SECP256K1_ECMULT_WINDOW_SIZE}
  HAVE_CONFIG_H
)

# Position independent code for static libraries
set_target_properties(secp256k1-zkp PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

# Suppress warnings for extracted code to maintain clean build output
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_target_properties(secp256k1-zkp PROPERTIES
    COMPILE_FLAGS "-w"
  )
endif()

# Integration verification
if(SECP256K1_AVAILABLE)
  message(STATUS "secp256k1-zkp library configured successfully")
  message(STATUS "  - Sources: ${SECP256K1_CORE_SOURCES}")
  message(STATUS "  - Headers: ${SECP256K1_HEADERS}")
  message(STATUS "  - ECMULT window size: ${SECP256K1_ECMULT_WINDOW_SIZE}")
else()
  message(WARNING "secp256k1-zkp library disabled")
endif()

# Mark this as an extracted library for build system tracking
set_target_properties(secp256k1-zkp PROPERTIES
  EXTRACTED_FROM "third_party/secp256k1-zkp"
  EXTRACTION_DATE "2025-10-22"
  ATTRIBUTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ATTRIBUTION.md"
)