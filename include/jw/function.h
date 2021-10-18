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

        template<typename R, typename... A>
        static R call(const void* storage, A... args)
        {
            auto* self = static_cast<const functor*>(storage);
            return self->lambda(std::forward<A>(args)...);
        }
    };
    template<typename F>
    functor(F) -> functor<std::remove_cvref_t<F>>;

    template<typename T>
    struct detect_function_instance : std::false_type { };
    template<typename F>
    concept is_function_instance = detect_function_instance<std::remove_cvref_t<F>>::value;

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
    struct function;

    // A simple std::function alternative that never allocates.  It contains
    // enough space to store a lambda that captures N pointer-sized objects.
    // Note that the copy/move assignment operators for the lambda's captured
    // objects will never be called!  Therefore only trivial types, such as
    // pointers and references, can safely be captured.
    template<typename R, typename... A, unsigned N>
    struct function<R(A...), N>
    {
        function() noexcept = default;
        ~function() = default;
        function(function&&) noexcept = default;
        function(const function&) noexcept = default;
        function& operator=(function&&) noexcept = default;
        function& operator=(const function&) noexcept = default;

        function(std::nullptr_t) noexcept : function { } { }

        template<typename F> requires (not detail::is_function_instance<F>)
        function(F&& func) : function { create(std::forward<F>(func)) } { }

        template<unsigned M> requires (M < N)
        function(const function<R(A...), M>& other) noexcept : function { copy(other) } { }

        R operator()(A... args) const { return call(&storage, std::forward<A>(args)...); }

        bool valid() const noexcept { return call != nullptr; }
        operator bool() const noexcept { return valid(); }

    private:
        template<typename F>
        static function create(F&& func)
        {
            auto wrapper = detail::functor { std::forward<F>(func) };
            static_assert(std::is_trivially_destructible_v<decltype(wrapper)>);
            static_assert(sizeof(wrapper) <= sizeof(storage));
            static_assert(alignof(wrapper) <= alignof(storage));
            function f;
            new (&f.storage) decltype(wrapper) { std::move(wrapper) };
            f.call = &decltype(wrapper)::template call<R, A...>;
            return f;
        }

        template<unsigned M>
        static function copy(const function<R(A...), M>& other) noexcept
        {
            function f;
            std::memcpy(&f.storage, &other.storage, sizeof(other.storage));
            f.call = other.call;
            return f;
        }

        template<typename, unsigned> friend struct function;
        using dummy_functor = detail::functor<decltype([x = std::declval<std::array<void*, N>>()](A...) { })>;
        using storage_t = std::aligned_storage_t<sizeof(dummy_functor), alignof(dummy_functor)>;

        storage_t storage;
        R(*call)(const void*, A...) { nullptr };
    };

    template<typename F, typename Signature = typename detail::member_function_signature<decltype(&F::operator())>::type>
    function(F) -> function<Signature, (sizeof(F) - 1) / sizeof(void*) + 1>;
}

namespace jw::detail
{
    template<typename T, unsigned N>
    struct detect_function_instance<function<T, N>> : std::true_type { };
}
