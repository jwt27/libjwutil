/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <memory>

namespace jw
{
    // An allocator adaptor that default-constructs its elements.  This means
    // that trivial types will not be zero-initialized.
    template<typename A>
    struct default_constructing_allocator_adaptor : A
    {
        using value_type = std::allocator_traits<A>::value_type;
        using size_type = std::allocator_traits<A>::size_type;
        using difference_type = std::allocator_traits<A>::difference_type;
        using pointer = std::allocator_traits<A>::pointer;
        using const_pointer = std::allocator_traits<A>::const_pointer;
        using void_pointer = std::allocator_traits<A>::void_pointer;
        using const_void_pointer = std::allocator_traits<A>::const_void_pointer;

        using propagate_on_container_copy_assignment = std::allocator_traits<A>::propagate_on_container_copy_assignment;
        using propagate_on_container_move_assignment = std::allocator_traits<A>::propagate_on_container_move_assignment;
        using propagate_on_container_swap = std::allocator_traits<A>::propagate_on_container_swap;
        template <typename T> struct rebind { using other = default_constructing_allocator_adaptor<typename std::allocator_traits<A>::rebind_alloc<T>>; };

        using A::A;
        using A::operator=;
        constexpr bool operator==(const default_constructing_allocator_adaptor&) const noexcept = default;
        constexpr bool operator!=(const default_constructing_allocator_adaptor&) const noexcept = default;

        template <typename T, typename... Args>
        constexpr void construct(T* p, Args&&... args)
        {
            if constexpr (sizeof...(Args) == 0 and not std::uses_allocator_v<T, A>) new (p) T;
            else std::allocator_traits<A>::construct(*this, p, std::forward<Args>(args)...);
        }
    };

    template<typename T>
    using default_constructing_allocator = default_constructing_allocator_adaptor<std::allocator<T>>;

    // An allocator adaptor that applies uses-allocator construction on its
    // elements.
    // For example, in a std::vector<std::basic_string<...>, ...>, the string
    // elements will be constructed with the same allocator as used for the
    // vector, if the allocators are compatible.
    template<typename A>
    struct uses_allocator_adaptor : A
    {
        using value_type = std::allocator_traits<A>::value_type;
        using size_type = std::allocator_traits<A>::size_type;
        using difference_type = std::allocator_traits<A>::difference_type;
        using pointer = std::allocator_traits<A>::pointer;
        using const_pointer = std::allocator_traits<A>::const_pointer;
        using void_pointer = std::allocator_traits<A>::void_pointer;
        using const_void_pointer = std::allocator_traits<A>::const_void_pointer;

        using propagate_on_container_copy_assignment = std::allocator_traits<A>::propagate_on_container_copy_assignment;
        using propagate_on_container_move_assignment = std::allocator_traits<A>::propagate_on_container_move_assignment;
        using propagate_on_container_swap = std::allocator_traits<A>::propagate_on_container_swap;
        template <typename T> struct rebind { using other = uses_allocator_adaptor<typename std::allocator_traits<A>::rebind_alloc<T>>; };

        using A::A;
        using A::operator=;
        constexpr bool operator==(const uses_allocator_adaptor&) const noexcept = default;
        constexpr bool operator!=(const uses_allocator_adaptor&) const noexcept = default;

        template <typename T, typename... Args>
        constexpr void construct(T* p, Args&&... args)
        {
            std::uninitialized_construct_using_allocator(p, *this, std::forward<Args>(args)...);
        }
    };
}
