cmake_minimum_required(VERSION 3.22)
project(VanitySearch LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)

# VanitySearch secp256k1 library
add_library(VanitySearch_secp256k1 STATIC
    # Core ECC files
    SECP256k1.cpp
    SECP256k1.h
    Int.cpp
    Int.h
    IntMod.cpp
    IntGroup.cpp
    IntGroup.h
    Point.cpp
    Point.h
    
    # Utility files
    Random.cpp
    Random.h
    Timer.cpp
    Timer.h
    Base58.cpp
    Base58.h
    Bech32.cpp
    Bech32.h
    
    # Hash functions (with SSE optimization)
    hash/sha256.cpp
    hash/ripemd160.cpp
    hash/sha256_sse.cpp
    hash/ripemd160_sse.cpp
)

# Enable SSE instructions for SSE files
set_source_files_properties(
    hash/sha256_sse.cpp
    hash/ripemd160_sse.cpp
    PROPERTIES COMPILE_FLAGS "-mssse3 -msse4.1"
)

target_include_directories(VanitySearch_secp256k1
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/hash
)

add_library(VanitySearch::secp256k1 ALIAS VanitySearch_secp256k1)

