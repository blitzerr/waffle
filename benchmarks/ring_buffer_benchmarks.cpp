#include <benchmark/benchmark.h>
#include <waffle/helpers/mpsc_ring_buffer.hpp>
#include <thread>
#include <vector>
#include <numeric> // For std::iota

constexpr size_t BENCH_BUFFER_CAPACITY = 1024;

// Benchmark for single-threaded try_emplace and try_pop
static void BM_RingBuffer_SingleThread_EmplacePop(benchmark::State& state) {
    MpscRingBuffer<int> rb(BENCH_BUFFER_CAPACITY);
    int value = 0;
    for (auto _ : state) {
        // This loop is timed
        if (!rb.try_emplace(value++)) {
            state.SkipWithError("Buffer full during emplace");
            break;
        }
        int popped_value;
        if (!rb.try_pop(popped_value)) {
            state.SkipWithError("Buffer empty during pop");
            break;
        }
        benchmark::DoNotOptimize(popped_value); // Prevent optimization
    }
    state.SetItemsProcessed(state.iterations()); // Report items processed
}
BENCHMARK(BM_RingBuffer_SingleThread_EmplacePop);

// Benchmark for MPSC scenario: Multiple producers, one consumer
static void BM_RingBuffer_MPSC_EmplacePop(benchmark::State& state) {
    const int num_producers = state.range(0); // Get number of producers from benchmark argument
    MpscRingBuffer<long> rb(BENCH_BUFFER_CAPACITY * num_producers); // Scale capacity

    for (auto _ : state) {
        state.PauseTiming(); // Pause timing for setup
        std::vector<std::thread> producers;
        std::atomic<long> items_produced_total{0};
        const long items_per_thread = state.iterations() / num_producers > 0 ? state.iterations() / num_producers : 1;
        state.ResumeTiming(); // Resume timing for the actual benchmarked code

        for (int i = 0; i < num_producers; ++i) {
            producers.emplace_back([&rb, &items_produced_total, items_per_thread, i]() {
                for (long j = 0; j < items_per_thread; ++j) {
                    long val = (static_cast<long>(i) << 32) | j; // Unique value
                    while (!rb.try_emplace(val)) { std::this_thread::yield(); }
                    items_produced_total.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        long items_consumed = 0;
        while (items_consumed < items_per_thread * num_producers) {
            long popped_value;
            if (rb.try_pop(popped_value)) {
                benchmark::DoNotOptimize(popped_value);
                items_consumed++;
            } else {
                 if (items_produced_total.load(std::memory_order_relaxed) >= items_per_thread * num_producers && items_consumed < items_per_thread * num_producers) {
                    // All produced, but not all consumed yet, yield
                    std::this_thread::yield();
                 }
            }
        }

        state.PauseTiming(); // Pause again for teardown
        for (auto& t : producers) { t.join(); }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * num_producers); // Total items by all producers
}
// Run MPSC benchmark with 1, 2, and 4 producer threads
BENCHMARK(BM_RingBuffer_MPSC_EmplacePop)->Arg(1)->Arg(2)->Arg(4)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN(); // Generates main() for the benchmark executable