#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace Waffle {

// Forward declarations
class Tracer;
class Span;

/**
 * @brief A unified, strongly-typed identifier for all trace entities.
 */
struct Id {
  uint64_t value = 0;
  bool operator==(const Id &) const = default;
};
constexpr Id kInvalidId{0};

/**
 * @brief A tag struct used to establish an explicit causal link.
 */
struct CausedBy {
  explicit constexpr CausedBy(Id id) : value(id) {}
  Id value;
};

struct AttributeValue {
  enum class Type : uint8_t { BOOL, INT64, DOUBLE, STRING_ID };
  Type type;
  union {
    bool b;
    int64_t i64;
    double f64;
    uint64_t string_id;
  };
  AttributeValue() : type(Type::BOOL), b(false) {}
};

struct Attribute {
  uint64_t key_id;
  AttributeValue value;
  Attribute() : key_id(0), value() {}
  Attribute(uint64_t k, AttributeValue v) : key_id(k), value(std::move(v)) {}
};

constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t MAX_ATTRIBUTES_PER_TRACELET = 6;

constexpr uint64_t fnv1a_hash(const char *str, size_t n) {
  uint64_t hash = 0xcbf29ce484222325;
  for (size_t i = 0; i < n; ++i) {
    hash ^= static_cast<uint64_t>(str[i]);
    hash *= 0x100000001b3;
  }
  return hash;
}

struct StaticStringSource {
  uint64_t hash;
  const char *str;
  StaticStringSource(const char *s, size_t n)
      : hash(fnv1a_hash(s, n)), str(s) {}
};

} // namespace Waffle