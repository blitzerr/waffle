# CMakeLists.txt for the Waffle tests

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

# Add the test executable.
# Source files are relative to this CMakeLists.txt (i.e., the 'tests' directory).
add_executable(WaffleTests
    waffle_tests.cpp
    ring_buffer_tests.cpp)

# Link the test executable against WaffleHelpers (defined in the parent scope)
# and Catch2::Catch2WithMain (provided by FetchContent_MakeAvailable(Catch2)).
target_link_libraries(WaffleTests PRIVATE WaffleHelpers Catch2::Catch2WithMain)

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