/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2025 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <type_traits>

namespace jw
{
    template<typename...>
    struct type_list;

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

        template<std::size_t I>
        using at = decltype([]
        {
            if constexpr (I == 0)
                return std::type_identity<T> { };
            else
                return std::type_identity<typename next::template at<I - 1>> { };
        }())::type;

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
                return std::type_identity<next> { };
            else
                return std::type_identity<typename next::template remove_at<I - 1>::template prepend<T>> { };
        }())::type;

        using remove_duplicates = next::remove_duplicates::template remove<T>::template prepend<T>;

        template<typename List>
        using concat = decltype([]<typename... Us>(type_list<Us...>)
        {
            return std::type_identity<type_list<T, Ts..., Us...>> { };
        }(List { }))::type;
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

        template<std::size_t N>
        using remove_first = decltype([]
        {
            static_assert (N == 0);
            return std::type_identity<type_list<>> { };
        }())::type;

        template<std::size_t N>
        using remove_last = remove_first<N>;

        using remove_duplicates = type_list<>;

        template<typename U>
        using concat = decltype([]<typename... Us>(type_list<Us...>)
        {
            return std::type_identity<type_list<Us...>> { };
        }(U { }))::type;
    };
}
