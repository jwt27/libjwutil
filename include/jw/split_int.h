/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2021 - 2025 J.W. Jagersma, see COPYING.txt for details    */

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
        template<bool, std::size_t>
        union split_int;

        template<bool Signed, std::size_t N> requires ((N > 16) and (N / 2) % 2 == 0)
        union [[gnu::packed]] alignas(alignment_for_bits(N, 4)) split_int<Signed, N>
        {
            struct [[gnu::packed]]
            {
                split_int<false, (N / 2)> lo;
                split_int<Signed, (N / 2)> hi;
            };
            std::conditional_t<Signed, std::int64_t, std::uint64_t> value : N;

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

        template<bool Signed, std::size_t N> requires (((N <= 16) or (N / 2) % 2 != 0) and (N % 2) == 0)
        union [[gnu::packed]] alignas(alignment_for_bits(N, 4)) split_int<Signed, N>
        {
            struct [[gnu::packed]]
            {
                unsigned lo : N / 2;
                std::conditional_t<Signed, signed, unsigned> hi : N / 2;
            };
            std::conditional_t<Signed, std::int64_t, std::uint64_t> value : N;

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

    template<std::size_t N> using split_uint = detail::split_int<false, N>;
    template<std::size_t N> using split_int = detail::split_int<true, N>;

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
