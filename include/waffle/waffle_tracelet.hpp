#pragma once

#include "waffle_common_types.hpp" // For Id, Attribute, MAX_ATTRIBUTES_PER_TRACELET, CACHE_LINE_SIZE
#include <algorithm> // For std::fill_n
#include <array>     // For std::array in Tracelet constructor parameter
#include <cstdint>   // For uint8_t etc.

namespace Waffle {

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

  Tracelet(uint64_t ts, Id t_id, Id s_id, Id p_span_id, Id c_id,
           uint64_t name_h, RecordType rtype,
           const std::array<Attribute, MAX_ATTRIBUTES_PER_TRACELET>
               &event_attrs_array,
           uint8_t actual_attr_count)
      : timestamp(ts), trace_id(t_id), span_id(s_id), parent_span_id(p_span_id),
        cause_id(c_id), name_string_hash(name_h), record_type(rtype),
        num_attributes(actual_attr_count) {
    std::fill_n(padding, sizeof(padding), uint8_t{0});
    for (uint8_t i = 0; i < actual_attr_count; ++i) {
      attributes[i] = event_attrs_array[i];
    }
    for (size_t i = actual_attr_count; i < MAX_ATTRIBUTES_PER_TRACELET; ++i) {
      attributes[i] = {};
    }
  }

  Tracelet(uint64_t ts, Id t_id, Id s_id, Id p_span_id, Id c_id,
           uint64_t name_h, RecordType rtype)
      : timestamp(ts), trace_id(t_id), span_id(s_id), parent_span_id(p_span_id),
        cause_id(c_id), name_string_hash(name_h), record_type(rtype),
        num_attributes(0) {
    std::fill_n(padding, sizeof(padding), uint8_t{0});
    for (size_t i = 0; i < MAX_ATTRIBUTES_PER_TRACELET; ++i) {
      attributes[i] = {};
    }
  }
  Tracelet() : Tracelet(0, {}, {}, {}, {}, 0, RecordType::EVENT) {}
};

} // namespace Waffle