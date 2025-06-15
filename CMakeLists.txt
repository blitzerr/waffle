cmake_minimum_required(VERSION 3.20) # Using a recent, existing CMake version for C++20 support

# Set the project name
project(Waffle VERSION 1.0 LANGUAGES CXX)

# Configure FetchContent to use a persistent directory for dependencies.
# This directory will be outside the main build folder, so dependencies
# like Catch2 aren't rebuilt on every clean build of the main project.
include(FetchContent)
include(ExternalProject) # For building dependencies with specific configurations
set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.cmake_dependencies)
message(STATUS "FETCHCONTENT_BASE_DIR explicitly set to: ${FETCHCONTENT_BASE_DIR}")

# Disable compiler-specific extensions (e.g., GNU extensions) for stricter C++ standard compliance.
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20) # Set default C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Source Directory (src) ---
# This subdirectory defines the main Waffle library (STATIC) and
# the internal WaffleHelpers library (INTERFACE).
add_subdirectory(src)

# --- Waffle (Public Library) ---
# This is the main public library target.
# The Waffle target is now defined in src/CMakeLists.txt.
if(TARGET Waffle AND BUILD_COVERAGE AND COVERAGE_COMPILE_FLAGS)
  target_compile_options(Waffle PRIVATE ${COVERAGE_COMPILE_FLAGS})
  target_link_options(Waffle PRIVATE ${COVERAGE_LINK_FLAGS}) # Requires CMake 3.13+
  # For older CMake, you might try adding to target_link_libraries or CMAKE_CXX_FLAGS/CMAKE_EXE_LINKER_FLAGS
endif()

# Option to enable code coverage
option(BUILD_COVERAGE "Enable code coverage support (for GCC/gcov)" OFF)

set(COVERAGE_COMPILE_FLAGS "")
set(COVERAGE_LINK_FLAGS "")

if(BUILD_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # These flags enable gcov-style coverage information generation for both GCC and Clang.
    # GCC's --coverage is a shorthand for these and linking libgcov.
    # For Clang, these flags produce data that llvm-cov gcov can process.
    set(COVERAGE_FLAGS_LIST -fprofile-arcs -ftest-coverage)

    # For Debug builds (which is what the CI uses), it's often good practice
    # to ensure optimizations are low (e.g., -O0) for accurate coverage mapping.
    # CMAKE_BUILD_TYPE=Debug usually handles this, but you could add -O0 explicitly if needed:
    # list(APPEND COVERAGE_FLAGS_LIST -O0)

    string(JOIN " " COVERAGE_COMPILE_FLAGS "${COVERAGE_FLAGS_LIST}")
    string(JOIN " " COVERAGE_LINK_FLAGS "${COVERAGE_FLAGS_LIST}") # These flags are also used by the linker
    message(STATUS "Code coverage flags enabled for ${CMAKE_CXX_COMPILER_ID}: ${COVERAGE_COMPILE_FLAGS}")
  else()
    message(WARNING "BUILD_COVERAGE is ON, but the current C++ compiler (${CMAKE_CXX_COMPILER_ID}) is not GCC or Clang. Coverage flags will not be set.")
  endif()
endif()

# --- Testing Setup ---

# Add an option to enable/disable building of tests
option(WAFFLE_BUILD_TESTS "Build Waffle unit tests" ON)

# --- Catch2 Dependency (Always Build in Release) ---
if(WAFFLE_BUILD_TESTS)
    # Define persistent directories for ExternalProject_Add for Catch2.
    # All these directories will be within FETCHCONTENT_BASE_DIR to persist across cleans
    # of the main project's build directory.
    set(CATCH2_EP_ROOT_DIR ${FETCHCONTENT_BASE_DIR}/catch2_ep_managed) # Root for this external project's artifacts
    set(CATCH2_EP_SOURCE_DIR ${CATCH2_EP_ROOT_DIR}/src)       # ExternalProject will download/clone sources here
    set(CATCH2_EP_BINARY_DIR ${CATCH2_EP_ROOT_DIR}/build)     # ExternalProject will configure and build here
    set(CATCH2_EP_INSTALL_DIR ${CATCH2_EP_ROOT_DIR}/install)  # ExternalProject will install Catch2 here

    message(STATUS "Setting up ExternalProject for Catch2. Artifacts will be managed in: ${CATCH2_EP_ROOT_DIR}")

    ExternalProject_Add(
        catch2_ep # Name of the external project target
        GIT_REPOSITORY    https://github.com/catchorg/Catch2.git
        GIT_TAG           v3.5.2
        SOURCE_DIR        ${CATCH2_EP_SOURCE_DIR}  # ExternalProject will download/update sources to this directory
        BINARY_DIR        ${CATCH2_EP_BINARY_DIR}  # ExternalProject will configure and build in this directory
        # ExternalProject_Add will create its stamp files (to track build steps)
        # inside CMAKE_CURRENT_BINARY_DIR/CMakeFiles by default, or within <BINARY_DIR>/CMakeFiles
        # if BINARY_DIR is outside the main build tree, which is our case.
        # The download step will occur if SOURCE_DIR is empty or doesn't match GIT_TAG.
    # to make them persist across main project cleans.
        CMAKE_ARGS      # Arguments passed to CMake when configuring Catch2
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${CATCH2_EP_INSTALL_DIR}
            -DCATCH_BUILD_TESTING=OFF       # Don't build Catch2's own tests
            -DCATCH_INSTALL_DOCS=OFF        # Don't install Catch2's documentation
            -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
            -DCMAKE_CXX_STANDARD_REQUIRED=${CMAKE_CXX_STANDARD_REQUIRED}
            -DCMAKE_CXX_EXTENSIONS=${CMAKE_CXX_EXTENSIONS}
        # For multi-configuration generators, ensure Release is used for build & install
        BUILD_COMMAND     ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
        INSTALL_COMMAND   ${CMAKE_COMMAND} --build <BINARY_DIR> --target install --config Release
    )

    # Add the install directory of catch2_ep to CMAKE_PREFIX_PATH
    # This helps find_package(Catch2) in tests/CMakeLists.txt
    list(APPEND CMAKE_PREFIX_PATH ${CATCH2_EP_INSTALL_DIR})
endif()

if(WAFFLE_BUILD_TESTS)
  # Enable testing for the project (required for CTest)
  # This should be called before add_subdirectory(tests) if tests/CMakeLists.txt uses CTest commands like catch_discover_tests.
  enable_testing()

  # Add the tests subdirectory. CMake will look for a CMakeLists.txt file
  # in the 'tests' directory and process it.
  add_subdirectory(tests)
  # Example: If tests link against the public Waffle library
  # And also need WaffleHelpers for internal testing
  # test targets would link to Waffle and/or WaffleHelpers as needed.
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
  # Benchmarks link against WaffleHelpers, which is correct.
else()
  message(STATUS "Benchmarks are disabled.")
endif()

# --- Examples Setup ---
option(WAFFLE_BUILD_EXAMPLES "Build Waffle examples" ON) # Default to ON for normal builds

# Examples should demonstrate usage of the public Waffle library.
if(WAFFLE_BUILD_EXAMPLES)
  message(STATUS "Examples are enabled.")
  add_subdirectory(examples)
else()
  message(STATUS "Examples are disabled.")
endif()
