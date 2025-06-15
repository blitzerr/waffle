# CMakeLists.txt for the Waffle tests

# Find the Catch2 package.
# CMAKE_PREFIX_PATH should have been updated in the root CMakeLists.txt
# to include the installation directory of the externally built Catch2.
# The `catch2_ep` target (ExternalProject) must have been built and installed at least once
# for Catch2Config.cmake to be available.
find_package(Catch2 3 REQUIRED CONFIG) # Requesting Catch2 v3 specifically

# Add the test executable.
# Source files are relative to this CMakeLists.txt (i.e., the 'tests' directory).
add_executable(WaffleTests
    waffle_tests.cpp
    ring_buffer_tests.cpp)

# Ensure WaffleTests depends on the external project target for Catch2.
# This guarantees catch2_ep is built before WaffleTests attempts to link.
# The target `catch2_ep` is defined in the parent scope (root CMakeLists.txt).
add_dependencies(WaffleTests catch2_ep)

# Link the test executable against WaffleHelpers (defined in the parent scope)
# and Catch2::Catch2WithMain (provided by FetchContent_MakeAvailable(Catch2)).
target_link_libraries(WaffleTests PRIVATE Waffle WaffleHelpers Catch2::Catch2WithMain)

# Set C++ standard for the test target as well
target_compile_features(WaffleTests PRIVATE cxx_std_20)

# Discover tests automatically using CTest and Catch2's test registration
include(Catch) # Provides catch_discover_tests
catch_discover_tests(WaffleTests)

# Apply coverage flags if enabled (flags are defined in the root CMakeLists.txt)
if(BUILD_COVERAGE AND COVERAGE_COMPILE_FLAGS AND TARGET WaffleTests)
  target_compile_options(WaffleTests PRIVATE ${COVERAGE_COMPILE_FLAGS})
  target_link_options(WaffleTests PRIVATE ${COVERAGE_LINK_FLAGS}) # Requires CMake 3.13+
endif()