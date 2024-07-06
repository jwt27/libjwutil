/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2021 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <utility>
#include <type_traits>
#include <array>
#include <tuple>
#include <functional>
#include <cstring>
#include <jw/detail/function.h>

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
        constexpr trivial_function() noexcept = default;
        constexpr ~trivial_function() = default;
        constexpr trivial_function(trivial_function&&) noexcept = default;
        constexpr trivial_function(const trivial_function&) noexcept = default;
        constexpr trivial_function& operator=(trivial_function&&) noexcept = default;
        constexpr trivial_function& operator=(const trivial_function&) noexcept = default;

        template<typename T, unsigned M>
        trivial_function(function<T, M>&&) = delete;
        template<typename T, unsigned M>
        trivial_function(const function<T, M>&) = delete;

        constexpr trivial_function(std::nullptr_t) noexcept : trivial_function { } { }

        template<typename F> requires (not detail::is_function_instance<std::remove_cvref_t<F>>)
        explicit trivial_function(F&& func) : trivial_function { create(std::forward<F>(func)) } { }

        template<typename F>
        trivial_function& operator=(F&& func) noexcept { return *this = trivial_function { std::forward<F>(func) }; }

        template<unsigned M> requires (M < N)
        trivial_function(const trivial_function<R(A...), M>& other) noexcept : call { other.call }
        {
            std::memcpy(storage, &other.storage, sizeof(other.storage));
        }

        R operator()(A... args) const { return call(&storage, std::forward<A>(args)...); }

        constexpr bool valid() const noexcept { return call != nullptr; }
        explicit constexpr operator bool() const noexcept { return valid(); }

    private:
        template<typename F>
        static trivial_function create(F&& func)
        {
            using functor = detail::functor<std::remove_cvref_t<F>>;
            static_assert(std::is_trivially_destructible_v<functor>);
            static_assert(sizeof(functor) <= sizeof(dummy));
            static_assert(alignof(functor) <= alignof(dummy));
            trivial_function f;
            new (&f.storage) functor { std::forward<F>(func) };
            f.call = functor::template call<R, A...>;
            return f;
        }

        template<typename, unsigned> friend struct trivial_function;
        template<typename, unsigned> friend struct function;
        using dummy = detail::functor<decltype([x = std::declval<std::array<void*, N>>()](A...) { })>;

        union
        {
            struct { } nothing { };
            alignas(dummy) std::byte storage[sizeof(dummy)];
        };
        R(*call)(const void*, A&&...) { nullptr };
    };

    template<typename F, typename Signature = typename detail::member_function_signature<decltype(&F::operator())>::type>
    trivial_function(F) -> trivial_function<Signature, (sizeof(F) - 1) / sizeof(void*) + 1>;

    // A fixed-size function object that can store non-trivial lambdas.  It is
    // larger than trivial_function and requires the use of virtual function
    // calls on copy/move/destroy.
    template<typename R, typename... A, unsigned N>
    struct function<R(A...), N>
    {
        constexpr function() noexcept = default;
        constexpr ~function() { if (call != nullptr) vtable->destroy(&storage); }

        template<typename F>
        function& operator=(F&& func) { return assign(std::forward<F>(func)); }

        constexpr function(std::nullptr_t) noexcept : function { } { }

        template<typename F> requires (not detail::is_function_instance<std::remove_cvref_t<F>>)
        explicit function(F&& func) : function { create(std::forward<F>(func)) } { }

        template<unsigned M> requires (M <= N)
        function(function<R(A...), M>&& other) : vtable { other.vtable }, call { other.call }
        {
            if (call == nullptr) return;
            vtable->move(&storage, &other.storage);
            other.call = nullptr;
        }

        template<unsigned M> requires (M <= N)
        function(const function<R(A...), M>& other) noexcept : vtable { other.vtable }, call { other.call }
        {
            if (call == nullptr) return;
            vtable->copy(&storage, &other.storage);
        }

        template<unsigned M> requires (M <= N)
        function(const trivial_function<R(A...), M>& other) noexcept : vtable { detail::functor_vtable::trivial<sizeof(other.storage)>() }, call { other.call }
        {
            std::memcpy(&storage, &other.storage, sizeof(storage));
        }

        R operator()(A... args) const { return call(&storage, std::forward<A>(args)...); }

        constexpr bool valid() const noexcept { return call != nullptr; }
        explicit constexpr operator bool() const noexcept { return valid(); }

    private:
        template<typename F>
        static function create(F&& func)
        {
            using functor = detail::functor<std::remove_cvref_t<F>>;
            static_assert(sizeof(functor) <= sizeof(dummy));
            static_assert(alignof(functor) <= alignof(dummy));
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
        using dummy = trivial_function<R(A...), N>::dummy;

        union
        {
            struct { } nothing { };
            struct
            {
                alignas(dummy) std::byte storage[sizeof(dummy)];
                const detail::functor_vtable* vtable;
            };
        };
        R(*call)(const void*, A&&...) { nullptr };
    };

    template<typename F, typename Signature = typename detail::member_function_signature<decltype(&F::operator())>::type>
    function(F) -> function<Signature, (sizeof(F) - 1) / sizeof(void*) + 1>;

    // A single-use function object with stored arguments.
    template<typename T>
    struct callable_tuple
    {
        template<typename... E>
        constexpr callable_tuple(E&&... elements) : tuple { std::forward<E>(elements)... } { }

        template<typename... A>
        decltype(auto) operator()(A&&... args)
        {
            return call(std::make_index_sequence<std::tuple_size_v<T>> { }, std::forward<A>(args)...);
        }

    private:
        template<std::size_t... I, typename... A>
        decltype(auto) call(std::index_sequence<I...>, A&&... args)
        {
            return std::invoke(std::get<I>(std::move(tuple))..., std::forward<A>(args)...);
        }

        T tuple;
    };

    template<typename... E>
    callable_tuple(E...) -> callable_tuple<std::tuple<std::decay_t<E>...>>;
}

namespace jw::detail
{
    template <typename Sig, unsigned N>
    inline constexpr bool is_function_instance<trivial_function<Sig, N>> = true;

    template <typename Sig, unsigned N>
    inline constexpr bool is_function_instance<function<Sig, N>> = true;
}
