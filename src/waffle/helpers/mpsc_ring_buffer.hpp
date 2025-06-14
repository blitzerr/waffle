#pragma once

/**
 * @file mpsc_ring_buffer.hpp
 * @brief A lock-free, bounded, multi-producer, single-consumer (MPSC) ring buffer.
 *
 * This MPSC ring buffer allows multiple producer threads to concurrently enqueue
 * items and a single consumer thread to dequeue items. It is designed for
 * high-performance inter-thread communication.
 *
 * Features:
 * - Lock-free: Operations do not involve traditional mutexes. `try_emplace` is
 *   lock-free due to its atomic compare-and-swap loop for slot acquisition.
 *   `try_pop` is also lock-free as its path is bounded and does not involve
 *   retrying its core logic due to contention (as there's only one consumer).
 * - Bounded: The buffer has a fixed capacity, determined at construction and
 *   rounded up to the next power of two for efficient indexing.
 * - FIFO: Items are consumed in the order they were successfully enqueued and marked ready.
 * - Cache-friendly: Head and tail pointers are aligned to cache line boundaries
 *   to mitigate false sharing.
 *
 * Implementation Details:
 * - Uses a contiguous array (`_buffer`) to store items.
 * - `_head`: An atomic counter indicating the next slot to be read by the consumer.
 *   Only modified by the consumer. Read by producers.
 * - `_tail`: An atomic counter indicating the next slot to be written by a producer.
 *   Modified by producers (atomically). Read by the consumer and producers.
 * - `_ready_flags`: An array of atomic booleans, one for each slot in `_buffer`.
 *   A flag `_ready_flags[i]` being `true` signifies that the item in `_buffer[i]`
 *   has been fully constructed and is ready for consumption. This is crucial for
 *   correctly synchronizing data visibility between producers and the consumer.
 * - `_mask`: Used for efficient index calculation (`index & _mask`) due to the
 *   power-of-two capacity.
 *
 * Synchronization and Memory Ordering:
 * 1. Producer Slot Claiming (`try_emplace`):
 *    - Producers contend to atomically increment `_tail` using
 *      `compare_exchange_weak(std::memory_order_relaxed, std::memory_order_relaxed)`.
 *      The `relaxed` ordering is sufficient as `_tail` only reserves a slot; data publication is separate.
 * 2. Producer Data Publication (`try_emplace`):
 *    - After constructing the item in the claimed buffer slot, the producer executes
 *      `_ready_flags[slot_index].store(true, std::memory_order_release)`.
 *    - This `release` ensures prior writes (item construction) are visible before the flag is set.
 * 3. Consumer Data Consumption (`try_pop`):
 *    - The consumer checks `_ready_flags[slot_index].load(std::memory_order_acquire)`.
 *    - This `acquire` synchronizes with the producer's `release` store on the flag. If true, the item is visible.
 *    - After consuming, `_ready_flags[slot_index].store(false, std::memory_order_relaxed)` resets the flag.
 * 4. Consumer Slot Freeing & Producer Space Check:
 *    - Consumer advances `_head` with `std::memory_order_release`. This publishes slot availability.
 *    - Producers read `_head` with `std::memory_order_acquire` to check for space, synchronizing with the consumer.
 */

#include <atomic>
#include <new> // For placement new, ::operator new, ::operator delete
#include <stdexcept>

// Helper to determine cache line size
#ifdef __cpp_lib_hardware_interference_size
#include <new>
constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
constexpr size_t CACHE_LINE_SIZE = 64;
#endif

// Helper to round up to the next power of two, ensuring a minimum of 2.
// This is specifically tailored for the MpscRingBuffer's capacity requirements.
inline size_t next_power_of_two(size_t n) {
  if (n <= 1) { // Handles n=0 and n=1
    return 2;   // MpscRingBuffer requires a minimum capacity of 2.
  }
  // For n >= 2:
  // Standard bit-twiddling algorithm to find the next power of two.
  // If n is already a power of two, it should return n.
  // Otherwise, it returns the smallest power of two greater than n.
  size_t v = n - 1;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  if constexpr (sizeof(size_t) > 4) { // For 64-bit size_t
    v |= v >> 32;
  }
  return v + 1; // v+1 correctly computes the next power of two for n >= 2.
}

template <typename T> class MpscRingBuffer {
public:
  explicit MpscRingBuffer(size_t capacity) {
    if (capacity == 0) {
      throw std::invalid_argument("Capacity cannot be zero.");
    }
    _capacity = next_power_of_two(capacity);
    _mask = _capacity - 1;
    // Allocate raw memory for the buffer and ready flags.
    _buffer = static_cast<T*>(::operator new(_capacity * sizeof(T)));
    // Allocate and initialize ready flags. std::atomic<bool> default constructs to false.
    _ready_flags = new std::atomic<bool>[_capacity]();
  }

  ~MpscRingBuffer() {
    // Manually destruct any remaining elements in the buffer
    // (i.e., those constructed but not yet popped).
    size_t current_head = _head.load(std::memory_order_relaxed);
    const size_t current_tail = _tail.load(std::memory_order_relaxed);

    for (size_t i = current_head; i != current_tail; ++i) {
      _buffer[i & _mask].~T();
    }

    // Deallocate the raw memory for buffer and flags.
    ::operator delete(_buffer);
    delete[] _ready_flags;
  }

  MpscRingBuffer(const MpscRingBuffer &) = delete;
  MpscRingBuffer &operator=(const MpscRingBuffer &) = delete;

  template <typename... Args> bool try_emplace(Args &&...args) {
    // Construct the object on the producer's stack before manipulating
    // shared state. If T's constructor throws, buffer remains consistent.
    T temp_obj(std::forward<Args>(args)...);

    size_t current_tail_ticket; // Candidate slot index.

    while (true) { // Loop for CAS-based slot acquisition.
      current_tail_ticket = _tail.load(
          std::memory_order_relaxed); // Atomically read current tail.
      const size_t current_head = _head.load(std::memory_order_acquire);

      // Buffer is full if the distance between tail and head reaches capacity.
      if (current_tail_ticket - current_head >= _capacity) {
        return false; // Buffer is full
      }

      // Attempt to claim the slot by advancing _tail.
      // Relaxed ordering is used as _tail only claims slot; _ready_flags publishes data.
      size_t expected_tail = current_tail_ticket;
      if (_tail.compare_exchange_weak(expected_tail, current_tail_ticket + 1,
                                      std::memory_order_relaxed, // success
                                      std::memory_order_relaxed  // failure
                                      )) {
        // Successfully claimed slot at 'current_tail_ticket'.
        // Construct the object in the buffer by moving from temp_obj.
        new (&_buffer[current_tail_ticket & _mask]) T(std::move(temp_obj));

        // Publish that the data in this slot is ready.
        // This release store synchronizes with the acquire load in try_pop.
        _ready_flags[current_tail_ticket & _mask].store(true, std::memory_order_release);
        return true;
      } else {
        // CAS failed: _tail was modified by another producer. Loop to retry.
      }
    }
  }

  bool try_pop(T &out_value) {
    const size_t current_head = _head.load(std::memory_order_relaxed);
    // Relaxed load of _tail for initial check; _ready_flag is the true gate.
    if (current_head == _tail.load(std::memory_order_relaxed)) {
      // No slots have been claimed by producers beyond what we've consumed.
      return false;
    }

    // A slot at current_head has been claimed by a producer.
    // Check if the data is actually ready using an acquire load on the flag.
    // This synchronizes with the producer's release store on this flag.
    if (_ready_flags[current_head & _mask].load(std::memory_order_acquire)) {
      // Data is ready.
      out_value = std::move(_buffer[current_head & _mask]);
      _buffer[current_head & _mask].~T();

      // Reset the ready flag for this slot. Relaxed is fine as this doesn't publish
      // new data; the subsequent _head.store(release) publishes slot availability.
      _ready_flags[current_head & _mask].store(false, std::memory_order_relaxed);

      // Advance _head, publishing that this slot (and its flag) is now fully processed and free.
      _head.store(current_head + 1, std::memory_order_release);
      return true;
    }
    // Slot is claimed (current_head != _tail) but data not yet ready (flag is false).
    return false;
  }

private:
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> _head{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> _tail{0};

  size_t _capacity;
  size_t _mask;
  T *_buffer;
  std::atomic<bool>* _ready_flags; // One flag per slot in _buffer
};
