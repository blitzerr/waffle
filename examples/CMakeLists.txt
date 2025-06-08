# CMakeLists.txt for Waffle examples

if(NOT TARGET Waffle)
    message(FATAL_ERROR "The public Waffle library target is not defined. Make sure the top-level CMakeLists.txt defines it.")
endif()

add_executable(WaffleExample simple_usage.cpp)

# Link the example against your public Waffle library
target_link_libraries(WaffleExample PRIVATE Waffle)

# Set C++ standard for the example (if not inherited or set globally)
target_compile_features(WaffleExample PRIVATE cxx_std_20)
