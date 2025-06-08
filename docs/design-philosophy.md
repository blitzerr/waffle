# Waffle: Design Philosophy

Waffle is more than a tracing library; it is a manifestation of a core belief: that developers
should be able to understand the complex, distributed systems they build without being burdened by
the complexity of the tools used to observe them. This document outlines the philosophy and design
principles that make Waffle a powerful, performant, and ergonomic hallmark of modern observability.

## Guiding Principles

Our design is guided by three non-negotiable principles:

1. _Minimal Cognitive Load_: The user's focus should be on their application logic, not on learning
   a complex tracing API. For 99% of use cases, the developer should only need to know a single
   concept. The library must handle the complexity of context, trace identity, and propagation
   automatically.


2. _Extreme Performance_: The tracing system must be a near-zero-cost abstraction. The "hot
   path"—the code running in the user's application thread—must be relentlessly optimized to avoid
   heap allocations, locks, and unnecessary overhead. All expensive work must be offloaded to a
   background processing thread.


3. _Extensible by Composition_: The library's backend should be a set of composable primitives
   (processors, exporters) that can be chained together to meet any observability need, from simple
   logging to complex, multi-backend metric and trace aggregation.

## Inspiration from Category Theory

The elegance and extensibility of Waffle's design are deeply inspired by the fundamental concepts of
category theory. This provides a robust mental model for how the library is structured.

- __Objects: The Trace Entity (Waffle::Id)__: In our system, the single fundamental "object" is a
  Trace Entity. This could be a span (an operation with a duration) or an event (an instantaneous
  occurrence). We don't differentiate them at the core level; each is identified by a single,
  unified Waffle::Id. This simplicity is key.
- __Morphisms: The Relationships__: The power of the system comes from the "morphisms," or the
  relationships we can define between these objects:
    1. __Composition (Parent-Child)__: This is the most common relationship, representing a nested
       scope. When one WAFFLE_SPAN is created inside another, a compositional link is formed. The
       library manages this automatically and implicitly through its thread-local context, requiring
       no effort from the user.
    1. __Causality (CausedBy)__: This is a different, more explicit kind of morphism. It is a directed
       "arrow" from one trace entity to another, signifying that the second happened because of the
       first. This is crucial for tracking workflows that are not strictly hierarchical, such as an
       event being triggered by a task that finished earlier. This relationship is also transitive;
       if Span A is caused by B, then all events and child spans within A implicitly inherit that
       causal link, freeing the user from repetitive declarations.
By providing just these two types of relationships, we can model nearly any program flow, from
simple function calls to complex, fan-out/fan-in distributed algorithms.

## Scaling Across All Boundaries

A tracing system is only as good as its ability to follow an operation from start to finish,
wherever it may go. Waffle is designed from the ground up to seamlessly cross every boundary.

- _Async Tasks_: For asynchronous operations where RAII is unsuitable, Waffle provides a manual start_span/end() API. This allows a span's lifetime to be tied to the completion of a future or callback, ensuring accurate measurement of non-blocking code.

- _Threads_: The use of thread_local context ensures that parent-child relationships are correctly maintained within a single process, even in a highly multi-threaded environment.

- _Processes, Hosts, and Microservices_: The Propagator
This is where the concepts of trace_id and cause_id become paramount. To track an operation across service boundaries, Waffle introduces the concept of a Propagator.

Imagine a single user request that flows through three services:
1. Service A (Web Server): A request arrives. The first Waffle span is created, which also establishes a new, unique trace_id for this entire end-to-end operation. Before calling Service B, a propagator injects the current trace_id and its own span_id into the outgoing HTTP request headers, typically following the W3C Trace Context standard (traceparent header).

1. Service B (Auth Service): The request arrives. A propagator extracts the trace_id and the caller's span_id from the headers. It then starts its own first span, but with two key actions:

    It continues using the same trace_id.

    It uses CausedBy(...) to create an explicit causal link back to the span_id from Service A.

1. Service C (GPU Worker): The flow repeats from Service B to Service C.

The result is a single, unified, and correct distributed trace. The `trace_id` groups all the spans
together, while the `CausedBy` links create the explicit causal arrows between the services. The
user only had to place `WAFFLE_SPAN` annotations in their code; the library's propagators and core
machinery handled the immense complexity of distributed context propagation. This is the ultimate
fulfillment of Waffle's design philosophy.
