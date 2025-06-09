# CMakeLists.txt for Waffle examples

if(NOT TARGET Waffle)
    message(FATAL_ERROR "The public Waffle library target is not defined. Make sure the top-level CMakeLists.txt defines it.")
endif()

add_executable(WaffleExample simple_usage.cpp)

# Link the example against your public Waffle library
target_link_libraries(WaffleExample PRIVATE Waffle)

# Set C++ standard for the example (if not inherited or set globally)
target_compile_features(WaffleExample PRIVATE cxx_std_20)

# Apply coverage flags if enabled
if(BUILD_COVERAGE AND COVERAGE_COMPILE_FLAGS AND TARGET WaffleExample)
    target_compile_options(WaffleExample PRIVATE ${COVERAGE_COMPILE_FLAGS})
    target_link_options(WaffleExample PRIVATE ${COVERAGE_LINK_FLAGS}) # Requires CMake 3.13+
endif()
