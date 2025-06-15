#pragma once

#include "waffle/waffle_core.hpp"
#include <waffle/consumer/iconsumer.hpp>
#include <waffle/waffle_common_types.hpp>

namespace Waffle {
class Consumer : public IConsumer {
public:
public:
  Consumer(MpscRingBuffer<Waffle::Tracelet> &queue,
           const std::unordered_map<uint64_t, std::string> &string_map,
           std::atomic<bool> &shutdown_flag);

  ~Consumer() override = default;

  Consumer(const Consumer &) = delete;
  Consumer &operator=(const Consumer &) = delete;
  Consumer(Consumer &&) = delete;
  Consumer &operator=(Consumer &&) = delete;

  /**
   * @brief Attempts to consume data from the ring buffer and produce a single
   * FullRecord.
   *
   * This method processes Tracelets from the queue. It accumulates data for
   * active spans (SPAN_START, EVENT) and produces a FullRecord when a SPAN_END
   * tracelet is encountered.
   *
   * @return std::optional<model::FullRecord> A FullRecord if a span was
   * completed. std::nullopt if no complete span record could be assembled at
   * this time (e.g., queue is empty, or only partial span data received, or
   * shutdown).
   * @throw ConsumerError If a critical error occurs during processing (e.g.,
   * data corruption).
   */
  std::optional<model::FullRecord> consume() override;

private:
  // Helper struct to store information about spans currently being processed.
  struct PartialSpanInfo {
    uint64_t trace_id_val; // Assuming Waffle::Id can be converted or stored
    uint64_t start_time_unix_nano;
    uint64_t name_hash;
    Waffle::Id parent_span_id;
    Waffle::Id cause_id; // Store for potential future use by processors
    std::vector<Waffle::Attribute> attributes;
    std::vector<Span> events;
  };

  // Helper to resolve string IDs.
  std::string get_string(uint64_t id) const;

  // Helper to convert Waffle::AttributeValue to std::string.
  std::string
  attribute_value_to_string(const Waffle::AttributeValue &value) const;

  MpscRingBuffer<Waffle::Tracelet> &_queue;
  const std::unordered_map<uint64_t, std::string> &_string_map;
  std::atomic<bool> &_shutdown_flag;

  // Map from span_id.value to PartialSpanInfo
  std::unordered_map<Waffle::Id, PartialSpanInfo> _active_spans;
};
} // namespace Waffle