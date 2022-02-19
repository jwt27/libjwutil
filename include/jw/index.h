/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <concepts>
#include <iterator>

namespace jw
{
    // A simple indexing "iterator", for use in std::for_each and other
    // algorithms.
    template <std::integral T>
    struct index_iterator
    {
        using value_type = T;
        using difference_type = std::ptrdiff_t;

        T i;

        constexpr T operator[](difference_type n) const noexcept { return i + n; }
        constexpr T operator*() const noexcept { return i; }

        constexpr index_iterator& operator++() noexcept { ++i; return *this; }
        constexpr index_iterator& operator--() noexcept { --i; return *this; }
        constexpr index_iterator operator++(int) noexcept { auto tmp = *this; ++(*this); return tmp; }
        constexpr index_iterator operator--(int) noexcept { auto tmp = *this; --(*this); return tmp; }

        constexpr index_iterator& operator+=(difference_type n) noexcept { i += n; return *this; }
        constexpr index_iterator& operator-=(difference_type n) noexcept { i -= n; return *this; }

        constexpr difference_type operator-(const index_iterator& ii) const noexcept { return i - ii.i; }

        constexpr std::strong_ordering operator<=>(const index_iterator&) const noexcept = default;
    };

    template <std::integral T> constexpr inline index_iterator<T> operator+(const index_iterator<T>& i, std::iter_difference_t<index_iterator<T>> n) noexcept { return { i.i + n }; }
    template <std::integral T> constexpr inline index_iterator<T> operator-(const index_iterator<T>& i, std::iter_difference_t<index_iterator<T>> n) noexcept { return { i.i - n }; }
    template <std::integral T> constexpr inline index_iterator<T> operator+(std::iter_difference_t<index_iterator<T>> n, const index_iterator<T>& i) noexcept { return { i.i + n }; }

    using index = index_iterator<std::size_t>;

    static_assert(std::random_access_iterator<index>);
}
