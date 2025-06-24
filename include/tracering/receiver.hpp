#ifndef TRACER_RECEIVER_HPP
#define TRACER_RECEIVER_HPP

#include <functional>
#include <vector>
#include <unordered_map>
#include <memory>
#include "tracering/receiver.h"    // C API
#include "tracering/receiver_ex.h" // context-aware internal API

namespace tracering::receiver
{
    inline void init() { tracer_receiver_init(); }
    inline void shutdown() { tracer_receiver_shutdown(); }
    inline void poll() { tracer_receiver_poll(); }

    // Internal structure to manage wrapper lifetimes
    namespace internal
    {
        struct Wrapper
        {
            std::function<void(const trace_event_t *)> func;

            static void trampoline(const trace_event_t *evt, void *ctx)
            {
                static_cast<Wrapper *>(ctx)->func(evt);
            }
        };

        // Track unique handlers by pointer identity
        inline std::unordered_map<std::size_t, std::unique_ptr<Wrapper>> wrapper_map;

        inline std::size_t handler_id(const void *ptr)
        {
            return reinterpret_cast<std::size_t>(ptr);
        }
    }

    // Register
    inline void register_handler(trace_event_handler_t fn)
    {
        tracer_receiver_register_handler(fn);
    }
    inline void register_handler(std::function<void(const trace_event_t *)> cb)
    {
        auto wrapper = std::make_unique<internal::Wrapper>();
        wrapper->func = std::move(cb);
        auto *ptr = wrapper.get();

        tracer_receiver_register_handler_ex(&internal::Wrapper::trampoline, ptr);

        // Track ownership
        internal::wrapper_map[internal::handler_id(ptr)] = std::move(wrapper);
    }

    // Unregister
    inline void unregister_handler(trace_event_handler_t fn)
    {
        tracer_receiver_unregister_handler(fn);
    }

    inline void unregister_handler(std::function<void(const trace_event_t *)> *handle_ptr)
    {
        auto id = internal::handler_id(handle_ptr);
        auto it = internal::wrapper_map.find(id);
        if (it != internal::wrapper_map.end())
        {
            tracer_receiver_unregister_handler_ex(&internal::Wrapper::trampoline, handle_ptr);
            internal::wrapper_map.erase(it); // delete automatically via unique_ptr
        }
    }

    // Optional helper for external cleanup if storing the handle
    inline void unregister_handler_by_context(void *context_ptr)
    {
        auto id = internal::handler_id(context_ptr);
        auto it = internal::wrapper_map.find(id);
        if (it != internal::wrapper_map.end())
        {
            tracer_receiver_unregister_handler_ex(&internal::Wrapper::trampoline, context_ptr);
            internal::wrapper_map.erase(it);
        }
    }

} // namespace tracering::receiver

#endif // TRACER_RECEIVER_HPP
