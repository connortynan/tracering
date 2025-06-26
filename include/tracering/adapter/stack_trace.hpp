#ifndef TRACERING_ADAPTER_STACK_TRACE_HPP
#define TRACERING_ADAPTER_STACK_TRACE_HPP

#include "tracering/adapter/stack_trace.h"
#include "tracering/adapter/stack_trace_ex.h"
#include "tracering/internal/handler_overload.hpp"

namespace tracering::adapter::stack_trace
{
    struct StackTraceBinding
        : public internal::HandlerRegistryBase<
              trace_span_handler_t,
              trace_span_handler_ex_t,
              trace_span_t,
              StackTraceBinding>
    {
        static void register_handler_ex(trace_span_handler_ex_t fn, void *ctx)
        {
            tracer_adapter_stktrce_register_handler_ex(fn, ctx);
        }

        static void unregister_handler_ex(trace_span_handler_ex_t fn, void *ctx)
        {
            tracer_adapter_stktrce_unregister_handler_ex(fn, ctx);
        }
    };

    inline int init() { return tracer_adapter_stktrce_init(); }
    inline void shutdown() { tracer_adapter_stktrce_shutdown(); }

    template <typename Handler>
    inline void register_handler(Handler &&cb) { StackTraceBinding::register_handler(std::forward<Handler>(cb)); }

    template <typename Handler>
    inline void unregister_handler(Handler &&cb) { StackTraceBinding::unregister_handler(std::forward<Handler>(cb)); }
    inline void unregister_handler_by_context(void *ctx) { StackTraceBinding::unregister_handler_by_context(ctx); }

} // namespace tracering::adapter::stack_trace

#endif // TRACERING_ADAPTER_STACK_TRACE_HPP
