#ifndef TRACE_EVENT_H
#define TRACE_EVENT_H

#include <stdint.h>

#define TRACE_EVENT_PAYLOAD_MAX 52

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint64_t timestamp;
        uint32_t thread_id;
        char data[TRACE_EVENT_PAYLOAD_MAX]; // label string
    } trace_event_t;

#ifdef __cplusplus
}
#endif

#endif // TRACE_EVENT_H