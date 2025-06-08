# CMakeLists.txt for the WaffleHelpers library

# Define WaffleHelpers as an INTERFACE library as it's primarily header-only.
# If you add .cpp files to this library later, you can change it back to STATIC
# and list the source files.
add_library(WaffleHelpers INTERFACE)

# Make the 'helpers/include' directory available for include statements.
# This allows consumers to use #include <waffle/helpers/mpsc_ring_buffer.hpp>
# The path ${CMAKE_CURRENT_SOURCE_DIR}/include points to the 'include'
# subdirectory within the 'helpers' directory.
target_include_directories(WaffleHelpers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Set C++ standard requirements for targets that link against WaffleHelpers.
# For INTERFACE libraries, these properties propagate to consumers.
target_compile_features(WaffleHelpers INTERFACE cxx_std_20)
# CXX_EXTENSIONS OFF is set globally in the root CMakeLists.txt,
# so it will apply to consumers of this interface library.