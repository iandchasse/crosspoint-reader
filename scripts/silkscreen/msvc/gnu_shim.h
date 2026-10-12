// Force-included by cxx_msvc.py: the GNU builtins the SDK's drivers and tests use, for cl.exe.
#pragma once
#include <intrin.h>
#include <stdlib.h>
#define __builtin_popcount __popcnt
#define __builtin_bswap16 _byteswap_ushort
#define __builtin_bswap32 _byteswap_ulong
#define __builtin_bswap64 _byteswap_uint64
#define __builtin_expect(x, y) (x)

// POSIX localtime_r (the Rtc driver's adjust()): the UCRT has localtime_s with the arguments the other way round.
#include <time.h>
inline struct tm* localtime_r(const time_t* t, struct tm* out) { return localtime_s(out, t) == 0 ? out : nullptr; }
