#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <waffle/helpers/mpsc_ring_buffer.hpp>

namespace Waffle {

// --- Core Public API Types ---

/**
 * @brief A unified, strongly-typed identifier for all trace entities (traces,
 * spans, events).
 */
struct Id {
  uint64_t value = 0;
  bool operator==(const Id &) const = default;
};
constexpr Id kInvalidId{0};

/**
 * @brief A tag struct used to establish an explicit causal link between trace
 * entities. This is distinct from the implicit parent/child nesting
 * relationship. It should be used when you want to create an "arrow" from a
 * separate, preceding operation to the start of a new one. This is optional and
 * should be used only when needed.
 *
 * Example: WAFFLE_SPAN("process_data", CausedBy(data_loader_id), ...);
 */
struct CausedBy {
  explicit constexpr CausedBy(Id id) : value(id) {}
  Id value;
};

// --- Forward Declarations ---
class Tracer;
class Span;

// --- Structured Attribute Handling ---
struct AttributeValue {
  enum class Type : uint8_t { BOOL, INT64, DOUBLE, STRING_ID };
  Type type;
  union {
    bool b;
    int64_t i64;
    double f64;
    uint64_t string_id;
  };

  // Default constructor
  AttributeValue() : type(Type::BOOL), b(false) {}
};

struct Attribute {
  uint64_t key_id;
  AttributeValue value;

  // Default constructor
  Attribute() : key_id(0), value() {}

  // Constructor for direct initialization from key_id and AttributeValue
  Attribute(uint64_t k, AttributeValue v) : key_id(k), value(std::move(v)) {}
};

// --- Compile-Time Hashing & Static String Registration ---

/**
 * @brief A constexpr implementation of the FNV-1a hash algorithm.
 * This allows hashing string literals at compile time for maximum performance.
 */
constexpr uint64_t fnv1a_hash(const char *str, size_t n) {
  uint64_t hash = 0xcbf29ce484222325;
  for (size_t i = 0; i < n; ++i) {
    hash ^= static_cast<uint64_t>(str[i]);
    hash *= 0x100000001b3;
  }
  return hash;
}

/**
 * @brief A source location for a static string, containing the string and its
 * compile-time hash. A static instance of this struct is created for every
 * string literal used in tracing macros.
 */
struct StaticStringSource {
  uint64_t hash;
  const char *str;
  StaticStringSource(const char *s, size_t n)
      : hash(fnv1a_hash(s, n)), str(s) {}
};

// --- Core Data Structures ---
// The number of attributes is chosen carefully to make the total Tracelet size
// a multiple of the cache line size (64 bytes) to avoid performance degradation
// from a single Tracelet spanning multiple cache lines.
// With 6 attributes, the total size is exactly 192 bytes (3 * 64).
constexpr size_t MAX_ATTRIBUTES_PER_TRACELET = 6;
struct alignas(CACHE_LINE_SIZE) Tracelet {
  enum class RecordType : uint8_t { SPAN_START, SPAN_END, EVENT };

  uint64_t timestamp;
  Id trace_id;
  Id span_id;
  Id parent_span_id;
  Id cause_id;
  uint64_t name_string_hash;
  RecordType record_type;
  uint8_t num_attributes;
  uint8_t padding[6]; // Padding to align the attributes array

  Attribute attributes[MAX_ATTRIBUTES_PER_TRACELET];

  // Constructor for SPAN_START, EVENT (with attributes)
  Tracelet(uint64_t ts, Id t_id, Id s_id, Id p_span_id, Id c_id,
             uint64_t name_h, RecordType rtype,
             const std::array<Attribute, MAX_ATTRIBUTES_PER_TRACELET>& event_attrs_array,
             uint8_t actual_attr_count)
      : timestamp(ts), trace_id(t_id), span_id(s_id),
        parent_span_id(p_span_id), cause_id(c_id), name_string_hash(name_h),
        record_type(rtype), num_attributes(actual_attr_count) {
    std::fill_n(padding, sizeof(padding) / sizeof(padding[0]), 0);
    for (uint8_t i = 0; i < actual_attr_count; ++i) {
      attributes[i] = event_attrs_array[i];
    }
    // Default initialize remaining Attribute objects
    for (size_t i = actual_attr_count; i < MAX_ATTRIBUTES_PER_TRACELET; ++i) {
      attributes[i] = {};
    }
  }

  // Constructor for SPAN_END (no attributes)
  Tracelet(uint64_t ts, Id t_id, Id s_id, Id p_span_id, Id c_id,
             uint64_t name_h, RecordType rtype)
      : timestamp(ts), trace_id(t_id), span_id(s_id),
        parent_span_id(p_span_id), cause_id(c_id), name_string_hash(name_h),
        record_type(rtype), num_attributes(0) {
    std::fill_n(padding, sizeof(padding) / sizeof(padding[0]), 0);
    // Default initialize all Attribute objects
    for (size_t i = 0; i < MAX_ATTRIBUTES_PER_TRACELET; ++i) {
      attributes[i] = {};
    }
  }
  // Default constructor
  Tracelet() : Tracelet(0, {}, {}, {}, {}, 0, RecordType::EVENT) {}
};

// --- Span Object ---
class Span {
public:
  Span() = default;
  Span(Span &&other) noexcept;
  Span &operator=(Span &&other) noexcept;
  Span(const Span &) = delete;
  Span &operator=(const Span &) = delete;
  ~Span();

  void end();
  Id id() const { return _span_id; }

private:
  friend class Tracer;
  Span(Tracer *tracer, Id trace_id, Id span_id, Id parent_span_id);
  Tracer *_tracer = nullptr;
  Id _trace_id{kInvalidId};
  Id _span_id{kInvalidId};
  Id _parent_span_id{kInvalidId};
  bool _is_ended = false;
};

// --- Tracer & Global Provider ---
class Tracer {
public:
  Tracer();
  ~Tracer();

  template<typename... AttrArgs>
  Span start_span(const StaticStringSource &name, Id parent, Id cause, AttrArgs&&... attr_args);

  template<typename... AttrArgs>
  Span start_span(std::string_view name, Id parent, Id cause, AttrArgs&&... attr_args);

  void end_span(Id trace_id, Id span_id);

  // Note: string_view overload for create_event might be useful too.
  template<typename... AttrArgs>
  void create_event(const StaticStringSource &name, Id parent, Id cause, AttrArgs&&... attr_args);

  // void create_event(std::string_view name, Id parent, Id cause,
  //                   std::initializer_list<Attribute> attrs);

  uint64_t get_string_id(const StaticStringSource &s);
  uint64_t get_string_id(std::string_view s);

  void shutdown();

private:
  friend class Span;
  uint64_t get_timestamp();
  void register_static_string(uint64_t hash, const char *str);

  std::atomic<uint64_t> _next_id{1};
  std::unique_ptr<MpscRingBuffer<Tracelet>> _queue;
  std::thread _processing_thread;
  std::atomic<bool> _shutdown_flag{false};

  std::mutex _string_mutex;
  std::unordered_map<uint64_t, std::string> _id_to_string_map;
};

namespace detail {
inline std::unique_ptr<Tracer> g_tracer_instance;
}

void setup();
void shutdown();

namespace context {
Id get_current_span_id();
void set_current_span_id(Id id);
} // namespace context

// --- Ergonomic Attribute Creation Helpers ---
/**
 * @brief This namespace enables the clean `key = value` syntax for attributes.
 *
 * It works via a three-step process:
 * 1. `_w` literal: `"my_key"_w` calls `operator""_w`, returning a temporary
 * `AttrMaker` struct.
 * 2. `operator=`: `AttrMaker{...} = "value"` calls a custom `operator=`
 * overload.
 * 3. Attribute Creation: This `operator=` function interns the key/value
 * strings and packs them into the final `Attribute` struct.
 */
namespace literals {
struct AttrMaker {
  const char *key;

  // Define operator= as const member functions of AttrMaker
  // They don't modify AttrMaker itself, but use its 'key' to produce an
  // Attribute.
  inline Attribute operator=(bool val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::BOOL;
    attr_val.b = val;
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key), attr_val};
  }
  inline Attribute operator=(int val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::INT64;
    attr_val.i64 = static_cast<int64_t>(val);
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key), attr_val};
  }
  inline Attribute operator=(long long val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::INT64;
    attr_val.i64 = static_cast<int64_t>(val);
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key), attr_val};
  }
  inline Attribute operator=(double val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::DOUBLE;
    attr_val.f64 = val;
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key), attr_val};
  }
  inline Attribute operator=(const char *val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::STRING_ID;
    attr_val.string_id = Waffle::detail::g_tracer_instance->get_string_id(val);
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key), attr_val};
  }
  // Consider adding an overload for std::string_view for completeness
  inline Attribute operator=(std::string_view val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::STRING_ID;
    attr_val.string_id = Waffle::detail::g_tracer_instance->get_string_id(val);
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key), attr_val};
  }
};
inline AttrMaker operator"" _w(const char *str, size_t) { return {str}; }
} // namespace literals

// --- Argument Parsing and Macro Implementation ---
namespace detail {
// A helper struct to parse the optional CausedBy tag from variadic arguments.
struct ParsedArgs {
  Id cause{kInvalidId};
};

template <typename T, typename... Args>
ParsedArgs parse_args_impl(const T &first, const Args &...rest) {
  if constexpr (std::is_same_v<T, CausedBy>) {
    return {first.value};
  } else {
    return parse_args_impl(rest...);
  }
}

inline ParsedArgs parse_args_impl() { return {}; }
} // namespace detail

#define WAFFLE_SPAN(name, ...)                                                 \
  static const Waffle::StaticStringSource CONCAT(waffle_span_loc_, __LINE__)(  \
      name, sizeof(name) - 1);                                                 \
  auto CONCAT(waffle_span_, __LINE__) =                                        \
      Waffle::detail::g_tracer_instance -> start_span(                         \
          CONCAT(waffle_span_loc_, __LINE__),                                  \
          Waffle::context::get_current_span_id(),                              \
          Waffle::detail::parse_args_impl(__VA_ARGS__).cause, __VA_ARGS__)

#define WAFFLE_EVENT(name, ...)                                                \
  static const Waffle::StaticStringSource CONCAT(waffle_event_loc_, __LINE__)( \
      name, sizeof(name) - 1);                                                 \
  Waffle::detail::g_tracer_instance->create_event(                             \
      CONCAT(waffle_event_loc_, __LINE__),                                     \
      Waffle::context::get_current_span_id(),                                \
      Waffle::detail::parse_args_impl(__VA_ARGS__).cause, __VA_ARGS__)

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

} // namespace Waffle
