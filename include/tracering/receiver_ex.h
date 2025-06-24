#ifndef TRACER_RECEIVE_EX_H
#define TRACER_RECEIVE_EX_H

// These additional includes are necessary for the extended receiver API
// which allows handlers to have a context pointer, which is useful for C++
// bindings or more complex use cases in C.

#include "tracering/event.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*trace_event_handler_ex_t)(const trace_event_t *event, void *context);

    void tracer_receiver_register_handler_ex(trace_event_handler_ex_t handler, void *context);
    void tracer_receiver_unregister_handler_ex(trace_event_handler_ex_t handler, void *context);

#ifdef __cplusplus
}
#endif

#endif // TRACER_RECEIVE_EX_H
