#ifndef TRACERING_ADAPTER_STACK_TRACE_H
#define TRACERING_ADAPTER_STACK_TRACE_H

#include "tracering/event.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        char full_path[256]; // Full nested path "Trace1;Trace2;Trace3"
        uint64_t start_timestamp;
        uint64_t end_timestamp;
        uint32_t thread_id;
    } trace_span_t;

    typedef void (*trace_span_handler_t)(const trace_span_t *span);

    int stack_trace_adapter_init(void);
    void stack_trace_adapter_shutdown(void);
    void stack_trace_adapter_register_handler(trace_span_handler_t handler);
    void stack_trace_adapter_unregister_handler(trace_span_handler_t handler);

    // This function should be registered as a trace event handler with the receiver
    void stack_trace_event_handler(const trace_event_t *event);

#ifdef __cplusplus
}
#endif

#endif // TRACERING_ADAPTER_STACK_TRACE_H