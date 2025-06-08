#pragma once

#include "waffle_common_types.hpp" // For Id, kInvalidId

namespace Waffle {
namespace context {

Id get_current_span_id();
void set_current_span_id(Id id);

} // namespace context
} // namespace Waffle