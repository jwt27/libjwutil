# libjwutil

This is a header-only library of various utility functions and classes, that I
wrote while developing [libjwdpmi](https://github.com/jwt27/libjwdpmi).  I've
separated these headers from that project, since they may prove useful for
other projects, too.

Everything here is OS-independent, but written specifically with x86 in mind.
I expect it should also work on other little-endian machines.

## Installing

Clone this repository somewhere, eg. as a submodule of your project.  Then
point your compiler to the `include/` directory, and you're all set.  The only
required compiler flag is `-std=gnu++20`.
