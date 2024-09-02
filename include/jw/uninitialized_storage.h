/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2024 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstddef>
#include <new>

namespace jw
{
    template<typename T>
    struct uninitialized_storage
    {
        alignas(T) std::byte storage[sizeof(T)];

              T* pointer()       noexcept { return std::launder(reinterpret_cast<      T*>(storage)); }
        const T* pointer() const noexcept { return std::launder(reinterpret_cast<const T*>(storage)); }

              T& operator *()       noexcept { return *pointer(); }
        const T& operator *() const noexcept { return *pointer(); }

              T* operator ->()       noexcept { return pointer(); }
        const T* operator ->() const noexcept { return pointer(); }

        template<typename... A>
        T* construct(A&&... args) { return new (storage) T { std::forward<A>(args)... }; }
        T* default_construct() { return new (storage) T; }
        void destroy() { pointer()->~T(); }
    };
}
