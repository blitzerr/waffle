
#include <catch2/catch_all.hpp>
#include <climits> // For CHAR_BIT
#include <string>
#include <thread>
#include <vector>
#include <waffle/helpers/mpsc_ring_buffer.hpp>

// Helper struct to track constructions, destructions, moves, copies
struct TestObject {
  int id;
  std::string data;
  static int constructions;
  static int destructions;
  static int moves;
  static int copies;

  TestObject(int i = 0, std::string d = "default") : id(i), data(std::move(d)) {
    constructions++;
  }

  ~TestObject() { destructions++; }

  TestObject(const TestObject &other) : id(other.id), data(other.data) {
    copies++;
    constructions++; // Copy construction is still a construction
  }

  TestObject &operator=(const TestObject &other) {
    if (this != &other) {
      id = other.id;
      data = other.data;
      copies++;
    }
    return *this;
  }

  TestObject(TestObject &&other) noexcept
      : id(other.id), data(std::move(other.data)) {
    other.id = -1; // Mark as moved-from
    moves++;
    constructions++; // Move construction is still a construction
  }

  TestObject &operator=(TestObject &&other) noexcept {
    if (this != &other) {
      id = other.id;
      data = std::move(other.data);
      other.id = -1; // Mark as moved-from
      moves++;
    }
    return *this;
  }

  static void reset_counts() {
    constructions = 0;
    destructions = 0;
    moves = 0;
    copies = 0;
  }

  bool operator==(const TestObject &other) const {
    return id == other.id && data == other.data;
  }
};

int TestObject::constructions = 0;
int TestObject::destructions = 0;
int TestObject::moves = 0;
int TestObject::copies = 0;

TEST_CASE("MpscRingBuffer Construction and Capacity", "[ring_buffer]") {
  SECTION("Zero capacity throws") {
    REQUIRE_THROWS_AS(MpscRingBuffer<int>(0), std::invalid_argument);
  }

  SECTION("Capacity is rounded to next power of two") {
    // Internal capacity is not directly exposed, but we can infer it by filling
    MpscRingBuffer<int> rb3(3); // next_power_of_two(3) is 4
    for (int i = 0; i < 4; ++i)
      REQUIRE(rb3.try_emplace(i));
    REQUIRE_FALSE(rb3.try_emplace(4));

    MpscRingBuffer<int> rb4(4); // next_power_of_two(4) is 4
    for (int i = 0; i < 4; ++i)
      REQUIRE(rb4.try_emplace(i));
    REQUIRE_FALSE(rb4.try_emplace(4));

    MpscRingBuffer<int> rb1(
        1); // next_power_of_two(1) is 2 (as per next_power_of_two impl for n=1
            // -> p=1, then p <<=1 -> p=2)
    REQUIRE(rb1.try_emplace(0));
    REQUIRE(rb1.try_emplace(1));
    REQUIRE_FALSE(rb1.try_emplace(2));
  }
}

TEST_CASE("MpscRingBuffer Basic Operations", "[ring_buffer]") {
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
      REQUIRE(TestObject::constructions ==
              2); // 1 for temp_obj + 1 for move-construction into buffer
      REQUIRE(TestObject::moves == 1); // 1 for moving temp_obj into buffer
      REQUIRE(TestObject::copies == 0);

      REQUIRE(rb.try_emplace(2, "obj2"));
      REQUIRE(TestObject::constructions ==
              4); // Cumulative: 2 previous + 2 for this emplace
      REQUIRE(TestObject::moves ==
              2); // Cumulative: 1 previous + 1 for this emplace
      REQUIRE(TestObject::copies == 0);

      TestObject out_val;
      // Default construction of out_val increments constructions.
      REQUIRE(TestObject::constructions ==
              5);                 // 4 from emplaces + 1 for out_val
      TestObject::reset_counts(); // Reset before pop

      REQUIRE(rb.try_pop(out_val)); // 1 move (from buffer to out_val), 1
                                    // destruction (in buffer)
      REQUIRE(out_val.id == 1);
      REQUIRE(out_val.data == "obj1");
      REQUIRE(TestObject::moves == 1);
      REQUIRE(TestObject::destructions == 1);
      // out_val was move-assigned, not constructed here. No new TestObject
      // constructions from pop.
      REQUIRE(TestObject::constructions == 0);

      TestObject::reset_counts();
      REQUIRE(rb.try_pop(out_val)); // 1 move, 1 destruction
      REQUIRE(out_val.id == 2);
      REQUIRE(out_val.data == "obj2");
      REQUIRE(TestObject::moves == 1);
      REQUIRE(TestObject::destructions == 1);

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

      REQUIRE(TestObject::constructions == 4);

      // Emplace creates a temporary object, then moves it into the buffer. When
      // emplace returns, the temporary object is destructed, but the moved
      // object remains in the buffer. The destruction of the temporary object
      // is counted in one destruction per emplace.
      REQUIRE(TestObject::destructions == 2);
    } // rb_dtor goes out of scope
    REQUIRE(TestObject::constructions == 4); // Constructions remain the same
    REQUIRE(TestObject::destructions ==
            4); // Two objects (each from 2 constructions) destructed by
                // ~MpscRingBuffer
  }
}

TEST_CASE(
    "MpscRingBuffer Stress Test (Single Producer, Single Consumer Sequential)",
    "[ring_buffer][stress]") {
  const size_t capacity = 128; // Actual capacity will be 128
  const int iterations = 10000;
  MpscRingBuffer<long> rb(capacity);
  long produced_sum = 0;
  long consumed_sum = 0;
  int produced_count = 0;
  int consumed_count = 0;

  for (int i = 0; i < iterations; ++i) {
    // Try to produce a few items
    for (int j = 0; j < 5; ++j) {
      long val_to_produce = static_cast<long>(i * 10 + j);
      if (rb.try_emplace(val_to_produce)) {
        produced_sum += val_to_produce;
        produced_count++;
      } else {
        break; // Buffer full for now
      }
    }

    // Try to consume a few items
    for (int k = 0; k < 3; ++k) {
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
    "MpscRingBuffer Simplified Multi-Producer, Single-Consumer Sanity Test",
    "[ring_buffer][threaded][simple_sanity]") {
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

TEST_CASE("next_power_of_two utility function", "[ring_buffer][utility]") {
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
    for (int k = num_size_t_bits - 1; k >= 1; --k) {
      // Ensure k is a valid exponent index, though k>=1 should cover this.
      if (k < 0 || static_cast<unsigned int>(k) >=
                       static_cast<unsigned int>(num_size_t_bits)) {
        // This case should ideally not be hit if num_size_t_bits is reasonable
        // (e.g. >=1) and k starts from num_size_t_bits - 1.
        continue;
      }
      size_t power_of_2 = static_cast<size_t>(1) << k;

      INFO("Testing P = 2^" << k << " = " << power_of_2);

      // Test P itself (P >= 2 here, so next_power_of_two(P) == P)
      REQUIRE(next_power_of_two(power_of_2) == power_of_2);

      // Test P-1. next_power_of_two(P-1) should be P.
      // This holds true for P>=2 because:
      // if P=2, P-1=1. next_power_of_two(1)=2. Correct.
      // if P>2, P-1 >= 2. The loop `while(p < n)` will make p equal to P.
      REQUIRE(next_power_of_two(power_of_2 - 1) == power_of_2);

      // Test P+1. Result should be P*2 (the next higher power of two).
      // This test is only safe if P is not the largest_po2 (i.e., k <
      // num_size_t_bits - 1). If P is the largest_po2, P+1 would cause
      // next_power_of_two to loop indefinitely.
      if (k < num_size_t_bits - 1) {
        size_t next_higher_po2 = power_of_2 << 1;
        REQUIRE(next_power_of_two(power_of_2 + 1) == next_higher_po2);
      }
    }
  }
}
