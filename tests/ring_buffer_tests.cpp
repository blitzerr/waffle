
#include <catch2/catch_all.hpp>
#include <climits> // For CHAR_BIT
#include <string>
#include <thread>
#include <vector>
#include <waffle/helpers/mpsc_ring_buffer.hpp>

#include <algorithm> // For std::sort, std::max
#include <set>       // For std::set in TestObject lifecycle test
// Helper struct to track constructions, destructions, moves, copies
struct TestObject {
  int id;
  std::string data;
  static std::atomic<int> constructions;
  static std::atomic<int> destructions;
  static std::atomic<int> moves;
  static std::atomic<int> copies;

  TestObject(int i = 0, std::string d = "default") : id(i), data(std::move(d)) {
    constructions.fetch_add(1, std::memory_order_relaxed);
  }

  ~TestObject() { destructions.fetch_add(1, std::memory_order_relaxed); }

  TestObject(const TestObject &other) : id(other.id), data(other.data) {
    copies.fetch_add(1, std::memory_order_relaxed);
    constructions.fetch_add(
        1,
        std::memory_order_relaxed); // Copy construction is still a construction
  }

  TestObject &operator=(const TestObject &other) {
    if (this != &other) {
      id = other.id;
      data = other.data;
      copies.fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
  }

  TestObject(TestObject &&other) noexcept
      : id(other.id), data(std::move(other.data)) {
    other.id = -1; // Mark as moved-from
    moves.fetch_add(1, std::memory_order_relaxed);
    constructions.fetch_add(
        1,
        std::memory_order_relaxed); // Move construction is still a construction
  }

  TestObject &operator=(TestObject &&other) noexcept {
    if (this != &other) {
      id = other.id;
      data = std::move(other.data);
      other.id = -1; // Mark as moved-from
      moves.fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
  }

  static void reset_counts() {
    constructions.store(0, std::memory_order_relaxed);
    destructions.store(0, std::memory_order_relaxed);
    moves.store(0, std::memory_order_relaxed);
    copies.store(0, std::memory_order_relaxed);
  }

  bool operator==(const TestObject &other) const {
    return id == other.id && data == other.data;
  }
};
std::atomic<int> TestObject::constructions{0};
std::atomic<int> TestObject::destructions{0};
std::atomic<int> TestObject::moves{0};
std::atomic<int> TestObject::copies{0};

TEST_CASE("MpscRingBuffer Construction and Capacity", "[ring_buffer]") {
  /**
   * @brief Verifies MpscRingBuffer constructor behavior and capacity
   * management. Objective: Ensure the constructor handles invalid capacity and
   * that the internal capacity is correctly rounded up to the next power of
   * two. Also tests basic operations at the minimum effective capacity.
   */
  SECTION("Zero capacity throws") {
    REQUIRE_THROWS_AS(MpscRingBuffer<int>(0), std::invalid_argument);
  }

  SECTION("Capacity is rounded to next power of two") {
    // Internal capacity is not directly exposed, so we infer it by attempting
    // to fill the buffer to what we expect its power-of-two capacity to be.
    // next_power_of_two(0) and next_power_of_two(1) both result in 2.
    // For n >= 2, it's the smallest power of two p such that p >= n.
    MpscRingBuffer<int> rb3(3); // next_power_of_two(3) is 4
    for (int i = 0; i < 4; ++i) {
      REQUIRE(rb3.try_emplace(i));
    }
    REQUIRE_FALSE(rb3.try_emplace(4));

    MpscRingBuffer<int> rb4(4); // next_power_of_two(4) is 4
    for (int i = 0; i < 4; ++i)
      REQUIRE(rb4.try_emplace(i));
    REQUIRE_FALSE(rb4.try_emplace(4));

    MpscRingBuffer<int> rb1(
        1); // next_power_of_two(1) is 2. Effective capacity is 2.
    REQUIRE(rb1.try_emplace(0));
    REQUIRE(rb1.try_emplace(1));
    REQUIRE_FALSE(rb1.try_emplace(2));
    // Pop one and ensure it's correct, then try to emplace again (wrap-around)
    int val;
    REQUIRE(rb1.try_pop(val));
    REQUIRE(val == 0);
    REQUIRE(rb1.try_emplace(2)); // Should succeed
    REQUIRE(rb1.try_pop(val));
    REQUIRE(val == 1);
    REQUIRE(rb1.try_pop(val));
    REQUIRE(val == 2);
    REQUIRE_FALSE(rb1.try_pop(val)); // Empty
  }
}

TEST_CASE("MpscRingBuffer Basic Operations", "[ring_buffer]") {
  /**
   * @brief Tests fundamental single-threaded operations of the MpscRingBuffer.
   * Objective: Verify `try_emplace` and `try_pop` under simple conditions.
   * Setup: A ring buffer with a small, fixed capacity (4).
   * Verifies: Correctness of adding and removing single elements, behavior on
   * empty/full buffer.
   */
  MpscRingBuffer<int> rb(4); // Actual capacity will be 4

  SECTION("Try emplace and try pop single element") {
    REQUIRE(rb.try_emplace(42));
    int val;
    REQUIRE(rb.try_pop(val));
    REQUIRE(val == 42);
    REQUIRE_FALSE(rb.try_pop(val)); // Buffer should be empty
  }

  SECTION("Try pop on empty buffer") {
    int val;
    REQUIRE_FALSE(rb.try_pop(val));
  }

  SECTION("Try emplace on full buffer") {
    for (int i = 0; i < 4; ++i) {
      REQUIRE(rb.try_emplace(i));
    }
    REQUIRE_FALSE(rb.try_emplace(100)); // Buffer is full

    // Pop one and try again
    int temp;
    REQUIRE(rb.try_pop(temp));
    REQUIRE(temp == 0);
    REQUIRE(rb.try_emplace(100)); // Should succeed now

    // Verify contents
    REQUIRE(rb.try_pop(temp));
    REQUIRE(temp == 1);
    REQUIRE(rb.try_pop(temp));
    REQUIRE(temp == 2);
    REQUIRE(rb.try_pop(temp));
    REQUIRE(temp == 3);
    REQUIRE(rb.try_pop(temp));
    REQUIRE(temp == 100);
    REQUIRE_FALSE(rb.try_pop(temp));
  }

  SECTION("Fill and empty buffer") {
    for (int i = 0; i < 4; ++i) {
      REQUIRE(rb.try_emplace(i * 10));
    }

    int val;
    for (int i = 0; i < 4; ++i) {
      REQUIRE(rb.try_pop(val));
      REQUIRE(val == i * 10);
    }
    REQUIRE_FALSE(rb.try_pop(val)); // Buffer should be empty
  }
}

TEST_CASE("MpscRingBuffer Wrap Around Behavior", "[ring_buffer]") {
  /**
   * @brief Verifies the correct wrap-around behavior of head and tail pointers.
   * Objective: Ensure that as items are added and removed, the internal indices
   *            correctly wrap around the buffer's capacity.
   * Setup: Buffers with small capacities (2 and 4) to easily induce
   * wrap-around. Verifies: Items are retrieved in FIFO order even after
   * multiple wraps.
   */
  MpscRingBuffer<int> rb(2); // Actual capacity 2

  // Fill
  REQUIRE(rb.try_emplace(1));
  REQUIRE(rb.try_emplace(2));
  REQUIRE_FALSE(rb.try_emplace(3)); // Full

  // Pop one, emplace one (tail wraps)
  int val;
  REQUIRE(rb.try_pop(val));
  REQUIRE(val == 1);
  REQUIRE(rb.try_emplace(3)); // Tail was 2, becomes 3. Masked index (3&1) = 1.

  // Pop remaining (head wraps)
  REQUIRE(rb.try_pop(val));
  REQUIRE(val == 2); // Head was 1, becomes 2. Masked index (1&1) = 1.
  REQUIRE(rb.try_pop(val));
  REQUIRE(val == 3); // Head was 2, becomes 3. Masked index (2&1) = 0.

  REQUIRE_FALSE(rb.try_pop(val));

  // More extensive wrap-around
  MpscRingBuffer<int> rb_large(4);       // Capacity 4
  for (int iter = 0; iter < 3; ++iter) { // Repeat a few times
    for (int i = 0; i < 4; ++i)
      REQUIRE(rb_large.try_emplace(iter * 100 + i));
    for (int i = 0; i < 2; ++i) {
      int v;
      REQUIRE(rb_large.try_pop(v));
      REQUIRE(v == iter * 100 + i);
    }
    for (int i = 4; i < 6; ++i)
      REQUIRE(rb_large.try_emplace(iter * 100 + i)); // Add 2 more

    for (int i = 2; i < 6; ++i) { // Pop remaining 4
      int v;
      REQUIRE(rb_large.try_pop(v));
      REQUIRE(v == iter * 100 + i);
    }
    REQUIRE_FALSE(rb_large.try_pop(val));
  }
}

TEST_CASE("MpscRingBuffer Object Lifecycle and Move Semantics",
          "[ring_buffer]") {
  /**
   * @brief Verifies correct object lifecycle (construction, destruction, moves)
   *        and the absence of copies when using TestObject with the ring
   * buffer. Objective: Ensure `TestObject` instances are handled correctly by
   * `try_emplace` (move construction) and `try_pop` (move
   * assignment/construction), and that objects left in the buffer are
   * destructed when the buffer itself is.
   */
  TestObject::reset_counts();
  {
    MpscRingBuffer<TestObject> rb(2); // Capacity 2
    REQUIRE(TestObject::constructions == 0);

    SECTION("Emplace and Pop TestObject") {
      TestObject::reset_counts();
      // try_emplace(Args&&...) performs two constructions for TestObject:
      // 1. TestObject temp_obj(std::forward<Args>(args)...); (direct
      // construction)
      // 2. new (&_buffer[...]) TestObject(std::move(temp_obj)); (move
      // construction) It also performs one move operation.
      REQUIRE(rb.try_emplace(1, "obj1"));
      REQUIRE(TestObject::constructions.load(std::memory_order_relaxed) ==
              2); // 1 for temp_obj + 1 for move-construction into buffer
      REQUIRE(TestObject::moves.load(std::memory_order_relaxed) ==
              1); // 1 for moving temp_obj into buffer
      REQUIRE(TestObject::copies.load(std::memory_order_relaxed) == 0);

      REQUIRE(rb.try_emplace(2, "obj2"));
      REQUIRE(TestObject::constructions.load(std::memory_order_relaxed) ==
              4); // Cumulative: 2 previous + 2 for this emplace
      REQUIRE(TestObject::moves.load(std::memory_order_relaxed) ==
              2); // Cumulative: 1 previous + 1 for this emplace
      REQUIRE(TestObject::copies.load(std::memory_order_relaxed) == 0);

      TestObject out_val;
      // Default construction of out_val increments constructions.
      REQUIRE(TestObject::constructions.load(std::memory_order_relaxed) ==
              5);                 // 4 from emplaces + 1 for out_val
      TestObject::reset_counts(); // Reset before pop

      REQUIRE(rb.try_pop(out_val)); // 1 move (from buffer to out_val), 1
                                    // destruction (in buffer)
      REQUIRE(out_val.id == 1);
      REQUIRE(out_val.data == "obj1");
      REQUIRE(TestObject::moves.load(std::memory_order_relaxed) == 1);
      REQUIRE(TestObject::destructions.load(std::memory_order_relaxed) == 1);
      // out_val was move-assigned, not constructed here. No new TestObject
      // constructions from pop.
      REQUIRE(TestObject::constructions.load(std::memory_order_relaxed) == 0);

      TestObject::reset_counts();
      REQUIRE(rb.try_pop(out_val)); // 1 move, 1 destruction
      REQUIRE(out_val.id == 2);
      REQUIRE(out_val.data == "obj2");
      REQUIRE(TestObject::moves.load(std::memory_order_relaxed) == 1);
      REQUIRE(TestObject::destructions.load(std::memory_order_relaxed) == 1);

      REQUIRE_FALSE(rb.try_pop(out_val));
    }
  } // Ring buffer goes out of scope
  // Any remaining objects in the buffer should be destructed by the buffer's
  // destructor. If the section above ran fully, constructions should equal
  // destructions. If objects were left, ~MpscRingBuffer would call destructors.
  // For this specific section, we pop everything, so TestObject::destructions
  // should account for the objects popped. The buffer itself is empty on
  // destruction.

  SECTION("Ensure objects are destructed when buffer is destroyed") {
    TestObject::reset_counts();
    {
      MpscRingBuffer<TestObject> rb_dtor(
          2); // Assuming this causes 1 construction based on line 201 behavior
              // If line 201 implies TestObject::constructions becomes 1,
              // then this should be reflected here too if consistent.
              // However, for clarity of dtor test, let's focus on emplace
              // counts.
      TestObject::reset_counts(); // Reset after buffer construction to isolate
                                  // emplace counts.

      rb_dtor.try_emplace(10, "dtor1");
      // First call: rb_dtor.try_emplace(10, "dtor1");
      // - temp_obj construction: constructions = 1
      // - move into buffer:      constructions = 2, moves = 1
      // - temp_obj destruction:  destructions = 1
      // Counts after 1st emplace: constructions = 2, moves = 1, destructions =
      // 1

      rb_dtor.try_emplace(20, "dtor2");
      // - temp_obj construction: constructions = 2 + 1 = 3
      // - move into buffer:      constructions = 3 + 1 = 4, moves = 1 + 1 = 2
      // - temp_obj destruction:  destructions = 1 + 1 = 2
      // Counts after 2nd emplace: constructions = 4, moves = 2, destructions =
      // 2

      REQUIRE(TestObject::constructions.load(std::memory_order_relaxed) == 4);

      // Emplace creates a temporary object, then moves it into the buffer. When
      // emplace returns, the temporary object is destructed, but the moved
      // object remains in the buffer. The destruction of the temporary object
      // is counted in one destruction per emplace.
      REQUIRE(TestObject::destructions.load(std::memory_order_relaxed) == 2);
    } // rb_dtor goes out of scope
    REQUIRE(TestObject::constructions.load(std::memory_order_relaxed) ==
            4); // Constructions remain the same
    REQUIRE(TestObject::destructions.load(std::memory_order_relaxed) ==
            4); // Two objects (each from 2 constructions) destructed by
                // ~MpscRingBuffer
  }
}

TEST_CASE(
    "MpscRingBuffer Stress Test (Single Producer, Single Consumer Sequential)",
    "[ring_buffer][stress]") {
  /**
   * @brief Performs a stress test with a large number of sequential emplace/pop
   * operations. Objective: Verify the ring buffer's correctness and stability
   * under a high volume of operations in a single-threaded producer/consumer
   * scenario. This is not a concurrency test but checks for issues that might
   * arise from many wraps or prolonged use.
   */
  const size_t capacity = 128; // Actual capacity will be 128
  const int iterations = 10000;
  MpscRingBuffer<long> rb(capacity);
  long produced_sum = 0;
  long consumed_sum = 0;
  int produced_count = 0;
  int consumed_count = 0;

  for (int i = 0; i < iterations; ++i) {
    // Try to produce a few items
    for (int j = 0; j < 5; ++j) { // Simulate bursty production
      long val_to_produce = static_cast<long>(i * 10 + j);
      if (rb.try_emplace(val_to_produce)) {
        produced_sum += val_to_produce;
        produced_count++;
      } else {
        break; // Buffer full for now
      }
    }

    // Try to consume a few items
    for (int k = 0; k < 3; ++k) { // Simulate bursty consumption
      long Rval;
      if (rb.try_pop(Rval)) {
        consumed_sum += Rval;
        consumed_count++;
      } else {
        break; // Buffer empty for now
      }
    }
  }

  // Consume any remaining items
  long Rval_final;
  while (rb.try_pop(Rval_final)) {
    consumed_sum += Rval_final;
    consumed_count++;
  }

  REQUIRE(produced_count > 0);
  REQUIRE(produced_count == consumed_count);
  REQUIRE(produced_sum == consumed_sum);
}

TEST_CASE(
    "MpscRingBuffer Multi-Producer, Single-Consumer with TestObject Lifecycle",
    "[ring_buffer][threaded][lifecycle]") {
  /**
   * @brief Objective: Verifies correct TestObject lifecycle management
   * (construction, destruction, moves, and absence of copies) in a concurrent
   * MPSC scenario. Setup: Multiple producers, one consumer. TestObjects with
   * unique IDs.
   * under concurrent producer operations.
   */
  TestObject::reset_counts(); // Reset once for the entire test case

  const size_t capacity = 64;
  const int num_producers =
      std::max(1u, std::thread::hardware_concurrency() / 2);
  const int items_per_producer =
      200; // Keep total items manageable for detailed checks
  const int total_items = num_producers * items_per_producer;

  { // Scope for MpscRingBuffer and TestObject out_val_consumer to ensure their
    // destruction
    MpscRingBuffer<TestObject> rb(capacity);
    std::vector<std::thread> producer_threads;
    std::atomic<int> items_successfully_produced_count{0};

    auto producer_task = [&](int producer_index) {
      for (int i = 0; i < items_per_producer; ++i) {
        int object_id = producer_index * items_per_producer + i; // Unique ID
        std::string data =
            "P" + std::to_string(producer_index) + "_Item" + std::to_string(i);

        // Loop until emplace succeeds, yielding to allow other threads
        // (especially the consumer if the buffer is full) to make progress.
        while (!rb.try_emplace(object_id, data)) {
          std::this_thread::yield();
        }
        items_successfully_produced_count.fetch_add(1,
                                                    std::memory_order_relaxed);
      }
    };

    for (int i = 0; i < num_producers; ++i) {
      producer_threads.emplace_back(producer_task, i);
    }

    std::set<int> consumed_ids;
    int consumed_count = 0;
    TestObject out_val_consumer; // Created once, reused for popping

    // Consumer loop: attempts to pop items. Yields if the buffer is
    // temporarily empty or if producers are still working, to avoid
    // busy-waiting while ensuring all items are eventually processed.
    while (consumed_count < total_items) {
      if (rb.try_pop(out_val_consumer)) {
        REQUIRE(consumed_ids.find(out_val_consumer.id) == consumed_ids.end());
        consumed_ids.insert(out_val_consumer.id);
        consumed_count++;
      } else {
        if (items_successfully_produced_count.load(std::memory_order_acquire) ==
                total_items &&
            consumed_count <
                total_items) { // All items produced, but not all consumed yet
          std::this_thread::yield();
        } else if (consumed_count < total_items) { // Producers still working or
                                                   // consumer catching up
          std::this_thread::yield();
        }
      }
    }

    for (auto &t : producer_threads) {
      t.join();
    }

    REQUIRE(consumed_count == total_items);
    REQUIRE(consumed_ids.size() == static_cast<size_t>(total_items));

    // For each item passing through:
    // - try_emplace: 1 direct construction (temp_obj), 1 move-construction
    // (into buffer). Total 2 constructions, 1 move.
    //                temp_obj is destructed (1 destruction).
    // - try_pop: 1 move (to out_val_consumer), 1 destruction (from buffer).
    // Net per item: 2 constructions, 2 moves, 2 destructions.
    // out_val_consumer: 1 construction (at its declaration).
    // At the end of this scope, out_val_consumer is destructed (1 destruction).
    // MpscRingBuffer is destructed (no items left, so no TestObject
    // destructions from it).

    // Expected moves: total_items * 2 (temp_obj to buffer, buffer to
    // out_val_consumer)
    REQUIRE(TestObject::moves.load(std::memory_order_relaxed) ==
            (long long)total_items * 2);
  } // MpscRingBuffer and out_val_consumer go out of scope here and are
    // destructed.

  // Global check after everything is destructed:
  REQUIRE(TestObject::constructions.load(std::memory_order_relaxed) ==
          TestObject::destructions.load(std::memory_order_relaxed));
  REQUIRE(TestObject::copies.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("MpscRingBuffer High Contention MPSC Test",
          "[ring_buffer][threaded][contention]") {
  /**
   * @brief Objective: Stress concurrent slot acquisition (_tail CAS by
   * producers) and data publishing/consumption (_ready_flags) with many
   * producers and a small buffer. Setup: High number of producers, very small
   * buffer capacity (8). Verifies: Correctness (all unique items consumed) and
   * liveness (test completes).
   */
  const size_t capacity = 8; // Very small capacity to force contention
  const int num_producers = std::max(4u, std::thread::hardware_concurrency() *
                                             2); // High number of producers
  const int items_per_producer = 1000;
  const int total_items = num_producers * items_per_producer;

  MpscRingBuffer<long> rb(capacity);
  std::vector<std::thread> producer_threads;
  std::atomic<int> items_successfully_produced_count{0};

  auto producer_task = [&](int producer_index) {
    for (int i = 0; i < items_per_producer; ++i) {
      long value = static_cast<long>(producer_index * items_per_producer +
                                     i); // Unique value
      while (!rb.try_emplace(value)) {
        std::this_thread::yield();
      }
      items_successfully_produced_count.fetch_add(1, std::memory_order_relaxed);
    }
  };

  for (int i = 0; i < num_producers; ++i) {
    producer_threads.emplace_back(producer_task, i);
  }

  std::vector<long> consumed_items_vec;
  consumed_items_vec.reserve(total_items);
  int consumed_count = 0;
  // Consumer loop similar to other threaded tests.

  while (consumed_count < total_items) {
    long val;
    if (rb.try_pop(val)) {
      consumed_items_vec.push_back(val);
      consumed_count++;
    } else {
      if (items_successfully_produced_count.load(std::memory_order_acquire) ==
              total_items &&
          consumed_count < total_items) {
        std::this_thread::yield();
      } else if (consumed_count < total_items) {
        std::this_thread::yield();
      }
    }
  }

  for (auto &t : producer_threads) {
    t.join();
  }

  REQUIRE(consumed_count == total_items);
  REQUIRE(consumed_items_vec.size() == static_cast<size_t>(total_items));

  // Verify all unique items are present
  // First, sort and check for adjacent duplicates to quickly find issues.
  // Then, use a set to confirm the total count of unique items matches the
  // expected total, ensuring no items were lost and all produced items were
  // unique.
  std::sort(consumed_items_vec.begin(), consumed_items_vec.end());
  for (int i = 0; i < total_items - 1; ++i) {
    REQUIRE(consumed_items_vec[i] <
            consumed_items_vec[i + 1]); // Checks for duplicates after sort
  }
  std::set<long> unique_consumed_items(consumed_items_vec.begin(),
                                       consumed_items_vec.end());
  REQUIRE(unique_consumed_items.size() == static_cast<size_t>(total_items));
}

TEST_CASE(
    "MpscRingBuffer Simplified Multi-Producer, Single-Consumer Sanity Test",
    "[ring_buffer][threaded][simple_sanity]") {
  /**
   * @brief Objective: A general sanity check for MPSC operation ensuring all
   * uniquely generated items from multiple producers are consumed exactly once
   * by a single consumer. Setup: Multiple producers generating unique `long`
   * values based on producer index and item index. Verifies: All produced items
   * are present in the consumed set, each exactly once.
   */
  const size_t capacity = 64; // A smaller capacity can increase contention
  const int num_producers = 3;
  const int items_per_producer =
      2000; // Reduced item count for faster test runs
  const int total_items = num_producers * items_per_producer;

  MpscRingBuffer<long> rb(capacity);
  std::vector<std::thread> producer_threads;
  std::atomic<int> items_produced_by_all_threads{0};

  // Producer task:
  // Each producer 'p' (0 to num_producers-1) produces 'items_per_producer'
  // items. Value = p * 1,000,000 + item_index_for_producer (0 to
  // items_per_producer-1) This ensures globally unique, identifiable values.
  auto producer_task = [&](int producer_index) {
    int items_produced_by_this_thread = 0;
    for (int i = 0; i < items_per_producer; ++i) {
      long value = static_cast<long>(producer_index * 1000000L + i);
      while (!rb.try_emplace(value)) {
        std::this_thread::yield(); // Spin if buffer is full
      }
      items_produced_by_this_thread++;
    }
    items_produced_by_all_threads.fetch_add(items_produced_by_this_thread,
                                            std::memory_order_relaxed);
  };

  // Launch producer threads
  for (int i = 0; i < num_producers; ++i) {
    producer_threads.emplace_back(producer_task, i);
  }

  std::vector<long> consumed_items_vec;
  consumed_items_vec.reserve(total_items);
  int consumed_count = 0;

  // Consumer logic (runs in the main test thread)
  while (consumed_count < total_items) {
    long val;
    if (rb.try_pop(val)) {
      consumed_items_vec.push_back(val);
      consumed_count++;
    } else {
      // If all items have been produced by threads but not all consumed yet by
      // this loop
      if (items_produced_by_all_threads.load(std::memory_order_acquire) ==
              total_items &&
          consumed_count < total_items) {
        // This condition means producers are done, but we haven't popped
        // everything. Keep trying to pop without excessive yielding if the
        // queue might just be momentarily empty. A short yield is still okay to
        // prevent a pure busy-wait if the pop is consistently failing.
        std::this_thread::yield();
      } else if (consumed_count < total_items) {
        // Not all items produced yet, or producers are done but we are still
        // catching up.
        std::this_thread::yield();
      }
      // If items_produced_by_all_threads < total_items, producers are
      // definitely still working.
    }
  }

  // Join producer threads
  for (auto &t : producer_threads) {
    t.join();
  }

  // --- Verification ---
  REQUIRE(consumed_count == total_items);
  REQUIRE(consumed_items_vec.size() == static_cast<size_t>(total_items));

  // 1. Check for duplicates and count occurrences of each item
  std::map<long, int> value_counts;
  for (long item : consumed_items_vec) {
    value_counts[item]++;
  }

  bool all_items_present_once = true;
  if (value_counts.size() != static_cast<size_t>(total_items)) {
    all_items_present_once = false;
    FAIL("Incorrect number of unique items received. Expected: "
         << total_items << ", Got: " << value_counts.size());
  }

  for (int p_idx = 0; p_idx < num_producers; ++p_idx) {
    for (int i = 0; i < items_per_producer; ++i) {
      long expected_value = static_cast<long>(p_idx * 1000000L + i);
      auto it = value_counts.find(expected_value);
      if (it == value_counts.end()) {
        all_items_present_once = false;
        FAIL("Expected item " << expected_value << " (from producer " << p_idx
                              << ") not found.");
      } else if (it->second != 1) {
        all_items_present_once = false;
        FAIL("Item " << expected_value << " (from producer " << p_idx
                     << ") found " << it->second << " times, expected once.");
      }
    }
  }
  REQUIRE(all_items_present_once);

  // 2. Alternative/additional check: Sort and compare with expected sorted list
  // This is somewhat redundant if the map check is thorough but can be a good
  // cross-check.
  std::sort(consumed_items_vec.begin(), consumed_items_vec.end());

  std::vector<long> expected_sorted_items;
  expected_sorted_items.reserve(total_items);
  for (int p_idx = 0; p_idx < num_producers; ++p_idx) {
    for (int i = 0; i < items_per_producer; ++i) {
      expected_sorted_items.push_back(static_cast<long>(p_idx * 1000000L + i));
    }
  }
  std::sort(expected_sorted_items.begin(), expected_sorted_items.end());

  REQUIRE(consumed_items_vec == expected_sorted_items);
}

TEST_CASE("MpscRingBuffer Producers Faster Than Consumer",
          "[ring_buffer][threaded][rate_mismatch]") {
  /**
   * @brief Objective: Test behavior when producers generate data faster than
   * the consumer can process, leading to the buffer frequently becoming full.
   * Setup: Producers emplace items rapidly. The consumer introduces a small,
   * periodic delay to simulate slower processing. Verifies: All items are
   * eventually consumed despite buffer-full conditions.
   */
  const size_t capacity = 16;
  const int num_producers = 2;
  const int items_per_producer = 1000;
  const int total_items = num_producers * items_per_producer;

  MpscRingBuffer<int> rb(capacity);
  std::vector<std::thread> producer_threads;
  std::atomic<int> items_successfully_produced_count{0};

  auto producer_task = [&](int producer_index) {
    for (int i = 0; i < items_per_producer; ++i) {
      int value = producer_index * items_per_producer + i;
      while (!rb.try_emplace(value)) { // Producers push as fast as possible
        std::this_thread::yield();
      }
      items_successfully_produced_count.fetch_add(1, std::memory_order_relaxed);
    }
  };

  for (int i = 0; i < num_producers; ++i) {
    producer_threads.emplace_back(producer_task, i);
  }

  std::set<int> consumed_values;
  int consumed_count = 0;
  while (consumed_count < total_items) {
    int val;
    if (rb.try_pop(val)) {
      consumed_values.insert(val);
      consumed_count++;
      // Simulate slower consumer by occasionally sleeping
      // Sleep less frequently than typical buffer fills to allow it to fill up.
      if (consumed_count % (capacity * 2 /*some factor > 1*/) ==
          0) { // Sleep less frequently than buffer fills
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    } else {
      std::this_thread::yield(); // Yield if buffer is empty
    }
  }

  for (auto &t : producer_threads) {
    t.join();
  }
  REQUIRE(consumed_count == total_items);
  REQUIRE(consumed_values.size() ==
          static_cast<size_t>(total_items)); // Ensure all unique items consumed
}

TEST_CASE("MpscRingBuffer Consumer Faster Than Producers",
          "[ring_buffer][threaded][rate_mismatch]") {
  /**
   * @brief Objective: Test behavior when the consumer attempts to pop items
   * faster than producers can supply, leading to the buffer frequently being
   * empty. Setup: A single producer adds items with an artificial delay. The
   * consumer attempts to pop rapidly. Verifies: `try_pop` correctly returns
   * `false` when empty, and all items are eventually consumed.
   */
  const size_t capacity = 16;
  const int num_producers = 1; // Single producer to make rate control easier
  const int items_to_produce = 500;

  MpscRingBuffer<int> rb(capacity);
  std::atomic<bool> producer_finished{false};

  std::thread producer_thread([&] {
    for (int i = 0; i < items_to_produce; ++i) {
      while (!rb.try_emplace(i)) {
        std::this_thread::yield();
      }
      std::this_thread::sleep_for(
          std::chrono::microseconds(50)); // Producer is slow
    }
    producer_finished.store(true, std::memory_order_release);
  });

  std::set<int> consumed_values;
  int val;
  int false_pop_count = 0;
  while (consumed_values.size() < static_cast<size_t>(items_to_produce)) {
    if (rb.try_pop(val)) {
      consumed_values.insert(val);
    } else {
      false_pop_count++;
      // Wrap the && expression in parentheses for Catch2
      REQUIRE_FALSE((producer_finished.load(std::memory_order_acquire) &&
                     consumed_values.size() ==
                         static_cast<size_t>(
                             items_to_produce))); // Should not be finished and
                                                  // fully consumed if pop fails
      std::this_thread::yield(); // Consumer is fast, might spin
    }
  }

  producer_thread.join();
  REQUIRE(consumed_values.size() == static_cast<size_t>(items_to_produce));
  REQUIRE(false_pop_count >
          0); // Ensure try_pop actually returned false sometimes
}

TEST_CASE("next_power_of_two utility function", "[ring_buffer][utility]") {
  /**
   * @brief Verifies the correctness of the `next_power_of_two` utility
   * function. Objective: Ensure the function behaves as expected for various
   * inputs, including edge cases.
   */
  SECTION("Small values") {
    REQUIRE(next_power_of_two(0) == 2);
    REQUIRE(next_power_of_two(1) == 2);
    REQUIRE(next_power_of_two(2) == 2);
    REQUIRE(next_power_of_two(3) == 4);
    REQUIRE(next_power_of_two(4) == 4);
  }

  SECTION("Powers of two (P >= 2)") {
    REQUIRE(next_power_of_two(2) == 2);
    REQUIRE(next_power_of_two(4) == 4);
    REQUIRE(next_power_of_two(8) == 8);
    REQUIRE(next_power_of_two(16) == 16);
    REQUIRE(next_power_of_two(1024) == 1024);
    REQUIRE(next_power_of_two(65536) == 65536); // 2^16
  }

  SECTION("Values between powers of two") {
    REQUIRE(next_power_of_two(5) == 8);
    REQUIRE(next_power_of_two(7) == 8);
    REQUIRE(next_power_of_two(9) == 16);
    REQUIRE(next_power_of_two(15) == 16);
    REQUIRE(next_power_of_two(1000) == 1024);
    REQUIRE(next_power_of_two(1025) == 2048);
    REQUIRE(next_power_of_two(65535) == 65536); // 2^16 - 1
  }

  SECTION("Values around large powers of two (P >= 2)") {
    const int num_size_t_bits = sizeof(size_t) * CHAR_BIT;

    // Iterate through exponents k for P = 2^k, from k_max down to 1.
    // k_max is (num_size_t_bits - 1) for the largest power of two.
    // k_min is 1 for P=2.
    for (int k = (num_size_t_bits - 1); k >= 1; --k) {
      size_t power_of_2 = static_cast<size_t>(1) << k;

      INFO("Testing P = 2^" << k << " = " << power_of_2);

      // Test P itself (P >= 2 here, so next_power_of_two(P) == P)
      REQUIRE(next_power_of_two(power_of_2) == power_of_2);

      // Test P-1. next_power_of_two(P-1) should be P.
      // This holds true for P>=2 because:
      // if P=2, P-1=1. next_power_of_two(1)=2.
      // if P>2, P-1 >= 2. The bit-twiddling algorithm correctly rounds up.
      REQUIRE(next_power_of_two(power_of_2 - 1) == power_of_2);

      // Test P+1. Result should be P*2 (the next higher power of two).
      if (k < num_size_t_bits - 1) {
        size_t next_higher_po2 = power_of_2 << 1;
        REQUIRE(next_power_of_two(power_of_2 + 1) == next_higher_po2);
      }
    }
  }
}
