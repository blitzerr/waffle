# waffle
> A Causal C++ Tracing Library for Distributed Systems


Debugging modern distributed systems, especially in machine learning and high-performance computing, is notoriously difficult. When an operation fails, the error message you see is often a distant symptom of a root cause that occurred on a different machine, in a different process, seconds earlier. Waffle is a high-performance tracing library designed to solve this problem.

At its core, Waffle provides a simple way to capture spans (units of work) and events (instantaneous occurrences). Its power feature is first-class causality tracking, allowing you to create explicit links between events and spans. This transforms a simple timeline of logs into a directed graph of cause-and-effect, enabling you to:

Pinpoint Error Origins: Instantly trace a numerical error, like a NaN, from the point of failure back to the exact operation that introduced it.
Understand Data Provenance: See the full computational history of a piece of data, like a model weight or an inference result.
Debug Distributed Deadlocks: Visualize communication patterns and dependencies between nodes.
With an ergonomic C++ core, a seamless Python facade for ML, and a robust C API for HPC, Waffle is built to provide deep observability across your entire stack.

Key Features

High-Performance C++ Core: Designed for minimal overhead in latency-sensitive applications.
First-Class Causality Tracking: Go beyond parent-child relationships to build a true graph of your system's execution.
Ergonomic C++/C APIs: Modern C++ RAII and attribute-based macros for clean, robust instrumentation in systems code.
Seamless Python Integration: A user-friendly Python API that integrates cleanly with JAX and other ML frameworks.
Standard Exporters: Export traces to Jaeger, Zipkin, and Perfetto for powerful visualization and analysis.

## High-Level Architecture

```
+---------------------+     +---------------------+     +-----------------------+
| Python Application  | --> | Python Facade (API) | --> | JAX Profiling Hooks   |
| (e.g., JAX model)   |     +---------------------+     +-----------------------+
+---------------------+              |
                                     | (Calls C API)
                                     v
                               +-----------+
                               |  C Facade |
                               +-----------+
                                     | (Calls C++ Core)
                                     v
+---------------------+     +---------------------+
| C/C++ Application/  | --> | C++ Core Tracing    |
| Library (e.g. PJRT) |     | Library (PolyTracer)|
+---------------------+     +---------------------+
                                     |
                                     | (Via Subscribers/Exporters)
                                     v
             +---------------------------------------------------+
             | Distributed Tracing Backends (OTel, Jaeger, etc.) |
             +---------------------------------------------------+
```

## Use Cases

*   End-to-end profiling of JAX ML models, from Python user code through JAX internals, PJRT plugins, and down to accelerator kernels.
*   Debugging performance bottlenecks in mixed Python/C++/C applications.
*   Understanding the flow of execution in distributed systems.
*   Providing a unified observability layer for complex software stacks.

## Getting Started

### Python Example
The Python API makes it easy to instrument complex machine learning workflows. This example
demonstrates tracing a distributed JAX matrix multiplication to compare its performance profile
against a local CPU implementation.

```Py
import jax
import jax.numpy as jnp
from jax.sharding import Mesh, PartitionSpec, NamedSharding
import numpy as np
import waffle # The Python facade for Waffle

# Configure Waffle to export traces to a visualizer like Jaeger
waffle.set_exporter(waffle.JaegerExporter(service_name="jax-matmul-comparison"))

# --- Define the distributed and local computations ---

# Wraps the function into a span.
# The span name is the function-name and the span metadata becomes the arguments to the function.
@waffle.instrument
def distributed_matmul(A, B):
    return jnp.matmul(A, B)

@waffle.instrument
def cpu_matmul(A, B):
    cpu_device = jax.devices('cpu')[0]
    with waffle.span("data_transfer_to_cpu"):
        A_cpu = jax.device_put(A, cpu_device)
        B_cpu = jax.device_put(B, cpu_device)
    
    with waffle.span("jnp.matmul_on_cpu"):
        return jnp.matmul(A_cpu, B_cpu)

# --- Main execution logic ---
if __name__ == "__main__":
    with waffle.span("matmul_comparison_experiment"):
        # 1. Set up a simulated distributed device mesh
        mesh_devices = jax.local_devices() * 2
        mesh = Mesh(np.array(mesh_devices).reshape(2, -1), axis_names=('data', 'model'))
        sharding_A = NamedSharding(mesh, PartitionSpec('data', None))
        sharding_B = NamedSharding(mesh, PartitionSpec(None, 'model'))

        # 2. Create data
        M, K, N = 8192, 4096, 4096
        key = jax.random.PRNGKey(0)
        A = jax.random.normal(key, (M, K))
        B = jax.random.normal(key, (K, N))

        # 3. Trace the distributed execution
        with waffle.span("distributed_gpu_execution"):
            dist_fn = jax.jit(distributed_matmul, out_shardings=NamedSharding(mesh, PartitionSpec('data', 'model')))
            result_dist = dist_fn(jax.device_put(A, sharding_A), jax.device_put(B, sharding_B))
            result_dist.block_until_ready()

        # 4. Trace the local CPU execution
        with waffle.span("local_cpu_execution"):
            result_cpu = cpu_matmul(A, B)
            result_cpu.block_until_ready()

        print("Experiment complete. Check Jaeger for traces.")
```
### Advanced Usage: C++ API for Systems Integration
The C++ API provides maximum performance and control, making it ideal for instrumenting runtimes, custom kernels, and simulators. This example demonstrates Waffle's power feature—causality tracking—to diagnose a NaN propagation in a distributed NCCL AllReduce operation.

The Scenario

An AllReduce operation sums data from all nodes. If one node contributes a NaN, the result on all nodes becomes NaN. Finding the source node is critical.

The Waffle C++ API

The API uses modern C++ and macros to make instrumentation effortless.

```cpp
// waffle.hpp - Abridged API
namespace Waffle {
    // Strongly-typed, unique identifiers.
    class SpanId { /* ... */ };
    class EventId { /* ... */ };

    // A tag to mark a parameter as a causal link.
    struct CausedBy {
        explicit CausedBy(SpanId id);
        explicit CausedBy(EventId id);
    };

    // Every event automatically gets an Id upon creation.
    [[nodiscard]] EventId Event(std::string_view event_name, ...);

    // The Span RAII object provides a method to get its Id.
    class Span {
    public:
        SpanId id() const;
        // ...
    };
}

#define WAFFLE_SPAN(...) // Creates a waffle::Span object
#define WAFFLE_EVENT(...) // Calls waffle::Event and returns an EventId
```

#### The Example: Finding the NaN Source

```cpp
#include <cmath>
#include "waffle.hpp"
#include "nccl.h"

void PJRT_NcclClient::AllReduce(float* send_buffer, float* recv_buffer, size_t count) {
    Waffle::EventId nan_source_id; // Default, null EventId.

    // 1. A "bad" rank detects NaN in its input and records an event.
    if (this->rank == 1 && contains_nan(send_buffer, count)) {
        // The WAFFLE_EVENT macro now returns an EventId that we can store.
        nan_source_id = WAFFLE_EVENT("Input buffer contains NaN",
            "rank", this->rank,
            "suggestion", "Check data loading pipeline for this rank."
        );
    }

    // 2. The main span for the AllReduce operation links to the event's ID.
    //    The Waffle UI will draw an arrow from the event to this span.
    WAFFLE_SPAN("NcclAllReduce",
        Waffle::CausedBy(nan_source_id), // Establishes the causal link!
        "element_count", count,
        "rank", this->rank
    );
    
    // ... perform the ncclAllReduce operation ...
    
    // 3. An "innocent" rank finds NaN in its *output*.
    if (this->rank == 0 && contains_nan(recv_buffer, count)) {
        // This event is a SYMPTOM. The UI can trace it back to the source
        // by inspecting the causal links of its parent span.
        WAFFLE_EVENT("Output buffer is NaN after AllReduce");
    }
}
```
In the trace viewer, an engineer seeing the error on Rank 0 can now instantly jump to the root cause
on Rank 1, solving a potentially day-long debugging puzzle in seconds.

### Interoperability: C API for HPC and Legacy Code
The C API provides opaque Id types and macros to enable the same powerful linking in pure C 
environments like MPI.

#### The Waffle C API

```C
// waffle_macros.h - Abridged API
typedef struct { /* ... */ } waffle_span_id_t;
typedef struct { /* ... */ } waffle_event_id_t;

// A special macro to establish a causal link using a known Id.
#define KV_CAUSE_SPAN(span_id)   // ...
#define KV_CAUSE_EVENT(event_id) // ...

// Creates a scoped span.
#define SPAN(name, ...)
// Creates an event and returns its waffle_event_id_t.
#define WAFFLE_EVENT(name, ...)
```

#### Example: MPI All-Reduce
```C
#include <stdio.h>
#include <math.h>
#include "mpi.h"
#include "waffle_macros.h"

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    float send_buf[1] = { (float)rank };
    waffle_event_id_t cause_id = {0}; // A null/empty event ID

    // 1. The "bad" rank (rank 1) creates a NaN and captures the EventId.
    if (rank == 1) {
        send_buf[0] = NAN;
        cause_id = WAFFLE_EVENT("Creating NaN on purpose",
            KV_I64("rank", rank)
        );
    }

    // 2. Create a span for the MPI operation, linking to the causal event ID.
    SPAN("MPI_Allreduce_op",
         KV_CAUSE_EVENT(cause_id), // Explicitly link to the event
         KV_I64("rank", rank));
    
    float recv_buf[1] = { 0.0f };
    MPI_Allreduce(send_buf, recv_buf, 1, MPI_FLOAT, MPI_SUM, MPI_COMM_WORLD);

    if (isnan(recv_buf[0])) {
        WAFFLE_EVENT("MPI_Allreduce result is NaN");
    }
    
    MPI_Finalize();
    return 0;
}
```
### Multithreading and Asynchronous Operations
Waffle supports tracing across complex concurrent and asynchronous boundaries via Context Propagation. The Waffle::Context object holds the current trace identity, which can be extracted from one thread and injected into another.

```cpp
// --- In the parent thread ---
WAFFLE_SPAN("dispatch_task");
// Extract the context, which contains the current SpanId.
auto context = Waffle::ExtractContext();
// Pass the context object to the new thread along with the work.
work_queue.push({task_data, context});

// --- In the worker thread ---
Task task = work_queue.pop();
// Inject the parent's context. This makes the current thread's
// tracing a child of the "dispatch_task" span.
Waffle::InjectContext injector(task.trace_context);
WAFFLE_SPAN("process_task"); // This is now correctly parented.
```

This same mechanism provides seamless trace continuity for std::async and can be extended to provide
fully automatic context propagation for C++20 Coroutines when using a library-provided waffle::task type.

## Modules

The Waffle Tracing Library is composed of several key modules, each responsible for a distinct stage in the tracing pipeline:

### 1. Filter
*   **Responsibility:** Determines which traces should be captured.
*   **Details:** Uses a simple filter expression (typically provided via an environment variable) to make sampling decisions. This allows for dynamic control over the volume of trace data generated.
    *(Note: Implementation for parsing and applying complex filter expressions is ongoing.)*

### 2. Interface
*   **Responsibility:** Provides the primary API for application developers to interact with the tracing system.
*   **Details:** This module includes:
    *   `Span`: Represents a single unit of work or operation within a trace. Developers create and manage spans to define the boundaries of their instrumented code.
    *   Event Macros: Convenient macros (e.g., `WAFFLE_EVENT`, `WAFFLE_LOG_INFO`) for recording timestamped events and structured log messages within the context of the current span.
    *   `TraceContext`: Defines the structure for trace identifiers (TraceID, SpanID) and propagation flags.

### 3. Ring Buffer
*   **Responsibility:** Acts as a high-performance, temporary storage for raw trace data (Tracelets) generated by the instrumented application.
*   **Details:** A lock-free or low-contention ring buffer is used to decouple the fast path of trace data generation from the slower processing and exporting tasks, minimizing overhead on the application threads.

### 4. Consumer
*   **Responsibility:** Processes raw trace data from the ring buffer.
*   **Details:** The consumer pulls `Tracelet` entries from the ring buffer. It then resolves any internal identifiers (e.g., for string literals) and assembles these pieces of information into `FullRecord` objects, which represent complete, understandable span data.

### 5. Processors
*   **Responsibility:** Take `FullRecord` objects and perform actions on them.
*   **Details:** Processors are the components that handle the "export" or "analysis" of trace data. Each processor receives an immutable `FullRecord`. Examples include:
    *   Logging Processor: Emits structured logs for each span.
    *   Metrics Processor: Generates metrics (e.g., counts, latencies) from span data.
    *   Backend Exporters: Publish trace data to distributed tracing systems like Jaeger or Prometheus.
*   **Chaining:** Processors can be chained. For instance, a metrics processor might calculate metrics and then pass the `FullRecord` (or derived data) to a counter processor, which then passes it to a histogram processor, eventually leading to an exporter.

### 6. Propagators
*   **Responsibility:** Manage the injection and extraction of trace context across various boundaries.
*   **Details:** When a trace needs to cross a boundary (like a thread, a process, a network call to another host/service), propagators ensure that the trace context (TraceID, SpanID, flags, baggage) is correctly serialized and deserialized. This allows for the reconstruction of the complete end-to-end trace. Examples include HTTP header propagators (e.g., W3C Trace Context, B3) and thread context managers.
