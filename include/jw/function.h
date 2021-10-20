/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <utility>
#include <type_traits>
#include <array>
#include <cstring>

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

    // Adapted from libstdc++ __function_guide_helper
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

namespace jw
{
    template<typename, unsigned = 1>
    struct trivial_function;
    template<typename, unsigned = 1>
    struct function;

    // A simple std::function alternative that never allocates.  It contains
    // enough space to store a lambda that captures N pointer-sized objects.
    // The lambda's destructor and copy/move constructors are never used,
    // which makes it very cheap to pass around.  This also implies that only
    // trivial types, such as pointers and references, can safely be captured.
    template<typename R, typename... A, unsigned N>
    struct trivial_function<R(A...), N>
    {
        trivial_function() noexcept = default;
        ~trivial_function() = default;
        trivial_function(trivial_function&&) noexcept = default;
        trivial_function(const trivial_function&) noexcept = default;
        trivial_function& operator=(trivial_function&&) noexcept = default;
        trivial_function& operator=(const trivial_function&) noexcept = default;

        template<typename T, unsigned M>
        trivial_function(function<T, M>&&) = delete;
        template<typename T, unsigned M>
        trivial_function(const function<T, M>&) = delete;

        trivial_function(std::nullptr_t) noexcept : trivial_function { } { }

        template<typename F> requires (not detail::is_function_instance<std::remove_cvref_t<F>>)
        explicit trivial_function(F&& func) : trivial_function { create(std::forward<F>(func)) } { }

        template<typename F>
        trivial_function& operator=(F&& func) noexcept { return *this = trivial_function { std::forward<F>(func) }; };

        template<unsigned M> requires (M < N)
        trivial_function(const trivial_function<R(A...), M>& other) noexcept : call { other.call }
        {
            std::memcpy(storage, &other.storage, sizeof(other.storage));
        }

        R operator()(A... args) const { return call(&storage, std::forward<A>(args)...); }

        bool valid() const noexcept { return call != nullptr; }
        explicit operator bool() const noexcept { return valid(); }

    private:
        template<typename F>
        static trivial_function create(F&& func)
        {
            using functor = detail::functor<std::remove_cvref_t<F>>;
            static_assert(std::is_trivially_destructible_v<functor>);
            static_assert(sizeof(functor) <= sizeof(storage));
            static_assert(alignof(functor) <= alignof(storage));
            trivial_function f;
            new (&f.storage) functor { std::forward<F>(func) };
            f.call = functor::template call<R, A...>;
            return f;
        }

        template<typename, unsigned> friend struct trivial_function;
        template<typename, unsigned> friend struct function;
        using dummy = detail::functor<decltype([x = std::declval<std::array<void*, N>>()](A...) { })>;
        using storage_t = std::aligned_storage_t<sizeof(dummy), alignof(dummy)>;

        storage_t storage;
        R(*call)(const void*, A...) { nullptr };
    };

    template<typename F, typename Signature = typename detail::member_function_signature<decltype(&F::operator())>::type>
    trivial_function(F) -> trivial_function<Signature, (sizeof(F) - 1) / sizeof(void*) + 1>;

    // A fixed-size function object that can store non-trivial lambdas.  It is
    // larger than trivial_function and requires the use of virtual function
    // calls on copy/move/destroy.
    template<typename R, typename... A, unsigned N>
    struct function<R(A...), N>
    {
        function() noexcept = default;
        ~function() { if (call != nullptr) vtable->destroy(&storage); }

        template<typename F>
        function& operator=(F&& func) { return assign(std::forward<F>(func)); };

        function(std::nullptr_t) noexcept : function { } { }

        template<typename F> requires (not detail::is_function_instance<std::remove_cvref_t<F>>)
        explicit function(F&& func) : function { create(std::forward<F>(func)) } { }

        template<unsigned M> requires (M <= N)
        function(function<R(A...), M>&& other) : call { other.call }, vtable { other.vtable }
        {
            if (call == nullptr) return;
            vtable->move(&storage, &other.storage);
            other.call = nullptr;
        }

        template<unsigned M> requires (M < N)
        function(const function<R(A...), M>& other) noexcept : call { other.call }, vtable { other.vtable }
        {
            if (call == nullptr) return;
            vtable->copy(&storage, &other.storage);
        }

        template<unsigned M> requires (M <= N)
        function(const trivial_function<R(A...), M>& other) noexcept : call { other.call }, vtable { detail::functor_vtable::trivial<sizeof(other.storage)>() }
        {
            std::memcpy(&storage, &other.storage, sizeof(storage));
        }

        R operator()(A... args) const { return call(&storage, std::forward<A>(args)...); }

        bool valid() const noexcept { return call != nullptr; }
        explicit operator bool() const noexcept { return valid(); }

    private:
        template<typename F>
        static function create(F&& func)
        {
            using functor = detail::functor<std::remove_cvref_t<F>>;
            static_assert(sizeof(functor) <= sizeof(storage));
            static_assert(alignof(functor) <= alignof(storage));
            function f;
            new (&f.storage) functor { std::forward<F>(func) };
            f.call = functor::template call<R, A...>;
            f.vtable = detail::functor_vtable::create<std::remove_cvref_t<F>>();
            return f;
        }

        template <typename F>
        function& assign(F&& other)
        {
            this->~function();
            return *new(this) function { std::forward<F>(other) };
        }

        template<typename, unsigned> friend struct function;
        using storage_t = trivial_function<R(A...), N>::storage_t;

        storage_t storage;
        R(*call)(const void*, A...) { nullptr };
        const detail::functor_vtable* vtable;
    };

    template<typename F, typename Signature = typename detail::member_function_signature<decltype(&F::operator())>::type>
    function(F) -> function<Signature, (sizeof(F) - 1) / sizeof(void*) + 1>;
}

namespace jw::detail
{
    template <typename Sig, unsigned N>
    inline constexpr bool is_function_instance<trivial_function<Sig, N>> = true;

    template <typename Sig, unsigned N>
    inline constexpr bool is_function_instance<function<Sig, N>> = true;
}
