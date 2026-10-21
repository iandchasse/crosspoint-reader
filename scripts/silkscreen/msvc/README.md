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

## Tests that compile a C file first (`hostcc`) and the app's host tests

`cxx_msvc.py` takes C++ sources only. `hostcc.py` is a second wrapper with `-c`, `.c` sources and `.o` inputs, for the
runners that build a C object and link it into a C++ test (ContentProtection's miniz, the app's MiniBidi):

```sh
# sh runners: `cc` and `c++` both come from bin-hostcc
export PATH="$PWD/../scripts/silkscreen/msvc/bin-hostcc:$PATH"
bash libs/book/ContentProtection/test/host/run.sh     # zip, lcp, test_protected, test_manifest_reserve (C3 and S3)
```

The app's python host tests (`test/test_*.py`) call `cc` and `c++` by name in a list-form `subprocess.run`, which cannot
start a script, and they slice production source into a harness, so `run_app_host_tests.py` runs each one with those
names replaced by `cc_hostcc.bat` and `cxx_hostcc.bat`. From `crosspoint-reader`:

```sh
python -I -X utf8 scripts/silkscreen/msvc/run_app_host_tests.py            # supported test/test_*.py, then test/clipping_sync
python -I -X utf8 scripts/silkscreen/msvc/run_app_host_tests.py test/test_selection_buttons.py
```

`test/clipping_sync/run.sh` is CRLF in a Windows work tree, so the runner feeds it to `sh -s` with the CRs removed, from
its own directory. Default Windows runs report two documented host limitations as skips; naming either test explicitly still runs it:
`test_trusted_time.py` (its stub includes POSIX `<sys/time.h>`) and `test_patch_animatedgif.py` (it compares LF text
with the CRLF files of the patched `.pio/libdeps`). The Google Test suites in `test/CMakeLists.txt` are not run: the
configure step fetches Google Test from GitHub.

Two of them, `test_selection_buttons.py` and `test/clipping_sync`, build code the Silkscreen port edits
(`ClipSelectionActivity::loop()` is sliced out of the source, `ClippingSync.cpp` is compiled with stubs), so a port
edit that the harness does not model stops them building: the second round's key path and cutoff poll are mirrored in
them, with cases of their own.

## What it does

- Translates `-D`, `-U`, `-I` (the value joined to the flag or as the next argument), `-o` and the sources; ignores
  `-std=`, `-W*`, `-f*`, `-O*` and `-pthread`. It always
  compiles `/std:c++20 /EHsc /utf-8 /W1 /Zc:__cplusplus` with `NDEBUG` undefined, so `assert()` holds (the SDK's
  tests also `#undef NDEBUG`: an `assert()` that wraps a call must not vanish).
- Force-includes `gnu_shim.h`: `__builtin_popcount`, `__builtin_bswap16/32/64`, `__builtin_expect`, POSIX
  `localtime_r` (the UCRT has only `localtime_s`) and `<initializer_list>` (libstdc++ pulls it in through almost every
  header, MSVC's STL does not, and `BoardConfig.h` range-fors over a braced pin list).
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

## What ran (2026-10-12 sync, SDK `336ec77`)

After merging Free-Ink `7fcd3b3` (and reverting this line's gated copy of its strip-transaction fix first). Through
the shim: FreeInkDisplay `run_uc8279.py`, `run_uc8253_power.py` and `run_x3_diagnostics.py`; PowerManager
`run_sleep_panel.py`; **Rtc `run.py`** (upstream's new RX8130 timer test, built both with and without
`FREEINK_CAP_RTC`: 18 RTC and 26 PMIC fault points; it needed the stubs of its own directory extended with the
fallback-RTC fields, `driver/gpio.h`, `esp32-hal-periman.h` and `esp_rom_sys.h`, and the shim to take `-I`, `path` as two
arguments and to supply `localtime_r`); InputManager `run.sh`, `run_metalio.sh`, `run_picco.sh`, `run_picco_touch.sh`;
BatteryMonitor, Imu, HapticManager and SDCardManager `run.sh`; XteinkDetect `test_display_probe`,
`test_silkscreen_policy` and `test_silkscreen_368_policy` (as is and forced `uc8279`); BoardConfig `test_probe_checks`
and FrontlightManager `test_load_policy`; all pass. BoardConfig `run_profile_asserts.py` (41 cases, the Xtensa 14.2
g++) passes, and the battery log core compiles with it. Syntax mode: `run_pro.py`, `run_picco.py`, InputManager
`run_eego.sh` and AudioManager `run_metalio.sh`. `run_uc8279c_a4.py` fails to compile at upstream's own pin (its
recording bus's `beginTxn()` returns `void`), as before.
Negative controls fail as they must: `UC8279_SOURCE` set to a file made with `git show c2d711a^:libs/display/FreeInkDisplay/src/driver/Uc8279Driver.cpp`
(upstream's source before the fix; the shared line's `0b60346` holds the same bytes) stops `run_uc8279.py` on `EpdBus::cmd(0x92) while a beginTxn() Transaction is alive`, and
`UC8253_SOURCE` set to `Uc8253X3Driver.cpp` without its strip's `txn.end()` stops `run_uc8253_power.py` on the same
message. With upstream's ungated fix no configuration of `run_uc8279.py` fails.

## What ran (2026-10-19 sync, SDK `98f42c4`, all pass)

After merging Free-Ink `25cd36c`. Through the shim: **BatteryMonitor `run.sh`** (the charge-ladder decoder, BQ27220, SGM41562 and
upstream's new `test_adc_usb_detect.cpp`, which builds this line's `BatteryMonitor.cpp` and needed the shim's
`<initializer_list>`), InputManager `run.sh`, `run_metalio.sh`, `run_picco.sh` and `run_picco_touch.sh`, FreeInkDisplay
`run_uc8279.py`, `run_uc8253_power.py`, `run_x3_diagnostics.py` and **`run_uc8279c_a4.py`** (it compiles and passes at this
pin: upstream's test changed), PowerManager `run_sleep_panel.py`, Rtc `run.py`, Imu, HapticManager and SDCardManager
`run.sh`, XteinkDetect's display probe, Silkscreen policy and 3.68" policy tests (as is and forced `uc8279`), BoardConfig
`test_probe_checks`, FrontlightManager `test_load_policy`, **FreeInkUI `run.sh`** (295918 checks), **BleKeyboardHost
`run.sh`** (287 checks), SecureNet `run.sh` and ContentProtection's zip and lcp tests. With the Xtensa 14.2 g++:
BoardConfig `run_profile_asserts.py` (41 cases), the battery log core's `static_assert`s (with and without
`SILKSCREEN_BATLOG`), and the syntax checks of FreeInkDisplay `run_pro.py` and `run_picco.py`, InputManager `run_eego.sh` and
AudioManager `run_metalio.sh` (no compile error; the run step finds no binary). The suites that call `cc` or `zip`
(CatalogList, FreeInkFont, FreeInkBook, the last part of ContentProtection) cannot run on this host: the shim has no C
compiler wrapper and the host no `zip`. (`hostcc` is that wrapper since the second round of the same day.)

## What ran (2026-10-19, second round: SDK `bee6382`, app `3b83219b`)

After merging Free-Ink `f9cc1be` and upstream `0fcdb3ec`. Through the shim and `hostcc`, all as before except as noted:
the groups of the first round (BatteryMonitor, InputManager, FreeInkDisplay, PowerManager, Rtc, Imu, HapticManager,
SDCardManager, XteinkDetect, BoardConfig, FrontlightManager, FreeInkUI, SecureNet; FreeInkDisplay `run_picco.py` now
runs through the shim too, by `run_picco_shim.py`: the runner's facade `-fsyntax-only` step goes to `cxx_syntax.bat`), **ContentProtection `run.sh` in
full** (upstream's new `test_manifest_reserve` for C3 and S3 included), the app's seven python host tests that this
host can run (`test_ble_learning`, `test_clipping_highlight_planes`, `test_clipping_selection_text`,
`test_list_viewport`, `test_recursive_dictionary`, `test_remote_button_input`, `test_selection_buttons`) and
`test/clipping_sync` for both chips. BleKeyboardHost's test failed one check in one batch run under load and passed
three re-runs (it mixes real threads with a fake clock).

## What it cannot do

- **Link a test that does `#define private public`.** MSVC mangles the access specifier into the name, so the
  object compiles and the link fails: FreeInkDisplay `run_pro.py` and `run_picco.py`, InputManager `run_eego.sh`,
  AudioManager `run_metalio.sh`. Use the syntax mode: `bin-syntax` on `PATH`, or `CXX=cxx_syntax.bat`, which runs the
  PlatformIO Xtensa g++ (`toolchain-xtensa-esp-elf`, 14.2) with `-fsyntax-only`. The compile step then passes or
  fails as with a real g++, and the run step finds no binary.
- **Suites whose command lines mix C and C++ sources or call `zip`**: CatalogList (its expat sources need
  `winconfig.h` without `XML_GE`), FreeInkFont (`/TC` and `/TP` per file), FreeInkBook (`zip`). They need more than
  `hostcc` does.
- **Static-assert suites that need GNU extensions**: BoardConfig `run_profile_asserts.py` (it finds the Xtensa 14.2 g++
  itself) and PowerManager `test_batterylog_core` (compile with the Xtensa 14.2 g++; the esp32s3 package's 8.4 fails 17 of its
  asserts). `run_uc8279c_a4.py` failed at upstream's own pin until 2026-10-19 and runs through the shim now.
- Sanitizers, and any runner that passes a flag with no cl equivalent beyond the ones above (it prints
  `cxx_msvc: ignoring <flag>`).

## Traps

- `Start-Process -Wait` on `cl.exe` from PowerShell hangs on the lingering `mspdbsrv.exe`. Do not wait on it that way:
  run `cl` from `cmd /c` under a timeout, as `cxx_msvc.py` does.
- A batch file splits `/DX=1` at the `=` when it reads positional parameters. Put arguments in a response file
  (`cl @args.rsp`), as `cxx_msvc.py` does; `cxx_msvc.bat` passes `%*` straight to python.
- Python on an MSYS path (`/c/...`) fails; the wrappers resolve their own directory with `pwd -W`. Give `CXX` a
  `C:/...` path, and put the shim's `bin` on `PATH` in its `/c/...` form: a `C:/...` entry splits at the colon and breaks
  `PATH` (the shim's `python` is then not found). Git Bash also rewrites an argument that starts with `//` (a C++ comment
  passed to a helper script) unless `MSYS_NO_PATHCONV=1` is set.
- Run python runners with `-B` (`importlib` and `-m` write `__pycache__` next to the runner, inside the SDK tree).
- cl's diagnostics are not g++'s: `-Werror` and the `-W*` flags are ignored, so a clean MSVC run is not a clean g++
  `-Wall -Wextra -Werror` run. Syntax-check what you change with the Xtensa g++ too (UPSTREAM-SYNC.md, step 1).
