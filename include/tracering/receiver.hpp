#ifndef TRACER_RECEIVER_HPP
#define TRACER_RECEIVER_HPP

#include "tracering/receiver.h"
#include "tracering/receiver_ex.h"
#include "tracering/internal/handler_overload.hpp"

namespace tracering::receiver
{
    struct ReceiverBinding
        : public internal::HandlerRegistryBase<
              trace_event_handler_t,
              trace_event_handler_ex_t,
              trace_event_t,
              ReceiverBinding>
    {
        static void register_handler_ex(trace_event_handler_ex_t fn, void *ctx)
        {
            tracer_receiver_register_handler_ex(fn, ctx);
        }

        static void unregister_handler_ex(trace_event_handler_ex_t fn, void *ctx)
        {
            tracer_receiver_unregister_handler_ex(fn, ctx);
        }
    };

    inline void init() { tracer_receiver_init(); }
    inline void shutdown() { tracer_receiver_shutdown(); }
    inline void poll() { tracer_receiver_poll(); }

    template <typename Handler>
    inline void register_handler(Handler &&cb) { ReceiverBinding::register_handler(std::forward<Handler>(cb)); }

    template <typename Handler>
    inline void unregister_handler(Handler &&cb) { ReceiverBinding::unregister_handler(std::forward<Handler>(cb)); }

    inline void unregister_handler_by_context(void *ctx) { ReceiverBinding::unregister_handler_by_context(ctx); }
}

#endif // TRACER_RECEIVER_HPP
