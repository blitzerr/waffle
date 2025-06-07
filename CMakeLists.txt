cmake_minimum_required(VERSION 3.20) # Using a recent, existing CMake version for C++20 support

# Set the project name
# It's good practice to specify the languages used by your project.
project(Waffle VERSION 1.0 LANGUAGES CXX)

# Disable compiler-specific extensions (e.g., GNU extensions) for stricter C++ standard compliance.
set(CMAKE_CXX_EXTENSIONS OFF)

# Add the executable
add_executable(Waffle main.cpp)

# Set C++ standard requirements for the MyProject target using compile features.
# This is the modern, preferred, and more granular way compared to setting CMAKE_CXX_STANDARD globally.
# 'PRIVATE' indicates these features are required for compiling MyProject itself.
# For C++20, the compile feature is 'cxx_std_20'.
target_compile_features(Waffle PRIVATE cxx_std_20)

# --- Testing with Catch2 ---

# Include FetchContent module to download dependencies
include(FetchContent)

# Declare Catch2 as a dependency
# Using a specific tag is recommended for reproducibility.
# As of early 2024, v3.5.2 is a recent stable tag for Catch2 v3.
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2 # You can update this to a newer stable Catch2 v3 tag
)

# Make Catch2 available. This will download (if needed) and configure it.
# The Catch2 targets (like Catch2::Catch2WithMain) will be available after this.
FetchContent_MakeAvailable(Catch2)

# Enable testing for the project (required for CTest)
enable_testing()

# Add the test executable. We'll assume test sources are in a 'tests' subdirectory.
add_executable(WaffleTests tests/waffle_tests.cpp)

# Link the test executable against Catch2::Catch2WithMain
# This target from Catch2 provides the main() function for the test runner.
target_link_libraries(WaffleTests PRIVATE Catch2::Catch2WithMain)

# Set C++ standard for the test target as well
target_compile_features(WaffleTests PRIVATE cxx_std_20)

# Discover tests automatically using CTest and Catch2's test registration
include(CTest)
include(Catch)
catch_discover_tests(WaffleTests)
