/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2021 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/type_traits.h>
#include <cstdint>
#include <bit>
#include <algorithm>
#include <concepts>

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wpadded"

namespace jw::detail
{
    consteval inline std::size_t alignment_for_bits(std::size_t nbits, std::size_t max = alignof(std::uintmax_t)) noexcept
    {
        if (nbits / 8 == 0) return 1;
        return std::min<std::size_t>(1 << std::countr_zero(nbits / 8), max);
    }

    template<std::size_t N>
    using unsigned_int_type_for_bits =
        std::conditional_t<(N <= 8), std::uint8_t,
        std::conditional_t<(N <= 16), std::uint16_t,
        std::conditional_t<(N <= 32), std::uint32_t,
        std::conditional_t<(N <= 64), std::uint64_t, std::uintmax_t>>>>;

    template<std::size_t N>
    using signed_int_type_for_bits = std::make_signed_t<unsigned_int_type_for_bits<N>>;

    template<bool Signed, std::size_t N>
    using int_type_for_bits = std::conditional_t<Signed, signed_int_type_for_bits<N>, unsigned_int_type_for_bits<N>>;

    template<bool Signed, std::size_t N>
    struct [[gnu::packed]] alignas(alignment_for_bits(N)) specific_int
    {
        static_assert(N <= sizeof(std::uintmax_t) * 8);
        static constexpr std::size_t storage_bits = (N + 7) & -8;
        using int_type = int_type_for_bits<Signed, N>;

        int_type value : storage_bits;

        constexpr specific_int() noexcept = default;
        constexpr specific_int(const specific_int&) noexcept = default;
        constexpr specific_int& operator=(const specific_int&) noexcept = default;

        constexpr specific_int(std::convertible_to<int_type> auto v) noexcept
            : value { mask(v) }
        { }

        constexpr operator int_type() const noexcept
        {
            using limits = std::numeric_limits<std::make_unsigned_t<int_type>>;
            if constexpr (Signed)
            {
                constexpr auto n = limits::digits - storage_bits;
                return static_cast<int_type>(value << n) >> n;
            }
            else
            {
                [[assume((value & ((1ull << N) - 1)) == value)]];
                return value;
            }
        }

        static constexpr int_type mask(auto v) noexcept
        {
            using limits = std::numeric_limits<std::make_unsigned_t<int_type>>;
            constexpr auto n = limits::digits - N;
            return static_cast<int_type>(v) << n >> n;
        }
    };
}

namespace jw
{
    // N includes the sign bit.
    template<std::size_t N> using specific_int = detail::specific_int<true, N>;
    template<std::size_t N> using specific_uint = detail::specific_int<false, N>;

    static_assert( sizeof(specific_uint<48>) == 6);
    static_assert( sizeof(specific_uint<24>) == 3);
    static_assert( sizeof(specific_uint<12>) == 2);
    static_assert( sizeof(specific_uint< 6>) == 1);
    static_assert(alignof(specific_uint<48>) == 2);
    static_assert(alignof(specific_uint<24>) == 1);
    static_assert(alignof(specific_uint<12>) == 1);
    static_assert(alignof(specific_uint< 6>) == 1);

    template<bool Signed, std::size_t N>
    struct make_signed<detail::specific_int<Signed, N>> { using type = detail::specific_int<true, N>; };

    template<bool Signed, std::size_t N>
    struct make_unsigned<detail::specific_int<Signed, N>> { using type = detail::specific_int<false, N>; };

    template<bool Signed, std::size_t N>
    struct is_signed<detail::specific_int<Signed, N>> : std::integral_constant<bool, Signed> { };

    template<bool Signed, std::size_t N>
    struct is_unsigned<detail::specific_int<Signed, N>> : std::integral_constant<bool, not Signed> { };
}

#pragma GCC diagnostic pop
