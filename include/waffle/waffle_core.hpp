#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "waffle_core_detail.hpp" // Provides detail::extract_attributes, detail::parse_args_impl. Depends on types from waffle_common_types.hpp.
#include <waffle/helpers/mpsc_ring_buffer.hpp>

namespace Waffle {

// --- Forward Declarations ---
// Forward declarations for Tracer and Span are now provided by
// waffle_common_types.hpp class Tracer; // No longer needed here class Span; //
// No longer needed here

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
           const std::array<Attribute, MAX_ATTRIBUTES_PER_TRACELET>
               &event_attrs_array,
           uint8_t actual_attr_count)
      : timestamp(ts), trace_id(t_id), span_id(s_id), parent_span_id(p_span_id),
        cause_id(c_id), name_string_hash(name_h), record_type(rtype),
        num_attributes(actual_attr_count) {
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
      : timestamp(ts), trace_id(t_id), span_id(s_id), parent_span_id(p_span_id),
        cause_id(c_id), name_string_hash(name_h), record_type(rtype),
        num_attributes(0) {
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

  void end_span(Id trace_id, Id span_id);

  // void create_event(std::string_view name, Id parent, Id cause,
  //                   std::initializer_list<Attribute> attrs);

  uint64_t get_string_id(const StaticStringSource &s);
  uint64_t get_string_id(std::string_view s);

  void shutdown();

  // --- Template method definitions moved here from .cpp file ---

  template <typename... AttrArgs>
  Span start_span(const StaticStringSource &name, Id parent_span_id,
                  Id cause_id, AttrArgs &&...attr_args) {
    uint64_t new_id_val = _next_id.fetch_add(1, std::memory_order_relaxed);
    Id new_span_id = {new_id_val};

    // TODO: Critical: Trace ID Propagation.
    // If parent_span_id is valid, trace_id should be the parent's trace_id.
    // If parent_span_id is kInvalidId, this is a root span, and its new_span_id
    // becomes the trace_id. The current logic `(parent_span_id.value !=
    // kInvalidId.value) ? Id{parent_span_id.value} : new_span_id` incorrectly
    // uses parent_span_id as trace_id for child spans. This requires a robust
    // way to access the parent's trace_id, possibly by enhancing
    // Waffle::context to store and retrieve the current trace_id alongside the
    // span_id.
    Id trace_id_for_new_span = (parent_span_id.value != kInvalidId.value)
                                   ? Id{parent_span_id.value}
                                   : new_span_id; // Needs fix

    auto [attributes_array, actual_attr_count] =
        Waffle::detail::extract_attributes(
            std::forward<AttrArgs>(attr_args)...);

    if (!_shutdown_flag) {
      register_static_string(name.hash, name.str); // Ensure string is known
      _queue->try_emplace(get_timestamp(), trace_id_for_new_span, new_span_id,
                          parent_span_id, cause_id, name.hash,
                          Tracelet::RecordType::SPAN_START, attributes_array,
                          actual_attr_count);
    }
    return Span(this, trace_id_for_new_span, new_span_id, parent_span_id);
  }

  template <typename... AttrArgs>
  Span start_span(std::string_view name, Id parent_span_id, Id cause_id,
                  AttrArgs &&...attr_args) {
    uint64_t new_id_val = _next_id.fetch_add(1, std::memory_order_relaxed);
    Id new_span_id = {new_id_val};
    // TODO: Same critical trace_id propagation issue as above.
    Id trace_id_for_new_span = (parent_span_id.value != kInvalidId.value)
                                   ? Id{parent_span_id.value}
                                   : new_span_id; // Needs fix

    auto [attributes_array, actual_attr_count] =
        Waffle::detail::extract_attributes(
            std::forward<AttrArgs>(attr_args)...);

    uint64_t name_hash = get_string_id(name); // Interns the string_view
    if (!_shutdown_flag) {
      _queue->try_emplace(get_timestamp(), trace_id_for_new_span, new_span_id,
                          parent_span_id, cause_id, name_hash,
                          Tracelet::RecordType::SPAN_START, attributes_array,
                          actual_attr_count);
    }
    return Span(this, trace_id_for_new_span, new_span_id, parent_span_id);
  }

  template <typename... AttrArgs>
  void create_event(const StaticStringSource &name, Id parent_span_id,
                    Id cause_id, AttrArgs &&...attr_args) {
    // TODO: Trace ID for events should be derived from the parent_span_id's
    // trace. Similar to start_span, this needs a way to get the parent's
    // trace_id. If parent_span_id is kInvalidId, it's an orphaned event,
    // trace_id might be new or invalid.
    Id trace_id_for_event = (parent_span_id.value != kInvalidId.value)
                                ? Id{parent_span_id.value}
                                : kInvalidId; // Needs fix
    Id event_id = {_next_id.fetch_add(
        1)}; // Events could have their own IDs or use parent_span_id
             // contextually. Using parent_span_id as the "span_id" for the
             // Tracelet.
    auto [attributes_array, actual_attr_count] =
        Waffle::detail::extract_attributes(
            std::forward<AttrArgs>(attr_args)...);
    if (!_shutdown_flag) {
      register_static_string(name.hash, name.str); // Ensure string is known
      _queue->try_emplace(get_timestamp(), trace_id_for_event, parent_span_id,
                          parent_span_id, cause_id, name.hash,
                          Tracelet::RecordType::EVENT, attributes_array,
                          actual_attr_count);
    }
  }

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
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key),
                     attr_val};
  }
  inline Attribute operator=(int val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::INT64;
    attr_val.i64 = static_cast<int64_t>(val);
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key),
                     attr_val};
  }
  inline Attribute operator=(long long val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::INT64;
    attr_val.i64 = static_cast<int64_t>(val);
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key),
                     attr_val};
  }
  inline Attribute operator=(double val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::DOUBLE;
    attr_val.f64 = val;
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key),
                     attr_val};
  }
  inline Attribute operator=(const char *val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::STRING_ID;
    attr_val.string_id = Waffle::detail::g_tracer_instance->get_string_id(val);
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key),
                     attr_val};
  }
  // Consider adding an overload for std::string_view for completeness
  inline Attribute operator=(std::string_view val) const {
    AttributeValue attr_val;
    attr_val.type = AttributeValue::Type::STRING_ID;
    attr_val.string_id = Waffle::detail::g_tracer_instance->get_string_id(val);
    return Attribute{Waffle::detail::g_tracer_instance->get_string_id(key),
                     attr_val};
  }
};
inline AttrMaker operator"" _w(const char *str, size_t) { return {str}; }
} // namespace literals
} // namespace Waffle
