#ifndef TRACER_BUFFER_H
#define TRACER_BUFFER_H

#include <stdatomic.h>

#include "tracering/event.h"

#define TRACE_SHM_NAME "/tracering_shm"

#define TRACE_BUFFER_BITS 12
#define TRACE_BUFFER_SIZE (1 << TRACE_BUFFER_BITS)

typedef struct
{
    atomic_uint read_index;
    atomic_uint write_index;
    trace_event_t events[TRACE_BUFFER_SIZE];
    atomic_uchar valid[(TRACE_BUFFER_SIZE + 7) / 8]; // Validity bitmap for events
} trace_shared_buffer_t;

#define GET_EVENT_VALID(buffer, index) \
    (atomic_load_explicit(&(buffer)->valid[(index) / 8], memory_order_acquire) & (1 << ((index) % 8)))

#define SET_EVENT_VALID(buffer, index) \
    (atomic_fetch_or_explicit(&(buffer)->valid[(index) / 8], (1 << ((index) % 8)), memory_order_release))

#define CLEAR_EVENT_VALID(buffer, index) \
    (atomic_fetch_and_explicit(&(buffer)->valid[(index) / 8], ~(1 << ((index) % 8)), memory_order_relaxed))

#endif // TRACER_BUFFER_H