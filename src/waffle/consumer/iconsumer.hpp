#pragma once

#include <waffle/model/full_record.hpp>

namespace Waffle {

// ... other code ...

/**
 * @brief Interface for a component that processes raw trace data (e.g.,
 * Tracelets) from a source like a ring buffer.
 *
 * The IConsumer's primary responsibilities are:
 * 1.  Pulling Tracelet entries from the data source.
 * 2.  Resolving any internal identifiers within the Tracelets (e.g., for string
 * literals or other interned values).
 * 3.  Assembling these pieces of information into FullRecord objects, which
 * represent complete, understandable span data.
 *
 * Implementations of this interface will encapsulate the logic for interacting
 * with the specific data source (like a ring buffer) and the state management
 * required for assembling FullRecords, which might involve correlating multiple
 * Tracelets.
 */
class IConsumer {
public:
  virtual ~IConsumer() = default;

  /**
   * @brief Attempts to consume data from the source and produce a single
   * FullRecord.
   *
   * This method is expected to be called repeatedly. It might block if no data
   * is immediately available or if it needs to wait for more Tracelets to form
   * a complete FullRecord.
   *
   * @return std::optional<model::FullRecord> A FullRecord if one was
   * successfully assembled. std::nullopt if no FullRecord could be assembled at
   * this time (e.g., end of data, or waiting for more partial data).
   * Implementations should define the exact semantics.
   * @throw ConsumerError An error occurred during consumption or processing.
   */
  virtual std::optional<model::FullRecord> consume() = 0;
};

} // namespace Waffle