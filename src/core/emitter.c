#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "tracering/emitter.h"

#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "../internal/buffer.h"

#ifndef TRACER_ALLOW_OVERWRITE
#define TRACER_ALLOW_OVERWRITE 0
#endif

// Very nonportable helper functions
static inline uint64_t get_timestamp_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}
static inline pid_t get_thread_id()
{
    return syscall(SYS_gettid);
}

static trace_shared_buffer_t *shared = NULL;

int tracer_emit_init(void)
{
    int fd = shm_open(TRACE_SHM_NAME, O_RDWR, 0666);
    shared = mmap(NULL, sizeof(trace_shared_buffer_t),
                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED)
    {
        perror("mmap failed");
        return 1;
    }
    return 0;
}

void tracer_emit_shutdown(void)
{
    if (shared)
    {
        munmap(shared, sizeof(trace_shared_buffer_t));
        shared = NULL;

        // Don't unlink the shared memory here, as it might be used by other processes.
    }
}

void tracer_set(trace_event_t *event)
{
    event->timestamp = get_timestamp_ns();        // Use current time as timestamp
    event->thread_id = (uint32_t)get_thread_id(); // Get the thread ID
}

void tracer_emit(const trace_event_t *event)
{
    if (!shared)
        return;

#if !TRACER_ALLOW_OVERWRITE
    unsigned int write = atomic_load_explicit(&shared->write_index, memory_order_relaxed);
    unsigned int read = atomic_load_explicit(&shared->read_index, memory_order_acquire);

    if (write - read >= TRACE_BUFFER_SIZE)
    {
        // Buffer is full and overwriting is not allowed
        return;
    }
#endif
    unsigned int index = atomic_fetch_add_explicit(&shared->write_index, 1, memory_order_acq_rel) & (TRACE_BUFFER_SIZE - 1);

    shared->events[index] = *event;
    SET_EVENT_VALID(shared, index);
}