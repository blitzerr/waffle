# CMakeLists.txt for Waffle internal source components

add_library(WaffleHelpers INTERFACE)

# Add the current source directory (src/) to the include path for WaffleHelpers.
# This allows includes like <waffle/helpers/mpsc_ring_buffer.hpp>
# when mpsc_ring_buffer.hpp is located at src/waffle/helpers/mpsc_ring_buffer.hpp
target_include_directories(WaffleHelpers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
