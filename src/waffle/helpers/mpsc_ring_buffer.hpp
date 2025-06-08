#pragma once

#include <atomic>
#include <stdexcept>

// Helper to determine cache line size
#ifdef __cpp_lib_hardware_interference_size
#include <new>
constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
constexpr size_t CACHE_LINE_SIZE = 64;
#endif

// Helper to round up to the next power of two
inline size_t next_power_of_two(size_t n) {
  if (n == 0)
    return 2;
  size_t p = 1;
  while (p < n) {
    p <<= 1;
  }
  return p;
}

template <typename T> class MpscRingBuffer {
public:
  explicit MpscRingBuffer(size_t capacity) {
    if (capacity == 0) {
      throw std::invalid_argument("Capacity cannot be zero.");
    }
    _capacity = next_power_of_two(capacity);
    _mask = _capacity - 1;
    _buffer = new T[_capacity];
  }

  ~MpscRingBuffer() { delete[] _buffer; }

  MpscRingBuffer(const MpscRingBuffer &) = delete;
  MpscRingBuffer &operator=(const MpscRingBuffer &) = delete;

  template <typename... Args> bool try_emplace(Args &&...args) {
    size_t current_tail;
    while (true) {
      current_tail = _tail.load(std::memory_order_relaxed);
      const size_t current_head = _head.load(std::memory_order_acquire);

      if (current_tail - current_head >= _capacity) {
        return false;
      }

      if (_tail.compare_exchange_weak(current_tail, current_tail + 1,
                                      std::memory_order_release,
                                      std::memory_order_relaxed)) {
        break;
      }
    }
    new (&_buffer[current_tail & _mask]) T(std::forward<Args>(args)...);
    return true;
  }

  bool try_pop(T &out_value) {
    const size_t current_head = _head.load(std::memory_order_relaxed);
    if (current_head == _tail.load(std::memory_order_acquire)) {
      return false;
    }

    out_value = std::move(_buffer[current_head & _mask]);
    _buffer[current_head & _mask].~T();

    _head.store(current_head + 1, std::memory_order_release);
    return true;
  }

private:
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> _head{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> _tail{0};

  size_t _capacity;
  size_t _mask;
  T *_buffer;
};
