/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2021 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstdint>
#include <utility>

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

    template<typename T>
    T volatile_load(const volatile T* p) noexcept { return *p; }
    template<typename T, typename U = T>
    void volatile_store(volatile T* p, U&& v) noexcept { *p = std::forward<U>(v); }

    template<typename F>
    struct finally
    {
        F dtor;
        ~finally() { dtor(); }
    };

    template<typename F>
    using local_destructor [[deprecated]] = finally<F>;

    struct empty { };

    using byte = std::uint8_t;
}
