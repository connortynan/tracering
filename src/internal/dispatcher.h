#ifndef CALLBACK_DISPATCHER_H
#define CALLBACK_DISPATCHER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*dispatcher_callback_t)(const void *payload, void *context);

    typedef struct dispatcher dispatcher_t;

    // Create dispatcher with a handler cap and number of worker threads (0 = synchronous)
    dispatcher_t *dispatcher_create(size_t max_handlers, size_t num_threads);
    void dispatcher_destroy(dispatcher_t *d);

    // Register/unregister handler+context pairs
    int dispatcher_register(dispatcher_t *d, dispatcher_callback_t fn, void *context);
    int dispatcher_unregister(dispatcher_t *d, dispatcher_callback_t fn, void *context);

    // Emit one payload to all handlers (synchronized)
    void dispatcher_emit(dispatcher_t *d, const void *payload);

#ifdef __cplusplus
}
#endif

#endif // CALLBACK_DISPATCHER_H
