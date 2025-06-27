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
    atomic_uint emit_write_index;
    atomic_uint rec_write_index;
    trace_event_t events[TRACE_BUFFER_SIZE];
} trace_shared_buffer_t;

#endif // TRACER_BUFFER_H