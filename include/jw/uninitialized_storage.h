/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2024 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstddef>
#include <new>
#include <concepts>
#include <algorithm>

namespace jw
{
    template<typename Base, std::size_t MinSize = 0, std::size_t MinAlign = 0>
    struct uninitialized_storage
    {
        static constexpr std::size_t align = std::max(alignof(Base), MinAlign);
        static constexpr std::size_t size = (std::max(sizeof(Base), MinSize) + align - 1) & -align;

        alignas(align) std::byte storage[size];

              Base* pointer()       noexcept { return std::launder(reinterpret_cast<Base*>(storage)); }
        const Base* pointer() const noexcept { return std::launder(reinterpret_cast<const Base*>(storage)); }

              Base& operator *()       noexcept { return *pointer(); }
        const Base& operator *() const noexcept { return *pointer(); }

              Base* operator ->()       noexcept { return pointer(); }
        const Base* operator ->() const noexcept { return pointer(); }

        template<typename T = Base, typename... A>
        requires (std::derived_from<T, Base> and sizeof(T) <= size and alignof(T) <= align)
        T* construct(A&&... args)
        {
            return new (storage) T { std::forward<A>(args)... };
        }

        template<typename T = Base>
        requires (std::derived_from<T, Base> and sizeof(T) <= size and alignof(T) <= align)
        T* default_construct()
        {
            return new (storage) T;
        }

        template<typename T = Base>
        requires (std::derived_from<T, Base> and sizeof(T) <= size and alignof(T) <= align)
        void destroy()
        {
            static_cast<T*>(pointer())->~T();
        }
    };

    template<typename T, typename Base, std::size_t N, std::size_t A, typename... Args>
    T* construct(uninitialized_storage<Base, N, A>& storage, Args&&... args)
    {
        return storage.template construct<T>(std::forward<Args>(args)...);
    }

    template<typename T, typename Base, std::size_t N, std::size_t A>
    T* default_construct(uninitialized_storage<Base, N, A>& storage)
    {
        return storage.template default_construct<T>();
    }

    template<typename T, typename Base, std::size_t N, std::size_t A>
    void destroy(uninitialized_storage<Base, N, A>& storage)
    {
        return storage.template destroy<T>();
    }
}
