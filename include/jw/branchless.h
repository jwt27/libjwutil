/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2025 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstdint>
#include <concepts>
#include <limits>
#include <tuple>

namespace jw
{
    template<std::integral T>
    constexpr T sign_mask(T x) noexcept
    {
        if constexpr (std::signed_integral<T>)
            return x >> std::numeric_limits<T>::digits;
        else
            return 0;
    }

    template<std::integral T>
    constexpr T abs(T x) noexcept
    {
        const T sign = sign_mask(x);
        return (x + sign) xor sign;
    }

    template<std::unsigned_integral T>
    constexpr T add_saturate(std::type_identity_t<T> x, std::type_identity_t<T> y) noexcept
    {
        T sum;
        const T carry = __builtin_add_overflow(x, y, &sum);
        return sum | -carry;
    }

    template<std::unsigned_integral T, std::unsigned_integral U>
    constexpr auto sub_saturate(T x, U y) noexcept
    {
        using V = decltype(x - y);
        static_assert(std::unsigned_integral<V>);
        V diff;
        const V borrow = __builtin_sub_overflow(x, y, &diff);
        return diff & (borrow - 1u);
    }

    template<std::integral T>
    constexpr std::pair<T, T> minmax(T a, T b) noexcept
    {
        if constexpr (std::unsigned_integral<T>)
        {
            T diff;
            const T borrow = __builtin_sub_overflow(a, b, &diff);
            const T x = diff & -borrow;
            return { b + x, a - x };
        }
        else
        {
            // This is compiled into a branch anyway, which is faster (on i386).
            const T x = (a xor b) & -static_cast<T>(a < b);
            return { b xor x, a xor x };
        }
    }

    template<typename T>
    constexpr std::pair<T*, T*> minmax(T* a, T* b) noexcept
    {
        auto r = minmax(reinterpret_cast<std::uintptr_t>(a), reinterpret_cast<std::uintptr_t>(b));
        return { reinterpret_cast<T*>(r.first), reinterpret_cast<T*>(r.second) };
    }

    // "inline if"
    template<std::integral T>
    constexpr T iif(bool c, T if_true, T if_false) noexcept
    {
        const auto x = if_true xor if_false;
        const auto y = static_cast<std::make_unsigned_t<T>>(c) - 1;
        return if_true xor (x & y);
    }

    template<typename T>
    constexpr T* iif(bool c, T* if_true, T* if_false) noexcept
    {
        const auto r = iif(c, reinterpret_cast<std::uintptr_t>(if_true), reinterpret_cast<std::uintptr_t>(if_true));
        return reinterpret_cast<T*>(r);
    }

    template<typename T> requires (std::integral<T> or std::is_pointer_v<T>)
    constexpr T min(T a, T b) noexcept
    {
        return minmax(a, b).first;
    }

    template<typename T> requires (std::integral<T> or std::is_pointer_v<T>)
    constexpr T max(T a, T b) noexcept
    {
        return minmax(a, b).second;
    }

    template<typename T> requires (std::integral<T> or std::is_pointer_v<T>)
    constexpr T clamp(T x, T lo, T hi) noexcept
    {
        return min(max(x, lo), hi);
    }

    // Fast alternative to: max(0, x)
    template<std::integral T>
    constexpr T clamp_positive(T x) noexcept
    {
        return x & ~sign_mask(x);
    }

    // Fast alternative to: min(0, x)
    template<std::integral T>
    constexpr T clamp_negative(T x) noexcept
    {
        return x & sign_mask(x);
    }

    // Fast alternative to: max(1, x).
    template<std::unsigned_integral T>
    constexpr T clamp_one(T x) noexcept
    {
        // cmp x, 1; adc x, 0
        return (x < 1) + x;
    }

    // Clamp (signed) array index I between 0 and (unsigned) MAX.
    template<std::integral T, std::integral U>
    constexpr auto clamp_index(T i, U max) noexcept
    {
        [[assume(max >= 0)]];
        return min<std::make_unsigned_t<U>>(clamp_positive(i), max);
    }
}
