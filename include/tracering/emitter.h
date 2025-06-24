#ifndef TRACER_EMIT_H
#define TRACER_EMIT_H

#include <stdio.h>
#include <time.h>

#include "tracering/event.h"
#include "tracering/macro_utils.h"

#ifdef __cplusplus
extern "C"
{
#endif

    int tracer_emit_init(void);
    void tracer_emit_shutdown(void);

    void tracer_set(trace_event_t *event);        // sets the timestamp and thread ID
    void tracer_emit(const trace_event_t *event); // will add a copy of the event to the trace buffer

#ifdef __cplusplus
}
#endif

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

#define TRACE_NOTIFY(label)                                                    \
    do                                                                         \
    {                                                                          \
        trace_event_t event;                                                   \
        tracer_set(&event);                                                    \
        snprintf(event.data, TRACE_EVENT_PAYLOAD_MAX, "%s", STRINGIFY(label)); \
        tracer_emit(&event);                                                   \
    } while (0)

// call tracer_emit for multiple labels, all traces share the same timestamp and thread ID,
// but the order of the labels is preserved in the trace buffer
#define TRACE_NOTIFY_LIST(...)                                              \
    do                                                                      \
    {                                                                       \
        trace_event_t event;                                                \
        tracer_set(&event);                                                 \
        const char *labels[] = {MAP_STRINGIFY(__VA_ARGS__)};                \
        for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)     \
        {                                                                   \
            snprintf(event.data, TRACE_EVENT_PAYLOAD_MAX, "%s", labels[i]); \
            tracer_emit(&event);                                            \
        }                                                                   \
    } while (0)

#define TRACE(label, body)                                                     \
    do                                                                         \
    {                                                                          \
        trace_event_t event;                                                   \
        snprintf(event.data, TRACE_EVENT_PAYLOAD_MAX, "%s", STRINGIFY(label)); \
        tracer_set(&event);                                                    \
        tracer_emit(&event);                                                   \
        body;                                                                  \
        tracer_set(&event);                                                    \
        tracer_emit(&event);                                                   \
    } while (0)

#ifndef NDEBUG
#define TRACE_NOTIFY_DEBUG(label) TRACE_NOTIFY(label)
#define TRACE_NOTIFY_LIST_DEBUG(...) TRACE_NOTIFY_LIST(__VA_ARGS__)
#define TRACE_DEBUG(label, body) TRACE(label, body)

#else
#define TRACE_NOTIFY_DEBUG(label)
#define TRACE_NOTIFY_LIST_DEBUG(...)
#define TRACE_DEBUG(label, body) body // TRACE_DEBUG does not emit anything in release builds, but still runs the body

#endif // NDEBUG

#endif // TRACER_EMIT_H