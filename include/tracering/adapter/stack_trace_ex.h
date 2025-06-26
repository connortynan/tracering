#ifndef TRACERING_ADAPTER_STACK_TRACE_EX_H
#define TRACERING_ADAPTER_STACK_TRACE_EX_H

// These additional includes are necessary for the extended receiver API
// which allows handlers to have a context pointer, which is useful for C++
// bindings or more complex use cases in C.

#include "stack_trace.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*trace_span_handler_ex_t)(const trace_span_t *event, void *context);

    void tracer_adapter_stktrce_register_handler_ex(trace_span_handler_ex_t handler, void *context);
    void tracer_adapter_stktrce_unregister_handler_ex(trace_span_handler_ex_t handler, void *context);

#ifdef __cplusplus
}
#endif

#endif // TRACERING_ADAPTER_STACK_TRACE_EX_H
