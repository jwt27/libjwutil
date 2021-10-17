/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <utility>
#include <type_traits>
#include <array>
#include <functional>

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
}

namespace jw
{
    template<typename, unsigned = 1>
    struct function;

    // A simple std::function alternative that never allocates.  It contains
    // enough space to store a lambda that captures N pointers or references.
    template<typename R, typename... A, unsigned N>
    struct function<R(A...), N>
    {
        function() noexcept = default;
        ~function() = default;
        function(function&&) noexcept = default;
        function(const function&) noexcept = default;

        template<typename F>
        function(F&& func) : function { create(std::forward<F>(func)) } { }

        R operator()(A... args) const { return call(&storage, std::forward<A>(args)...); }

        bool valid() const noexcept { return call != nullptr; }
        operator bool() const noexcept { return valid(); }

    private:
        template<typename F>
        static function create(F&& func)
        {
            auto wrapper = detail::functor { std::forward<F>(func) };
            static_assert(std::is_trivially_copyable_v<std::remove_cvref_t<F>>);
            static_assert(std::is_trivially_destructible_v<std::remove_cvref_t<F>>);
            static_assert(sizeof(wrapper) <= sizeof(storage));
            static_assert(alignof(wrapper) <= alignof(storage));
            function f;
            new (&f.storage) decltype(wrapper) { std::move(wrapper) };
            f.call = &decltype(wrapper)::template call<R, A...>;
            return f;
        }

        using dummy_functor = detail::functor<decltype([x = std::declval<std::array<void*, N>>()](A...) { })>;
        using storage_t = std::aligned_storage_t<sizeof(dummy_functor), alignof(dummy_functor)>;

        storage_t storage;
        R(*call)(const void*, A...) { nullptr };
    };

    template<typename F, typename Signature = typename std::__function_guide_helper<decltype(&F::operator())>::type>
    function(F) -> function<Signature, (sizeof(F) - 1) / sizeof(void*) + 1>;
}
