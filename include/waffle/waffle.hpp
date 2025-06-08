#pragma once

// This is the primary include file for using the Waffle tracing library.
// It provides the main user-facing macros WAFFLE_SPAN and WAFFLE_EVENT.
#include "waffle_common_types.hpp" // Provides Id, CausedBy, Attribute, etc.
#include "waffle_core.hpp"         // Provides all core definitions and types.

namespace Waffle {

// --- Helper Macros ---
// These are used internally by the WAFFLE_SPAN and WAFFLE_EVENT macros
// to generate unique variable names.
#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

// --- User-Facing Macros ---

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
      Waffle::context::get_current_span_id(),                                  \
      Waffle::detail::parse_args_impl(__VA_ARGS__).cause, __VA_ARGS__)

// --- Convenience using declarations ---
// Expose commonly used types and functions directly in the Waffle namespace
// for users including waffle.hpp
using Waffle::CausedBy;
using Waffle::Id;
using Waffle::kInvalidId;
using namespace Waffle::literals; // For "key"_w = value syntax
} // namespace Waffle