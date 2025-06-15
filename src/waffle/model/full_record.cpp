#include "waffle/model/full_record.hpp"

std::optional<Waffle::model::FullRecord> Waffle::model::tracelet_to_full_record(
    const Tracelet &tracelet,
    const std::unordered_map<uint64_t, std::string> &id_to_string_map) {

  auto get_name = [&](uint64_t hash) {
    return id_to_string_map.count(hash) ? id_to_string_map.at(hash) : "???";
  };

  FullRecord record;
  record.name = get_name(tracelet.name_string_hash);
  record.rec_ty = tracelet.record_type;
  record.trace_id = tracelet.trace_id;
  record.span_id = tracelet.span_id;
  record.parent_id = tracelet.parent_span_id;
  record.cause_id = tracelet.cause_id;

  // use the tracelet attributes iteraror to populate the data map. Use a
  // function programming style.
  std::for_each(tracelet.attributes_begin(), tracelet.attributes_end(),
                [&](const Attribute &attr) {
                  auto key_str = get_name(
                      attr.key_id); // Renamed 'key' to 'key_str' for clarity
                  switch (attr.value.type) {
                  case AttributeValue::Type::BOOL:
                    record.data[key_str] = attr.value.b;
                    break;
                  case AttributeValue::Type::INT64:
                    record.data[key_str] = attr.value.i64;
                    break;
                  case AttributeValue::Type::DOUBLE:
                    record.data[key_str] = attr.value.f64;
                    break;
                  case AttributeValue::Type::STRING_ID:
                    // Resolve the string_id to a std::string using get_name
                    record.data[key_str] = get_name(attr.value.string_id);
                    break;
                  }
                });
  return record;
}
