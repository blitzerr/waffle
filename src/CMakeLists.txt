# CMakeLists.txt for Waffle internal source components

# --- WaffleHelpers (Internal Interface Library) ---
add_library(WaffleHelpers INTERFACE)

# Add the current source directory (src/) to the include path for WaffleHelpers.
# This allows includes like <waffle/helpers/mpsc_ring_buffer.hpp>
# when mpsc_ring_buffer.hpp is located at src/waffle/helpers/mpsc_ring_buffer.hpp
target_include_directories(WaffleHelpers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> # i.e., src/
)

# --- Waffle (Main Static Library) ---
add_library(Waffle STATIC)

# Add source files for the Waffle library.
# Paths are relative to this CMakeLists.txt (i.e., the 'src' directory).
target_sources(Waffle PRIVATE
    waffle/waffle_core.cpp
    # Add any other .cpp files from src/ that belong to the Waffle library here
)

# Include directories for the Waffle library.
# PRIVATE: For Waffle's own source files.
# Users will include headers from project_root/include/ (e.g., <waffle/tracer.hpp>).
# PUBLIC (BUILD_INTERFACE): For consumers linking against Waffle in the build tree.
# PUBLIC (INSTALL_INTERFACE): For consumers linking against Waffle after installation.
# Using CMAKE_CURRENT_SOURCE_DIR/.. to robustly get to the project root from src/.
target_include_directories(Waffle PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include> # For build tree
    $<INSTALL_INTERFACE:include> # For installation
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include # For Waffle's own compilation
)

# Link Waffle against WaffleHelpers. PUBLIC propagates WaffleHelpers' INTERFACE properties if needed.
target_link_libraries(Waffle PUBLIC WaffleHelpers)

# Set C++ standard for the Waffle library target
target_compile_features(Waffle PRIVATE cxx_std_20)
