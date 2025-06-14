#include "waffle/waffle_core.hpp"
#include <cstring>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

namespace Waffle {

// --- Span Implementation ---
Span::Span(Tracer *tracer, Id trace_id, Id span_id, Id parent_span_id)
    : _tracer(tracer), _trace_id(trace_id), _span_id(span_id),
      _parent_span_id(parent_span_id) {
  context::set_current_span_id(_span_id);
}

Span::Span(Span &&other) noexcept
    : _tracer(other._tracer), _trace_id(other._trace_id),
      _span_id(other._span_id), _parent_span_id(other._parent_span_id),
      _is_ended(other._is_ended) {
  other._tracer = nullptr;
  other._is_ended = true;
}

Span &Span::operator=(Span &&other) noexcept {
  if (this != &other) {
    if (!_is_ended && _tracer) {
      end();
    }
    _tracer = other._tracer;
    _trace_id = other._trace_id;
    _span_id = other._span_id;
    _parent_span_id = other._parent_span_id;
    _is_ended = other._is_ended;
    other._tracer = nullptr;
    other._is_ended = true;
  }
  return *this;
}

Span::~Span() {
  if (!_is_ended && _tracer) {
    end();
  }
}

void Span::end() {
  if (_is_ended || !_tracer)
    return;
  _tracer->end_span(_trace_id, _span_id);
  _is_ended = true;
  context::set_current_span_id(_parent_span_id);
}

// --- Processor Thread Helpers ---
struct ReadableSpanData {
  uint64_t name_hash;
  Id parent_id;
  Id cause_id;
  std::vector<Attribute> attributes;
};

void print_attribute(const std::string &key, const AttributeValue &val,
                     const std::unordered_map<uint64_t, std::string> &id_map) {
  std::cout << key << ": ";
  switch (val.type) {
  case AttributeValue::Type::BOOL:
    std::cout << (val.b ? "true" : "false");
    break;
  case AttributeValue::Type::INT64:
    std::cout << val.i64;
    break;
  case AttributeValue::Type::DOUBLE:
    std::cout << val.f64;
    break;
  case AttributeValue::Type::STRING_ID:
    std::cout << "'" << id_map.at(val.string_id) << "'";
    break;
  }
}

// --- Tracer Implementation ---
Tracer::Tracer() {
  _id_to_string_map[0] = ""; // ID 0 is the empty string
  _queue = std::make_unique<MpscRingBuffer<Tracelet>>(8192);

  _processing_thread = std::thread([this]() {
    std::map<uint64_t, ReadableSpanData> active_spans;
    Tracelet tracelet;
    while (!_shutdown_flag.load(std::memory_order_acquire)) {
      if (_queue->try_pop(tracelet)) {
        auto get_name = [&](uint64_t hash) {
          return _id_to_string_map.count(hash) ? _id_to_string_map.at(hash)
                                               : "???";
        };

        switch (tracelet.record_type) {
        case Tracelet::RecordType::SPAN_START: {
          ReadableSpanData data;
          data.name_hash = tracelet.name_string_hash;
          data.parent_id = tracelet.parent_span_id;
          data.cause_id = tracelet.cause_id;
          data.attributes.assign(tracelet.attributes,
                                 tracelet.attributes + tracelet.num_attributes);
          active_spans[tracelet.span_id.value] = data;
          break;
        }
        case Tracelet::RecordType::SPAN_END: {
          active_spans.erase(tracelet.span_id.value);
          break;
        }
        case Tracelet::RecordType::EVENT: {
          std::cout << "\n[Processor] EVENT '"
                    << get_name(tracelet.name_string_hash) << "'\n";

          // --- Implicit Causality Tracking Logic ---
          Id effective_cause_id = tracelet.cause_id;
          bool is_implicit_cause = false;
          if (effective_cause_id.value == kInvalidId.value) {
            // No explicit cause, so search up the parent chain.
            Id current_id = tracelet.parent_span_id;
            while (current_id.value != kInvalidId.value &&
                   active_spans.count(current_id.value)) {
              const auto &parent_span_data = active_spans.at(current_id.value);
              if (parent_span_data.cause_id.value != kInvalidId.value) {
                effective_cause_id = parent_span_data.cause_id;
                is_implicit_cause = true;
                break; // Found the first ancestor with a cause
              }
              current_id = parent_span_data.parent_id; // Go up one level
            }
          }

          std::cout << "  { Causal Link: " << effective_cause_id.value
                    << (is_implicit_cause ? " (Implicit)" : " (Explicit)")
                    << ",\n";

          std::cout << "    Event Attributes: { ";
          for (uint8_t i = 0; i < tracelet.num_attributes; ++i) {
            print_attribute(get_name(tracelet.attributes[i].key_id),
                            tracelet.attributes[i].value, _id_to_string_map);
            if (i < tracelet.num_attributes - 1)
              std::cout << ", ";
          }
          std::cout << " },\n";

          std::cout << "    Span Context: {\n";
          Id current_span_id = tracelet.parent_span_id;
          while (current_span_id.value != kInvalidId.value &&
                 active_spans.count(current_span_id.value)) {
            const auto &span_data = active_spans.at(current_span_id.value);
            std::cout << "      '" << get_name(span_data.name_hash) << "': { ";
            for (size_t i = 0; i < span_data.attributes.size(); ++i) {
              print_attribute(get_name(span_data.attributes[i].key_id),
                              span_data.attributes[i].value, _id_to_string_map);
              if (i < span_data.attributes.size() - 1)
                std::cout << ", ";
            }
            std::cout << " },\n";
            current_span_id = span_data.parent_id;
          }
          std::cout << "    }\n  }\n";
          break;
        }
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  });
}

Tracer::~Tracer() {
  if (!_shutdown_flag)
    shutdown();
}
void Tracer::shutdown() {
  _shutdown_flag.store(true, std::memory_order_release);
  if (_processing_thread.joinable())
    _processing_thread.join();
}

uint64_t Tracer::get_timestamp() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void Tracer::register_static_string(uint64_t hash, const char *str) {
  std::lock_guard<std::mutex> lock(_string_mutex);
  _id_to_string_map.emplace(hash, str);
}

uint64_t Tracer::get_string_id(const StaticStringSource &s) {
  register_static_string(s.hash, s.str);
  return s.hash;
}

uint64_t Tracer::get_string_id(std::string_view s) {
  uint64_t hash = fnv1a_hash(s.data(), s.size());
  std::lock_guard<std::mutex> lock(_string_mutex);
  _id_to_string_map.emplace(hash, s);
  return hash;
}

void Tracer::end_span(Id trace_id, Id span_id) {
  // For SPAN_END, parent_span_id in Tracelet context usually refers to the span
  // that just ended, and its "parent" for context restoration is its original
  // parent. However, the Tracelet structure has parent_span_id. For an END
  // record, this might be kInvalidId or the span_id itself. The current call
  // passes kInvalidId. The name_string_hash is 0 as SPAN_END doesn't have a
  // name in the same way.
  _queue->try_emplace(get_timestamp(), trace_id, span_id, kInvalidId,
                      kInvalidId, 0, Tracelet::RecordType::SPAN_END);
}

// --- Global Setup & Context ---
void setup() {
  if (!detail::g_tracer_instance)
    detail::g_tracer_instance = std::make_unique<Tracer>();
}
void shutdown() {
  if (detail::g_tracer_instance) {
    detail::g_tracer_instance->shutdown();
    detail::g_tracer_instance.reset();
  }
}
namespace context {
thread_local Id g_current_span_id = {kInvalidId};
Id get_current_span_id() { return g_current_span_id; }
void set_current_span_id(Id id) { g_current_span_id = id; }
} // namespace context
} // namespace Waffle
