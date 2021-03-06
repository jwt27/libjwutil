/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <jw/common.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wpadded"
#pragma GCC diagnostic ignored "-Wpacked-not-aligned"

namespace jw
{
    namespace detail
    {
        template<typename, std::size_t, typename = bool>
        union split_int;

        template<typename T, std::size_t size>
        union [[gnu::packed]] alignas(alignment_for_bits(size, 4))
            split_int<T, size, std::enable_if_t<(size > 16) and (size / 2) % 2 == 0, bool>>
        {
            struct [[gnu::packed]]
            {
                split_int<unsigned, (size / 2)> lo;
                split_int<T, (size / 2)> hi;
            };
            std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t> value : size;

            constexpr split_int() noexcept = default;
            constexpr split_int(const split_int&) noexcept = default;
            constexpr split_int(split_int&&) noexcept = default;
            constexpr split_int& operator=(const split_int&) noexcept = default;
            constexpr split_int& operator=(split_int&&) noexcept = default;

            template<typename L, typename H>
            constexpr split_int(L&& l, H&& h) noexcept : lo { std::forward<L>(l) }, hi { std::forward<H>(h) } { }
            constexpr split_int(std::integral auto v) noexcept : value { static_cast<decltype(value)>(v) } { }
            constexpr operator auto() const noexcept { return value; }
        };

        template<typename T, std::size_t size>
        union [[gnu::packed]] alignas(alignment_for_bits(size, 4))
            split_int<T, size, std::enable_if_t<((size <= 16) or (size / 2) % 2 != 0) and (size % 2) == 0, bool>>
        {
            struct [[gnu::packed]]
            {
                unsigned lo : size / 2;
                T hi : size / 2;
            };
            std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t> value : size;

            constexpr split_int() noexcept = default;
            constexpr split_int(const split_int&) noexcept = default;
            constexpr split_int(split_int&&) noexcept = default;
            constexpr split_int& operator=(const split_int&) noexcept = default;
            constexpr split_int& operator=(split_int&&) noexcept = default;

            constexpr split_int(std::integral auto l, std::integral auto h) noexcept : lo { static_cast<unsigned>(l) }, hi { static_cast<T>(h) } { };
            constexpr split_int(std::integral auto v) noexcept : value { static_cast<decltype(value)>(v) } { };
            constexpr operator auto() const noexcept { return value; }
        };
    }

    template<std::size_t N> using split_uint = detail::split_int<unsigned, N>;
    template<std::size_t N> using split_int = detail::split_int<signed, N>;

    using split_uint16_t = split_uint<16>;
    using split_uint32_t = split_uint<32>;
    using split_uint64_t = split_uint<64>;
    using split_int16_t = split_int<16>;
    using split_int32_t = split_int<32>;
    using split_int64_t = split_int<64>;

    static_assert(sizeof(split_uint64_t) == 8);
    static_assert(sizeof(split_uint32_t) == 4);
    static_assert(sizeof(split_uint16_t) == 2);
    static_assert(alignof(split_uint64_t) == 4);
    static_assert(alignof(split_uint32_t) == 4);
    static_assert(alignof(split_uint16_t) == 2);
}

#pragma GCC diagnostic pop
