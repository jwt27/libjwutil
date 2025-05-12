/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2025 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <type_traits>

// Specializable type traits.
namespace jw
{
    template<typename T>
    struct make_signed : std::make_signed<T> { };

    template<typename T>
    using make_signed_t = make_signed<T>::type;

    template<typename T>
    struct make_unsigned : std::make_unsigned<T> { };

    template<typename T>
    using make_unsigned_t = make_unsigned<T>::type;

    template<typename T>
    struct is_signed : std::is_signed<T> { };

    template<typename T>
    constexpr bool is_signed_v = is_signed<T>::value;

    template<typename T>
    struct is_unsigned : std::is_unsigned<T> { };

    template<typename T>
    constexpr bool is_unsigned_v = is_unsigned<T>::value;
}
