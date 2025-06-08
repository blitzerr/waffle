#include "waffle/waffle.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace Waffle;
using namespace Waffle::literals;

void implicit_causality_example() {
  std::cout << "\n--- Running Implicit Causality Example ---\n";

  // Manually create the initial "cause" span
  // Note: "initial_cause" is a const char*, which implicitly converts to
  // std::string_view for the start_span overload.
  auto initial_cause_span = detail::g_tracer_instance->start_span(
      "initial_cause", context::get_current_span_id(), kInvalidId);
  Id cause_id = initial_cause_span.id();
  initial_cause_span.end();

  // This parent span is EXPLICITLY caused by the first span
  WAFFLE_SPAN("parent_with_cause", CausedBy(cause_id), "parent_attr"_w = 100);

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  {
    // This nested child span has NO explicit cause
    WAFFLE_SPAN("nested_child_no_cause", "child_attr"_w = "hello");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // This event also has NO explicit cause. The processor should traverse
    // up from its parent ("nested_child_no_cause") to "parent_with_cause"
    // and find the original cause_id.
    WAFFLE_EVENT("important_event", "status"_w = "processing");
  }

} // Spans end via RAII

// Run this example with:
// ./build/examples/WaffleExample
int main() {
  std::cout << "Setting up Waffle tracer...\n";
  Waffle::setup();

  implicit_causality_example();

  std::cout << "\nWork complete. Shutting down Waffle tracer...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  Waffle::shutdown();

  std::cout << "Shutdown complete. Exiting.\n";
  return 0;
}
