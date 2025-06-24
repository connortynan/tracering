#ifndef TRACER_RECEIVE_H
#define TRACER_RECEIVE_H

#include "tracering/event.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*trace_event_handler_t)(const trace_event_t *event);

    void tracer_receiver_init(void);
    void tracer_receiver_shutdown(void);
    void tracer_receiver_poll(void);

    void tracer_receiver_register_handler(trace_event_handler_t handler);
    void tracer_receiver_unregister_handler(trace_event_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif // TRACER_RECEIVE_H
