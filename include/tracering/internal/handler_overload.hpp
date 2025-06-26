#ifndef TRACERING_INTERNAL_HANDLER_OVERLOAD_HPP
#define TRACERING_INTERNAL_HANDLER_OVERLOAD_HPP

#include <functional>
#include <memory>
#include <unordered_map>
#include <type_traits>
#include <cstddef>

namespace tracering::internal
{
    template <
        typename CFunc,
        typename CFuncEx,
        typename Payload,
        typename Derived>
    class HandlerRegistryBase
    {
    public:
        using StdFunc = std::function<void(const Payload *)>;

        template <typename Callable>
        static void register_handler(Callable &&cb)
        {
            register_handler_dispatch(std::forward<Callable>(cb),
                                      typename std::is_convertible<Callable, CFunc>::type{},
                                      typename std::is_pointer<std::decay_t<Callable>>::type{});
        }

        template <typename Callable>
        static void unregister_handler(Callable &&cb)
        {
            register_unhandler_dispatch(std::forward<Callable>(cb),
                                        typename std::is_convertible<Callable, CFunc>::type{},
                                        typename std::is_pointer<std::decay_t<Callable>>::type{});
        }

        static void unregister_handler_by_context(void *ctx)
        {
            auto id = handler_id(ctx);
            auto &map = wrappers();
            auto it = map.find(id);
            if (it != map.end())
            {
                Derived::unregister_handler_ex(&Wrapper::trampoline_cpp, ctx);
                map.erase(it);
            }
        }

    private:
        // C function pointer case
        static void register_handler_dispatch(CFunc fn, std::true_type, std::true_type)
        {
            Derived::register_handler_ex(reinterpret_cast<CFuncEx>(trampoline_c),
                                         reinterpret_cast<void *>(fn));
        }

        // std::function/lambda/functor case
        template <typename Callable>
        static void register_handler_dispatch(Callable &&cb, std::false_type, std::false_type)
        {
            auto wrapper = std::make_unique<Wrapper>();
            wrapper->func = StdFunc(std::forward<Callable>(cb));
            void *ctx = wrapper.get();

            Derived::register_handler_ex(&Wrapper::trampoline_cpp, ctx);
            wrappers()[handler_id(ctx)] = std::move(wrapper);
        }

        template <typename Callable>
        static void register_handler_dispatch(Callable &&cb, std::true_type, std::false_type)
        {
            auto fn = static_cast<CFunc>(cb);
            Derived::register_handler_ex(reinterpret_cast<CFuncEx>(trampoline_c),
                                         reinterpret_cast<void *>(fn));
        }

        static void unregister_handler_dispatch(CFunc fn, std::true_type, std::true_type)
        {
            Derived::unregister_handler_ex(reinterpret_cast<CFuncEx>(trampoline_c),
                                           reinterpret_cast<void *>(fn));
        }

        template <typename Callable>
        static void unregister_handler_dispatch(Callable &&, std::false_type, std::false_type)
        {
            static_assert(sizeof(Callable) == 0, "Unregister by context is required for non-C handlers");
        }

        template <typename Callable>
        static void unregister_handler_dispatch(Callable &&cb, std::true_type, std::false_type)
        {
            auto fn = static_cast<CFunc>(cb);
            Derived::unregister_handler_ex(reinterpret_cast<CFuncEx>(trampoline_c),
                                           reinterpret_cast<void *>(fn));
        }

        struct Wrapper
        {
            StdFunc func;
            static void trampoline_cpp(const Payload *payload, void *ctx)
            {
                static_cast<Wrapper *>(ctx)->func(payload);
            }
        };

        static void trampoline_c(const Payload *payload, void *ctx)
        {
            reinterpret_cast<CFunc>(ctx)(payload);
        }

        static std::size_t handler_id(const void *ptr)
        {
            return reinterpret_cast<std::size_t>(ptr);
        }

        static std::unordered_map<std::size_t, std::unique_ptr<Wrapper>> &wrappers()
        {
            static std::unordered_map<std::size_t, std::unique_ptr<Wrapper>> map;
            return map;
        }
    };
}

#endif // TRACERING_INTERNAL_HANDLER_OVERLOAD_HPP
