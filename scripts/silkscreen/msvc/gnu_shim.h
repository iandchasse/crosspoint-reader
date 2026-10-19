// Force-included by cxx_msvc.py: the GNU builtins the SDK's drivers and tests use, for cl.exe.
#pragma once
#include <intrin.h>
#include <stdlib.h>
// libstdc++ pulls <initializer_list> in through almost every header; MSVC's STL does not, and BoardConfig.h's
// range-for over a braced pin list (parkSdmmcBus(), holdPowerRails()) needs its begin()/end().
#include <initializer_list>
#define __builtin_popcount __popcnt
#define __builtin_bswap16 _byteswap_ushort
#define __builtin_bswap32 _byteswap_ulong
#define __builtin_bswap64 _byteswap_uint64
#define __builtin_expect(x, y) (x)

// POSIX localtime_r (the Rtc driver's adjust()): the UCRT has localtime_s with the arguments the other way round.
#include <time.h>
inline struct tm* localtime_r(const time_t* t, struct tm* out) { return localtime_s(out, t) == 0 ? out : nullptr; }
