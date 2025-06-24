# tracering – Lightweight C Event Tracing with Shared Memory Ring Buffer

**tracering** is a minimal C/C++ library for real-time trace event capture across multiple threads and programs. It uses a lock-free ring buffer in shared memory to send events from your app to a separate receiver process.

---

## Features

- Lock-free, multi-thread-safe tracing
- Shared memory communication (via `shm_open`)
- Timestamped trace events with thread IDs
- Easy emit API via macros
- Includes test suite with threads

---

## Project Structure

```
include/
  tracer/             # Public headers
    tracer_emit.h       # Emit API
    tracer_receiver.h   # Receiver API
    tracer_event.h      # Event struct
    tracer_buffer.h     # Shared buffer layout

src/
  tracer_emit.c       # Event emit logic
  tracer_receiver.c   # Event receive logic

tests/
  emit_test.c         # Spawns threads, emits events
  receive_test.c      # Reads and prints events

Makefile
build/                # Output binaries and objects
```

---

## Building

```bash
make        # Build all (library + tests)
make clean  # Remove build artifacts
```

---

## Run a Demo

In one terminal:

```bash
./build/receive_test
```

In another (or however many you want!):

```bash
./build/emit_test
```

---

## Usage in Your Project

1. Add `include/tracer/` to your includes.
2. Link against `libtracer.a` or add `tracering_emitter.c` to your build.
3. Emit trace events:

```c
#include <tracering/emitter.h>

tracer_emit_init();

TRACE_NOTIFY(Start);
TRACE(Loop, {
    do_work();
});

tracer_emit_shutdown();
```

---

## ⚠️ Portability Notice

This library currently relies on **Linux-specific APIs** (`shm_open`, `clock_gettime`, `SYS_gettid`, `mmap`, etc.) and is only tested on **Ubuntu 24.04**. It is **not portable** to other operating systems (e.g. Windows or macOS) without significant modification.
Portable code is on the todo list, but cannot be guaranteed at this time.

---

## License

MIT
