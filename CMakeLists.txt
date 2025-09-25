cmake_minimum_required(VERSION 3.24)

project(Puzzle71Solver VERSION 0.1.0 LANGUAGES CXX CUDA)

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
)

add_executable(Puzzle71Solver ${PUZZLE71_CORE_SOURCES})
target_include_directories(Puzzle71Solver PRIVATE src src/models src/utils src/config src/scheduler src/scan src/services src/compare)
set_target_properties(Puzzle71Solver PROPERTIES CUDA_ARCHITECTURES "75;86;89")

find_package(OpenSSL REQUIRED)
target_link_libraries(Puzzle71Solver PRIVATE OpenSSL::SSL OpenSSL::Crypto)

# Enable testing / GoogleTest via FetchContent
include(FetchContent)
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
