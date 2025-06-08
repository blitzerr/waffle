cmake_minimum_required(VERSION 3.20) # Using a recent, existing CMake version for C++20 support

# Set the project name
# It's good practice to specify the languages used by your project.
project(Waffle VERSION 1.0 LANGUAGES CXX)

# Disable compiler-specific extensions (e.g., GNU extensions) for stricter C++ standard compliance.
set(CMAKE_CXX_EXTENSIONS OFF)

# Add the subdirectory for helper data structures.
# This will look for a CMakeLists.txt in the 'helpers' directory
# and process it, defining the WaffleHelpers library target.
add_subdirectory(helpers)

# Add the executable
add_executable(Waffle main.cpp)

target_link_libraries(Waffle PRIVATE WaffleHelpers)
# Set C++ standard requirements for the MyProject target using compile features.
# This is the modern, preferred, and more granular way compared to setting CMAKE_CXX_STANDARD globally.
# 'PRIVATE' indicates these features are required for compiling MyProject itself.
# For C++20, the compile feature is 'cxx_std_20'.
target_compile_features(Waffle PRIVATE cxx_std_20)

# --- Testing Setup ---

# Add an option to enable/disable building of tests
option(WAFFLE_BUILD_TESTS "Build Waffle unit tests" ON)

if(WAFFLE_BUILD_TESTS)
  # Enable testing for the project (required for CTest)
  # This should be called before add_subdirectory(tests) if tests/CMakeLists.txt uses CTest commands like catch_discover_tests.
  enable_testing()

  # Add the tests subdirectory. CMake will look for a CMakeLists.txt file
  # in the 'tests' directory and process it.
  add_subdirectory(tests)
else()
  message(STATUS "Tests are disabled.")
endif()

# --- Benchmark Setup ---
option(WAFFLE_BUILD_BENCHMARKS "Build Waffle microbenchmarks" OFF) # Default to OFF as they might take time

if(WAFFLE_BUILD_BENCHMARKS)
  message(STATUS "Benchmarks are enabled.")
  # Add the benchmarks subdirectory. CMake will look for a CMakeLists.txt file
  # in the 'benchmarks' directory and process it.
  add_subdirectory(benchmarks)
else()
  message(STATUS "Benchmarks are disabled.")
endif()
