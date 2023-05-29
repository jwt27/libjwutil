/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <ios>
#include <stdexcept>

namespace jw::io
{
    struct io_error : std::runtime_error { using runtime_error::runtime_error; };
    struct overflow : io_error { using io_error::io_error; };
    struct parity_error : io_error { using io_error::io_error; };
    struct framing_error : io_error { using io_error::io_error; };
    struct timeout_error : io_error { using io_error::io_error; };

    struct device_not_found : std::runtime_error { using runtime_error::runtime_error; };

    struct failure : std::ios::failure
    {
        explicit failure(const char* msg) : std::ios::failure { msg } { }
    };

    struct end_of_file : std::ios::failure
    {
        explicit end_of_file() : std::ios::failure { "end of file" } { }
    };
}
