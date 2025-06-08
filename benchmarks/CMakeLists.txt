# CMakeLists.txt for Waffle benchmarks

include(FetchContent)

# Declare Google Benchmark dependency
FetchContent_Declare(
  benchmark # Arbitrary name for FetchContent
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.3 # Use a recent stable tag
)

# Set options for the Google Benchmark project BEFORE making it available.
# This prevents it from trying to build its own tests or install itself.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable building/running of Google Benchmark's own tests")
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable installation of Google Benchmark")

FetchContent_MakeAvailable(benchmark)

# Add the benchmark executable
add_executable(WaffleBenchmarks
    ring_buffer_benchmarks.cpp
    # Add other benchmark_*.cpp files here
)

# Link against WaffleHelpers and Google Benchmark (benchmark::benchmark is the main library target)
target_link_libraries(WaffleBenchmarks PRIVATE
    WaffleHelpers
    benchmark::benchmark # Main Google Benchmark library
)

# Set C++ standard for the benchmark target
target_compile_features(WaffleBenchmarks PRIVATE cxx_std_20)