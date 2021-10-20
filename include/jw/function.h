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

    // A simple std::function alternative that never allocates.  It contains
    // enough space to store a lambda that captures N pointer-sized objects.
    // Note that the copy/move assignment operators for the lambda's captured
    // objects will never be called!  Therefore only trivial types, such as
    // pointers and references, can safely be captured.
    template<typename R, typename... A, unsigned N>
    struct trivial_function<R(A...), N>
    {
        trivial_function() noexcept = default;
        ~trivial_function() = default;
        trivial_function(trivial_function&&) noexcept = default;
        trivial_function(const trivial_function&) noexcept = default;
        trivial_function& operator=(trivial_function&&) noexcept = default;
        trivial_function& operator=(const trivial_function&) noexcept = default;

        trivial_function(std::nullptr_t) noexcept : trivial_function { } { }

        template<typename F> requires (not detail::is_function_instance<std::remove_cvref_t<F>>)
        explicit trivial_function(F&& func) : trivial_function { create(std::forward<F>(func)) } { }

        template<typename F>
        trivial_function& operator=(F&& func) noexcept { return *this = trivial_function { std::forward<F>(func) }; };

        template<unsigned M> requires (M < N)
        trivial_function(const trivial_function<R(A...), M>& other) noexcept : trivial_function { copy(other) } { }

        R operator()(A... args) const { return call(&storage, std::forward<A>(args)...); }

        bool valid() const noexcept { return call != nullptr; }
        explicit operator bool() const noexcept { return valid(); }

    private:
        template<typename F>
        static trivial_function create(F&& func)
        {
            auto wrapper = detail::functor { std::forward<F>(func) };
            static_assert(std::is_trivially_destructible_v<decltype(wrapper)>);
            static_assert(sizeof(wrapper) <= sizeof(storage));
            static_assert(alignof(wrapper) <= alignof(storage));
            trivial_function f;
            new (&f.storage) decltype(wrapper) { std::move(wrapper) };
            f.call = &decltype(wrapper)::template call<R, A...>;
            return f;
        }

        template<unsigned M>
        static trivial_function copy(const trivial_function<R(A...), M>& other) noexcept
        {
            trivial_function f;
            std::memcpy(&f.storage, &other.storage, sizeof(other.storage));
            f.call = other.call;
            return f;
        }

        template<typename, unsigned> friend struct trivial_function;
        using dummy = detail::functor<decltype([x = std::declval<std::array<void*, N>>()](A...) { })>;
        using storage_t = std::aligned_storage_t<sizeof(dummy), alignof(dummy)>;

        storage_t storage;
        R(*call)(const void*, A...) { nullptr };
    };

    template<typename F, typename Signature = typename detail::member_function_signature<decltype(&F::operator())>::type>
    trivial_function(F) -> trivial_function<Signature, (sizeof(F) - 1) / sizeof(void*) + 1>;
}

namespace jw::detail
{
    template <typename Sig, unsigned N>
    inline constexpr bool is_function_instance<trivial_function<Sig, N>> = true;
}
