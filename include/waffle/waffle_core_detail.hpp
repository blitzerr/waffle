#pragma once

#include "waffle_common_types.hpp" // Provides Id, CausedBy, Attribute, MAX_ATTRIBUTES_PER_TRACELET, fnv1a_hash, StaticStringSource, CACHE_LINE_SIZE and forward decls for Tracer, Span.
#include <array>                   // For std::array
#include <type_traits>             // For std::is_same_v
#include <utility>                 // For std::forward, std::pair, std::decay_t

namespace Waffle::detail {

struct ParsedArgs {
  Id cause{kInvalidId};
};

inline ParsedArgs parse_args_impl() { return {}; }

template <typename T, typename... Args>
ParsedArgs parse_args_impl(const T &first, const Args &...rest) {
  if constexpr (std::is_same_v<T, CausedBy>) {
    return {first.value};
  } else {
    return parse_args_impl(rest...);
  }
}

// --- Variadic Argument Processing Helpers ---
// (Moved from waffle_core.hpp)

/**
 * @brief A helper template to make static_assert dependent on a template
 * parameter. Used to trigger a static_assert only if a specific 'else
 * constexpr' branch is taken.
 */
template <typename T> inline constexpr bool dependent_false_v = false;

/**
 * @brief Extracts Attribute objects from a parameter pack.
 *
 * This function iterates over the provided arguments, collects instances of
 * Waffle::Attribute into an array, and ignores Waffle::CausedBy objects (as
 * they are handled separately). If any other type of argument is provided, a
 * compile-time error is generated.
 */
template <typename... Args>
inline std::pair<std::array<Attribute, MAX_ATTRIBUTES_PER_TRACELET>, uint8_t>
extract_attributes(Args &&...args) {
  std::array<Attribute, MAX_ATTRIBUTES_PER_TRACELET> attrs_array;
  uint8_t count = 0;

  auto process_arg = [&](auto &&arg) {
    using ArgType = std::decay_t<decltype(arg)>;
    if constexpr (std::is_same_v<ArgType, Attribute>) {
      if (count < MAX_ATTRIBUTES_PER_TRACELET) {
        attrs_array[count++] = std::forward<decltype(arg)>(arg);
      }
    } else if constexpr (!std::is_same_v<ArgType, CausedBy>) {
      static_assert(dependent_false_v<ArgType>,
                    "Unsupported argument type for WAFFLE_SPAN/EVENT. Only "
                    "Attributes and CausedBy are allowed.");
    }
  };

  (process_arg(std::forward<Args>(args)), ...);

  for (size_t i = count; i < MAX_ATTRIBUTES_PER_TRACELET; ++i) {
    attrs_array[i] = {};
  }
  return {attrs_array, count};
}

} // namespace Waffle::detail