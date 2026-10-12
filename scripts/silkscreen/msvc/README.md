# Running the SDK's host tests with MSVC

The SDK's host suites (`freeink-sdk/libs/**/test/host/run*.sh`, `run*.py` and the command lines in their
READMEs) build with `c++` or `$CXX`. The Windows bench host has no g++, but it has Visual Studio Build Tools 2022
(MSVC 19.44, toolset 14.44, `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`). This directory is a
GNU-style `c++` over `cl.exe`, so the suites run there. Before the 3.68" variant stage (2026-10-11) the runbook said
the host had no C++ compiler and the suites were only syntax-checked.

## Use

From `freeink-sdk` (the runners and READMEs expect it), in Git Bash:

```sh
# the sh runners and the README command lines call `c++`
export PATH="$PWD/../scripts/silkscreen/msvc/bin:$PATH"
bash libs/hardware/InputManager/test/host/run.sh

# the python runners call $CXX; a list-form subprocess needs the .bat
CXX="$PWD/../scripts/silkscreen/msvc/cxx_msvc.bat" python -B libs/display/FreeInkDisplay/test/host/run_x3_diagnostics.py
```

(`..` is `crosspoint-reader`; use a path in `C:/...` form for `CXX`.) A compile takes 20-60 s.

## What it does

- Translates `-D`, `-U`, `-I`, `-o` and the sources; ignores `-std=`, `-W*`, `-f*`, `-O*` and `-pthread`. It always
  compiles `/std:c++20 /EHsc /utf-8 /W1 /Zc:__cplusplus` with `NDEBUG` undefined, so `assert()` holds (the SDK's
  tests also `#undef NDEBUG`: an `assert()` that wraps a call must not vanish).
- Force-includes `gnu_shim.h`: `__builtin_popcount`, `__builtin_bswap16/32/64` and `__builtin_expect`.
- Passes every argument through a response file (a line that holds a space is quoted, so a path with a space
  works), runs `cl` under a timeout (`CXX_MSVC_TIMEOUT`, 600 s) and removes its object directory. `VCVARS` names
  `vcvars64.bat` when Visual Studio is not in a default place.

## What ran (2026-10-11, SDK `48be3d0`, all pass)

At the SDK stage, through the scratchpad copy this directory came from: XteinkDetect `test_display_probe`,
`test_silkscreen_policy`, `test_silkscreen_368_policy` (each build variant its README names); FreeInkDisplay
`run_uc8253_power.py`, `run_uc8279.py`, `run_x3_diagnostics.py`; InputManager `run.sh`, `run_metalio.sh`,
`run_picco.sh`, `run_picco_touch.sh`; BatteryMonitor, Imu, SDCardManager and HapticManager `run.sh`; BoardConfig
`test_probe_checks`; FrontlightManager `test_load_policy`; PowerManager `run_sleep_panel.py`.

Re-run from this directory when it was added (the same day, all pass): `test_silkscreen_368_policy` as is, forced
`uc8279` and forced `uc8253` (through `bin/c++`); `run_sleep_panel.py` (both boards), `run_x3_diagnostics.py`,
`run_uc8279.py` and `run_uc8253_power.py` (through `cxx_msvc.bat`). A negative control fails as it must:
`SLEEP_PANEL_SOURCE` pointing at the pre-fix `PowerManager.cpp` (the SDK's `01740b4`) stops `run_sleep_panel.py` on
`r.settleMs == 61`. The syntax mode compiled BatteryMonitor's `test_charge_ladder_decoder.cpp`.

## What ran (2026-10-12, SDK `5fb944d`, all pass)

For the 3.68" variant's UC8279d-only change: XteinkDetect `test_display_probe` (X3 and X4), `test_silkscreen_policy`
(as is and forced `uc8279`) and `test_silkscreen_368_policy` (as is and with the `=uc8279` no-op) through `bin/c++`;
FreeInkDisplay `run_uc8279.py` (with a bus-transaction model that fails a call made inside a held transaction),
`run_uc8253_power.py` and `run_x3_diagnostics.py`, and PowerManager `run_sleep_panel.py`, through `cxx_msvc.bat`;
BoardConfig `run_profile_asserts.py` (41 cases) with the Xtensa 14.2 g++, **not** the shim: with `CXX` set to the shim
every case fails on `cl : Command line error D8004 : '/I' requires an argument`, because the suite needs
`-fsyntax-only` and GNU extensions (leave `CXX` unset). Negative controls fail as they must: upstream's `0b60346`
`PowerManager.cpp` and `Uc8279Driver.cpp` as `SLEEP_PANEL_SOURCE` and `UC8279_SOURCE` stop `run_sleep_panel.py` on the
settle and `run_uc8279.py` on `EpdBus::cmd(0x92) while a beginTxn() Transaction is alive`. There is no forced-`uc8253`
build of `test_silkscreen_368_policy` any more: the SDK refuses that value for the variant.

## What it cannot do

- **Link a test that does `#define private public`.** MSVC mangles the access specifier into the name, so the
  object compiles and the link fails: FreeInkDisplay `run_pro.py` and `run_picco.py`, InputManager `run_eego.sh`,
  AudioManager `run_metalio.sh`. Use the syntax mode: `bin-syntax` on `PATH`, or `CXX=cxx_syntax.bat`, which runs the
  PlatformIO Xtensa g++ (`toolchain-xtensa-esp-elf`, 14.2) with `-fsyntax-only`. The compile step then passes or
  fails as with a real g++, and the run step finds no binary.
- **Static-assert suites that need GNU extensions**: BoardConfig `run_profile_asserts.py` (it finds the Xtensa 14.2 g++
  itself), PowerManager `test_batterylog_core` (compile with the Xtensa 14.2 g++; the esp32s3 package's 8.4 fails 17 of its
  asserts), and `run_uc8279c_a4.py`, which fails at upstream's own pin.
- Sanitizers, and any runner that passes a flag with no cl equivalent beyond the ones above (it prints
  `cxx_msvc: ignoring <flag>`).

## Traps

- `Start-Process -Wait` on `cl.exe` from PowerShell hangs on the lingering `mspdbsrv.exe`. Do not wait on it that way:
  run `cl` from `cmd /c` under a timeout, as `cxx_msvc.py` does.
- A batch file splits `/DX=1` at the `=` when it reads positional parameters. Put arguments in a response file
  (`cl @args.rsp`), as `cxx_msvc.py` does; `cxx_msvc.bat` passes `%*` straight to python.
- Python on an MSYS path (`/c/...`) fails; the wrappers resolve their own directory with `pwd -W`. Give `CXX` a
  `C:/...` path.
- Run python runners with `-B` (`importlib` and `-m` write `__pycache__` next to the runner, inside the SDK tree).
- cl's diagnostics are not g++'s: `-Werror` and the `-W*` flags are ignored, so a clean MSVC run is not a clean g++
  `-Wall -Wextra -Werror` run. Syntax-check what you change with the Xtensa g++ too (UPSTREAM-SYNC.md, step 1).
