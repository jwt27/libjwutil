/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2025 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <type_traits>

namespace jw
{
    template<typename...>
    struct type_list;

    template<typename>
    constexpr bool is_type_list = false;

    template<typename... Ts>
    constexpr bool is_type_list<type_list<Ts...>> = true;

    template<typename T, typename... Ts>
    struct type_list<T, Ts...>
    {
        using first = T;
        using next = type_list<Ts...>;

        static constexpr std::size_t size = 1 + sizeof...(Ts);

        template<typename U>
        static constexpr bool contains = std::is_same_v<T, U> or next::template contains<U>;

        template<template<typename...> typename U>
        using as = U<T, Ts...>;

        template<template<typename> typename U>
        using transform = type_list<U<T>, U<Ts>...>;

        template<typename... Us>
        using prepend = type_list<Us..., T, Ts...>;

        template<typename... Us>
        using append = type_list<T, Ts..., Us...>;

        using reverse = next::reverse::template append<T>;

        template<typename U>
        using remove
            = std::conditional_t<std::is_same_v<T, U>,
                                 typename next::template remove<U>, 
                                 typename next::template remove<U>::template prepend<T>>;

        template<std::size_t N>
        using remove_first
            = std::conditional_t<N == 0, type_list<T, Ts...>, typename next::template remove_first<N - 1>>;

        template<std::size_t N>
        using remove_last = reverse::template remove_first<N>::reverse;

        template<std::size_t I>
        using remove_at = decltype([]
        {
            if constexpr (I == 0)
                return next { };
            else
                return typename next::template remove_at<I - 1>::template prepend<T> { };
        }());

        using remove_duplicates = next::remove_duplicates::template remove<T>::template prepend<T>;

        template<std::size_t I>
        using at = remove_first<I>::first;

        template<typename List>
        using concat = decltype([]<typename... Us>(type_list<Us...>)
        {
            return type_list<T, Ts..., Us...> { };
        }(List { }));

        // Remove types that do not appear in List (preserving order and
        // duplicates).
        template<typename List> requires (is_type_list<List>)
        using intersect
            = std::conditional_t<List::template contains<T>,
                                 typename next::template intersect<List>::template prepend<T>,
                                 typename next::template intersect<List>>;
    };

    template<>
    struct type_list<>
    {
        using next = type_list<>;

        static constexpr std::size_t size = 0;

        template<typename>
        static constexpr bool contains = false;

        template<template<typename...> typename U>
        using as = U<>;

        template<template<typename> typename>
        using transform = type_list<>;

        template<typename... Us>
        using prepend = type_list<Us...>;

        template<typename... Us>
        using append = type_list<Us...>;

        using reverse = type_list<>;

        template<typename>
        using remove = type_list<>;

        template<std::size_t>
        using remove_first = type_list<>;

        template<std::size_t>
        using remove_last = type_list<>;

        using remove_duplicates = type_list<>;

        template<typename List> requires (is_type_list<List>)
        using concat = List;

        template<typename List> requires (is_type_list<List>)
        using intersect = type_list<>;
    };
}
