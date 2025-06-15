#pragma once

#include "waffle/waffle_common_types.hpp"
#include "waffle/waffle_core.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace Waffle::model {
using RecordDataValue = std::variant<bool, int64_t, double, std::string>;

struct FullRecord {
  std::string name;
  Tracelet::RecordType rec_ty;
  Id trace_id;
  Id span_id;
  std::optional<Id> parent_id;
  std::optional<Id> cause_id;
  std::unordered_map<std::string, RecordDataValue> data;
};

std::optional<FullRecord> tracelet_to_full_record(
    const Tracelet &tracelet,
    const std::unordered_map<uint64_t, std::string> &id_to_string_map);
} // namespace Waffle::model