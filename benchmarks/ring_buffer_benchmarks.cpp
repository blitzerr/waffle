#include <benchmark/benchmark.h>
#include <numeric> // For std::iota
#include <thread>
#include <vector>
#include <waffle/helpers/mpsc_ring_buffer.hpp>

constexpr size_t BENCH_BUFFER_CAPACITY = 1024;

// Benchmark for single-threaded try_emplace and try_pop
/**
 * @brief BM_RingBuffer_SingleThread_EmplacePop
 *
 * @Measures: The combined latency of a single `try_emplace` operation
 * immediately followed by a single `try_pop` operation in a tight loop,
 * executed by a single thread. The buffer capacity is fixed.
 *
 * @What_To_Look_For:
 *   - **`items_per_second`**: This is the primary metric. A higher value is
 * better.
 *   - **`Time` (per iteration)**: Should be very low (nanoseconds).
 * `items_per_second` is often more intuitive for throughput.
 *   - This serves as a baseline for the "best-case"
 *     throughput of individual operations without contention or
 * queue-full/empty effects.
 *   - Consistency of results across runs.
 * @When_To_Be_Concerned:
 *   - Unusually low throughput (high latency per operation) compared to
 * expectations for simple memory operations.
 *   - High variability in results, which might indicate measurement issues or
 * external factors.
 */
static void BM_RingBuffer_SingleThread_EmplacePop(benchmark::State &state) {
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
/**
 * @brief BM_RingBuffer_MPSC_EmplacePop
 *
 * @Measures: The overall throughput of the MpscRingBuffer when multiple
 * producer threads are concurrently emplacing items and a single consumer
 * thread (the main benchmark thread) is popping them. The number of producers
 * is varied as an argument. The buffer capacity scales with the number of
 * producers.
 *
 * @What_To_Look_For:
 *   - **`items_per_second`**: The primary metric for overall throughput.
 *   - **Scalability**: How `items_per_second` changes as the number of
 *     producer threads (the benchmark argument) increases. Ideally, this value
 *     should increase, but diminishing returns are common.
 *   - **`Time` (per iteration)**: Reflects the time to process a batch of
 * items. Lower is better. Compare this across different producer counts.
 *   - Impact of `std::this_thread::yield()`: If `items_per_second` is
 * unexpectedly low, it might indicate excessive yielding when the buffer is
 * full (producers) or empty (consumer).
 *
 * @When_To_Be_Concerned:
 *   - Negative Scaling: Throughput decreases as more producers are added. This
 *     indicates severe contention issues.
 *   - Very Low Throughput: Significantly lower than single-threaded
 * performance, suggesting high overhead from synchronization or inefficient
 * yielding.
 *   - Stalls or Deadlocks: The benchmark failing to complete.
 */
static void BM_RingBuffer_MPSC_EmplacePop(benchmark::State &state) {
  const int num_producers =
      state.range(0); // Get number of producers from benchmark argument
  MpscRingBuffer<long> rb(BENCH_BUFFER_CAPACITY *
                          num_producers); // Scale capacity

  for (auto _ : state) {
    state.PauseTiming(); // Pause timing for setup
    std::vector<std::thread> producers;
    std::atomic<long> items_produced_total{0};
    const long items_per_thread = state.iterations() / num_producers > 0
                                      ? state.iterations() / num_producers
                                      : 1;
    state.ResumeTiming(); // Resume timing for the actual benchmarked code

    for (int i = 0; i < num_producers; ++i) {
      producers.emplace_back(
          [&rb, &items_produced_total, items_per_thread, i]() {
            for (long j = 0; j < items_per_thread; ++j) {
              long val = (static_cast<long>(i) << 32) | j; // Unique value
              while (!rb.try_emplace(val)) {
                std::this_thread::yield();
              }
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
        if (items_produced_total.load(std::memory_order_relaxed) >=
                items_per_thread * num_producers &&
            items_consumed < items_per_thread * num_producers) {
          // All produced, but not all consumed yet, yield
          std::this_thread::yield();
        }
      }
    }

    state.PauseTiming(); // Pause again for teardown
    for (auto &t : producers) {
      t.join();
    }
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() *
                          num_producers); // Total items by all producers
}
// Run MPSC benchmark with 1, 2, and 4 producer threads
BENCHMARK(BM_RingBuffer_MPSC_EmplacePop)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Unit(benchmark::kMillisecond);

// Benchmark for single-threaded try_emplace only (fill the buffer)
/**
 * @brief BM_RingBuffer_SingleThread_EmplaceOnly
 *
 * @Measures: The time taken to fill the ring buffer to its capacity using
 * `try_emplace` operations from a single thread. The buffer is re-initialized
 * and filled in each iteration of the benchmark loop.
 *
 * @What_To_Look_For:
 *   - **`items_per_second`**: Measures the rate of emplacing items to fill the
 * buffer.
 *   - **`Time` (per iteration)**: Time taken to fill the buffer once.
 *   - This isolates the producer-side logic without consumer interaction.
 *     Compare `items_per_second` here to roughly half of
 * `BM_RingBuffer_SingleThread_EmplacePop`'s rate.
 *   - Consistency of time taken to fill the buffer.
 *
 * @When_To_Be_Concerned:
 *   - Significantly slower than expected based on
 * `BM_RingBuffer_SingleThread_EmplacePop`, as it's half of that operation.
 *   - `state.SkipWithError` indicating the buffer became full prematurely,
 * which would point to a logic error in capacity management or the test itself.
 */
static void BM_RingBuffer_SingleThread_EmplaceOnly(benchmark::State &state) {
  MpscRingBuffer<int> rb(BENCH_BUFFER_CAPACITY);
  int value = 0;
  for (auto _ : state) {
    state.PauseTiming();
    // Clear the buffer for each iteration if it's not guaranteed to be empty.
    // For this test, we want to fill it from an empty state.
    // A simple way is to re-create it, or pop all elements.
    MpscRingBuffer<int> fresh_rb(BENCH_BUFFER_CAPACITY);
    value = 0;
    state.ResumeTiming();

    for (size_t i = 0; i < BENCH_BUFFER_CAPACITY; ++i) {
      if (!fresh_rb.try_emplace(value++)) {
        state.SkipWithError("Buffer full prematurely during emplace-only test");
        break;
      }
    }
  }
  state.SetItemsProcessed(BENCH_BUFFER_CAPACITY);
}
BENCHMARK(BM_RingBuffer_SingleThread_EmplaceOnly);

// Benchmark for single-threaded try_pop only (empty a pre-filled buffer)
/**
 * @brief BM_RingBuffer_SingleThread_PopOnly
 *
 * @Measures: The time taken to empty a pre-filled ring buffer using `try_pop`
 * operations from a single thread. The buffer is pre-filled in the setup phase
 * of each benchmark iteration.
 *
 * @What_To_Look_For:
 *   - **`items_per_second`**: Measures the rate of popping items to empty the
 * buffer.
 *   - **`Time` (per iteration)**: Time taken to empty the pre-filled buffer
 * once.
 *   - This isolates the consumer-side logic.
 *     Compare `items_per_second` here to roughly half of
 * `BM_RingBuffer_SingleThread_EmplacePop`'s rate.
 *   - Consistency of time taken to empty the buffer.
 *
 * @When_To_Be_Concerned:
 *   - Significantly slower than expected based on
 * `BM_RingBuffer_SingleThread_EmplacePop`.
 *   - `state.SkipWithError` indicating the buffer became empty prematurely,
 * pointing to an issue with pre-filling or pop logic.
 */
static void BM_RingBuffer_SingleThread_PopOnly(benchmark::State &state) {
  MpscRingBuffer<int> rb(BENCH_BUFFER_CAPACITY);
  int popped_value;
  for (auto _ : state) {
    state.PauseTiming();
    // Pre-fill the buffer
    for (size_t i = 0; i < BENCH_BUFFER_CAPACITY; ++i) {
      if (!rb.try_emplace(static_cast<int>(i))) {
        // Should not happen if buffer starts empty and capacity is correct
      }
    }
    state.ResumeTiming();

    for (size_t i = 0; i < BENCH_BUFFER_CAPACITY; ++i) {
      if (!rb.try_pop(popped_value)) {
        state.SkipWithError("Buffer empty prematurely during pop-only test");
        break;
      }
      benchmark::DoNotOptimize(popped_value);
    }
  }
  state.SetItemsProcessed(BENCH_BUFFER_CAPACITY);
}
BENCHMARK(BM_RingBuffer_SingleThread_PopOnly);

// Benchmark for MPSC scenario with high producer contention on a small buffer
/**
 * @brief BM_RingBuffer_MPSC_HighContention
 *
 * @Measures: The throughput of the MpscRingBuffer under high contention
 * conditions. This is achieved by using multiple producer threads, a very small
 * fixed buffer capacity, and a single consumer. This scenario is designed to
 * stress the producer-side slot acquisition logic (`_tail` CAS) and the
 * behavior when the buffer is frequently full.
 *
 * @What_To_Look_For:
 *   - **`items_per_second`**: The primary metric. Observe how it changes (or
 * doesn't) with an increasing number of producers (the benchmark argument).
 *   - **Throughput Stability/Scaling**: Whether `items_per_second` remains
 * stable, increases slightly, or degrades as producers are added. Expect
 * limited scaling due to the small buffer and single consumer; stability is a
 * good sign.
 *   - **`Time` (per iteration)**: Time to process the fixed batch of items.
 *   - Liveness: Ensures the system doesn't deadlock or livelock.
 *
 * @When_To_Be_Concerned:
 *   - Drastic Throughput Drop: A sharp decrease in `items_per_second` as
 * producers are added.
 *   - Stagnation: Throughput plateaus very early or even decreases, indicating
 * the contention management (e.g., CAS loop for `_tail`, yielding) is becoming
 * a bottleneck.
 *   - Benchmark Timeout/Failure: Suggests severe liveness issues.
 */
static void BM_RingBuffer_MPSC_HighContention(benchmark::State &state) {
  const int num_producers = state.range(0);
  const size_t small_capacity = 64; // Fixed small capacity
  MpscRingBuffer<long> rb(small_capacity);

  const long total_items_per_state_iteration =
      32768; // Items to push through per state loop
  const long items_per_producer =
      total_items_per_state_iteration / num_producers > 0
          ? total_items_per_state_iteration / num_producers
          : 1;
  const long actual_total_items_to_process = items_per_producer * num_producers;

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<std::thread> producers;
    std::atomic<long> items_produced_count{0};
    state.ResumeTiming();

    for (int i = 0; i < num_producers; ++i) {
      producers.emplace_back(
          [&rb, &items_produced_count, items_per_producer, i]() {
            for (long j = 0; j < items_per_producer; ++j) {
              long val = (static_cast<long>(i) << 32) | j;
              while (!rb.try_emplace(val)) {
                std::this_thread::yield();
              }
              items_produced_count.fetch_add(1, std::memory_order_relaxed);
            }
          });
    }

    long items_consumed = 0;
    while (items_consumed < actual_total_items_to_process) {
      long popped_value;
      if (rb.try_pop(popped_value)) {
        benchmark::DoNotOptimize(popped_value);
        items_consumed++;
      } else {
        if (items_consumed < actual_total_items_to_process) {
          std::this_thread::yield();
        }
      }
    }

    state.PauseTiming();
    for (auto &t : producers) {
      t.join();
    }
  }
  state.SetItemsProcessed(actual_total_items_to_process);
}
// Test with increasing number of producers on a small buffer
BENCHMARK(BM_RingBuffer_MPSC_HighContention)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN(); // Generates main() for the benchmark executable