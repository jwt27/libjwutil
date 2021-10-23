/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <algorithm>
#include <bit>

namespace jw
{
    inline namespace literals
    {
        constexpr std::uint64_t operator""  _B(std::uint64_t n) { return n << 00; }
        constexpr std::uint64_t operator"" _KB(std::uint64_t n) { return n << 10; }
        constexpr std::uint64_t operator"" _MB(std::uint64_t n) { return n << 20; }
        constexpr std::uint64_t operator"" _GB(std::uint64_t n) { return n << 30; }
        constexpr std::uint64_t operator"" _TB(std::uint64_t n) { return n << 40; }
    }

    // Prevent omission of the frame pointer in the function where this is
    // called.  If a frame pointer is present, stack memory operands in asm
    // statements are always addressed through it.  Without a frame pointer,
    // such operands are addressed via esp which is invalidated by push/pop
    // operations.
    [[gnu::always_inline]]
    inline void force_frame_pointer() noexcept { asm(""::"r"(__builtin_frame_address(0))); }

    [[gnu::always_inline]]
    constexpr inline void assume(bool condition) noexcept { if (not condition) __builtin_unreachable(); }

    consteval inline std::size_t alignment_for_bits(std::size_t nbits, std::size_t max) noexcept
    {
        if (nbits / 8 == 0) return 1;
        return std::min(static_cast<std::size_t>(1 << std::countr_zero(nbits / 8)), max);
    }

    template<typename F>
    struct local_destructor
    {
        F dtor;
        ~local_destructor() { dtor(); }
    };

    struct empty { };

    using byte = std::uint8_t;
}
