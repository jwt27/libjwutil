/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>

namespace jw::detail
{
    template<typename F> struct functor
    {
        F lambda;

        static functor* cast(void* storage) noexcept { return static_cast<functor*>(storage); }
        static const functor* cast(const void* storage) noexcept { return static_cast<const functor*>(storage); }

        template<typename R, typename... A>
        static R call(const void* self, A... args)
        {
            return cast(self)->lambda(std::forward<A>(args)...);
        }

        static void destroy(void* self)
        {
            cast(self)->~functor();
        }

        static void move(void* to, void* from)
        {
            new (to) functor { static_cast<functor&&>(*cast(from)) };
            destroy(from);
        }

        static void copy(void* to, const void* from)
        {
            new (to) functor { static_cast<const functor&>(*cast(from)) };
        }
    };

    struct functor_vtable
    {
        void (*destroy)(void*);
        void (*move)(void*, void*);
        void (*copy)(void*, const void*);

        template<typename F>
        static const functor_vtable* create() noexcept
        {
            static constexpr functor_vtable vtable
            {
                functor<F>::destroy,
                functor<F>::move,
                functor<F>::copy
            };
            return &vtable;
        }

        template<std::size_t N>
        static const functor_vtable* trivial() noexcept
        {
            static constexpr functor_vtable vtable
            {
                trivial_destroy,
                trivial_move<N>,
                trivial_copy<N>
            };
            return &vtable;
        }

    private:
        static void trivial_destroy(void*) { }
        template<std::size_t N>
        static void trivial_move(void* to, void* from) { trivial_copy<N>(to, from); }
        template<std::size_t N>
        static void trivial_copy(void* to, const void* from) { std::memcpy(to, from, N); }
    };

    template <typename T>
    inline constexpr bool is_function_instance = false;

    template<typename>
    struct member_function_signature { };
    template<typename R, typename T, bool Nx, typename... A>
    struct member_function_signature<R(T::*)(A...) noexcept(Nx)> { using type = R(A...); };
    template<typename R, typename T, bool Nx, typename... A>
    struct member_function_signature<R(T::*)(A...) & noexcept(Nx)> { using type = R(A...); };
    template<typename R, typename T, bool Nx, typename... A>
    struct member_function_signature<R(T::*)(A...) const noexcept(Nx)> { using type = R(A...); };
    template<typename R, typename T, bool Nx, typename... A>
    struct member_function_signature<R(T::*)(A...) const & noexcept(Nx)> { using type = R(A...); };
}
