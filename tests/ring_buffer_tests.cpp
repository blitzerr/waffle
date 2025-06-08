
#include <catch2/catch_all.hpp>
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

    SECTION("Emplace and Pop TestObject") {
      TestObject::reset_counts();
      REQUIRE(rb.try_emplace(1, "obj1")); // 1 construction (in-place)
      REQUIRE(TestObject::constructions == 1);
      REQUIRE(TestObject::moves == 0); // No moves during emplace itself

      REQUIRE(rb.try_emplace(2, "obj2")); // 1 construction
      REQUIRE(TestObject::constructions == 2);

      TestObject out_val;
      TestObject::reset_counts(); // Reset before pop

      REQUIRE(rb.try_pop(out_val)); // 1 move (from buffer to out_val), 1
                                    // destruction (in buffer)
      REQUIRE(out_val.id == 1);
      REQUIRE(out_val.data == "obj1");
      REQUIRE(TestObject::moves == 1);
      REQUIRE(TestObject::destructions == 1);
      REQUIRE(TestObject::constructions == 0); // No new constructions

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
      MpscRingBuffer<TestObject> rb_dtor(2);
      rb_dtor.try_emplace(10, "dtor1");
      rb_dtor.try_emplace(20, "dtor2");
      REQUIRE(TestObject::constructions == 2);
      REQUIRE(TestObject::destructions == 0);
    } // rb_dtor goes out of scope
    REQUIRE(TestObject::constructions == 2);
    REQUIRE(TestObject::destructions ==
            2); // Two objects destructed by ~MpscRingBuffer
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

TEST_CASE("MpscRingBuffer Multi-Producer, Single-Consumer Threaded Test",
          "[ring_buffer][threaded]") {
  const size_t capacity = 256; // Ring buffer capacity
  const int num_producers = 4;
  const int items_per_producer = 5000;
  const int total_items = num_producers * items_per_producer;

  MpscRingBuffer<long> rb(capacity);
  std::vector<std::thread> producer_threads;
  std::atomic<long> total_produced_sum{0};
  std::atomic<int> total_produced_count{0};

  // Producer lambda
  auto producer_task = [&](int producer_id) {
    long local_sum = 0;
    int local_count = 0;
    for (int i = 0; i < items_per_producer; ++i) {
      // Create a unique value for each item from each producer
      long value = static_cast<long>(producer_id * items_per_producer + i);
      while (!rb.try_emplace(value)) {
        // Spin/yield if buffer is full - in a real app, might use a condition
        // variable or backoff
        std::this_thread::yield();
      }
      local_sum += value;
      local_count++;
    }
    total_produced_sum += local_sum;
    total_produced_count += local_count;
  };

  // Launch producer threads
  for (int i = 0; i < num_producers; ++i) {
    producer_threads.emplace_back(producer_task,
                                  i + 1); // producer_id starting from 1
  }

  long consumed_sum = 0;
  int consumed_count = 0;
  std::vector<bool> consumed_flags(
      total_items + num_producers,
      false); // A bit oversized to be safe with 1-based producer_id *
              // items_per_producer

  // Consumer logic (runs in the main test thread)
  while (consumed_count < total_items) {
    long val;
    if (rb.try_pop(val)) {
      consumed_sum += val;
      consumed_count++;
      // Check for duplicates or unexpected values
      // Values are expected to be unique in the range [items_per_producer,
      // (num_producers+1)*items_per_producer -1] if producer_id starts from 1.
      // For producer_id * items_per_producer + i, where producer_id is 1 to N,
      // i is 0 to M-1 Smallest value: 1*items_per_producer + 0 =
      // items_per_producer Largest value: num_producers*items_per_producer +
      // (items_per_producer-1)
      REQUIRE_FALSE(consumed_flags[val]); // Ensure no duplicates
      consumed_flags[val] = true;
    } else {
      // Yield if buffer is empty and not all items are consumed yet
      std::this_thread::yield();
    }
  }

  // Join producer threads
  for (auto &t : producer_threads) {
    t.join();
  }

  REQUIRE(total_produced_count == total_items);
  REQUIRE(consumed_count == total_items);
  REQUIRE(total_produced_sum == consumed_sum);
}
