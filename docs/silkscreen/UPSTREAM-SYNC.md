# Silkscreen upstream sync

How to bring every Silkscreen firmware repo up to its upstream, in one order, with the same checks every
time. It collects the steps from [PORTING.md, "Upstream merges"](PORTING.md#upstream-merges) and CrossInk's
`docs/silkscreen/PORTING.md`, plus the lessons of the 2026-10-01 rounds. Those two sections keep the per-repo
detail (what each past merge changed, the CrossInk drain checklist); this page is the procedure.

From a Claude Code session opened in `silkscreen-firmware/`, the saved workflow
`.claude/workflows/silkscreen-upstream-sync.js` runs this whole page: status, merges, builds, an adversarial
review with a fix pass, and the records. Ask for "the silkscreen-upstream-sync workflow".

## The repos

| Repo (under `silkscreen-firmware/`) | Branch | Upstream remote | Takes |
|---|---|---|---|
| `crosspoint-reader` | `silkscreen` | `upstream` = crosspoint-reader/crosspoint-reader | `upstream/develop` |
| `crosspoint-reader/freeink-sdk`: **the one shared Silkscreen SDK line** | `silkscreen` | `fk-upstream` = Free-Ink/freeink-sdk (`origin` = iandchasse/freeink-sdk) | the SDK commit upstream CrossPoint pins |
| `crossink` | `silkscreen` | `crossink` = uxjulia/CrossInk (`crosspoint` = the local CrossPoint, `cpupstream`) | `crossink/development` |
| `crossink/freeink-sdk` | detached at the gitlink | `silkscreen-sdk` = the local shared line | nothing of its own: always the shared head |
| `../de-link_pcb` | `master` | `pcb` | not part of a sync; the user's repo |

Pins:

- The shared SDK line follows the commit **upstream CrossPoint pins** (`git ls-tree upstream/develop freeink-sdk`),
  not Free-Ink `main`, which runs ahead of it.
- CrossPoint's gitlink is the shared line's head.
- CrossInk's gitlink is **always** the shared line's head, never CrossInk's own pin
  (CrossInk's PORTING.md, "SDK pin", has the three cases).

## Before starting

- Nobody is building or flashing from these checkouts. The build matrix wipes `.pio/build` (below), and a
  build running at the same time hits `firmware.map` locks or loses its outputs.
- No other job (a Claude workflow, an editor's auto-save, a formatter) is writing to the repos. A job that
  commits with `git add -A` sweeps in whatever new files it finds.
- Working trees are clean. Commits stay local: never push, open a PR or retag without the user's approval.
- Tools on the Windows bench host: Git Bash, `pio` (PlatformIO Core), clang-format 21 at `~/.local/bin`,
  the xtensa g++ for syntax-only checks (`~/.platformio/packages/toolchain-xtensa-esp-elf/bin`, 14.2: the
  esp32s3 package's 8.4 fails 17 of the battery log core's `static_assert`s), and Visual Studio Build Tools
  2022 (MSVC 19.44, toolset 14.44), which **runs** the SDK host tests: `scripts/silkscreen/msvc/` is a `c++` over `cl.exe`
  (its README: use, what ran, the tests it cannot link, the traps). There is no g++ on the host. The tests that
  `#define private public` are syntax-checked with the xtensa g++ instead.
- `git config core.longpaths true` in every repo (the scripts also set it per call).

## Decision rule: match upstream

The owner's rule (2026-10-10): at every fork in the road, match upstream. Where a merge, a conflict or a build setting
offers a choice between upstream's behaviour and a Silkscreen divergence, take upstream's: its defaults, enum values,
language and font sets, flags, wording and UI. A Silkscreen difference stays only where the hardware or a standing rule
needs it (key access, drain, the front light, the board profile, the battery log, the 5 % display step). It is then gated
to Silkscreen, placed after upstream's entries, and listed in the sync record with its reason. A choice this rule does
not settle is reported, not made.

Standing examples: CrossInk's English-only built-in translations (`custom_i18n_builtin_langs = en`) stay as upstream
builds them, and Silkscreen does not restore `all`; a value upstream took that a Silkscreen shortcut once held keeps
upstream's number, and Silkscreen's moves after it (see step 2). The 3.68" variant's panel predicate keeps upstream's own
expression, `gpio.deviceIsX3()`, in the `#else` of its one `#if FREEINK_SILKSCREEN_368`, so every other board is upstream's
to the byte.

## The steps

0. **Status.** `scripts/silkscreen/upstream_status.sh` (from `crosspoint-reader`) fetches every upstream and
   prints what a sync would take, marking each needed action with `!`. Exit 0 means nothing to do.
1. **SDK line.** If upstream CrossPoint's pin is not yet on the shared line, in `crosspoint-reader/freeink-sdk`:
   `git fetch fk-upstream`, then `git merge <pin>` on `silkscreen`.
   - Upstream appends struct fields: keep upstream's fields first, in upstream's order, and the Silkscreen
     fields after them. `SILKSCREEN` uses designated initializers, so it needs a change only when it must set
     a new field. Keep upstream's board code gated to its board.
   - **No clang-format in the SDK.** It has no `.clang-format`, so clang-format picks up the app's config and
     re-wraps hundreds of unrelated lines. Format SDK edits by hand.
   - Run the touched host tests through the MSVC shim (`scripts/silkscreen/msvc/README.md`), syntax-check
     the ones it cannot link with the xtensa g++ (`-std=gnu++17 -fsyntax-only`), and use `static_assert`s on
     `constexpr` rules where a test cannot run.
   - Upstream's new host tests can put their stub directory ahead of ours (2026-10-10: `run_eego.sh` puts
     `eego_stubs` before `metalio_stubs`, and the shared line's `BoardConfig.h` and `SharedI2c.h` need the
     latter's `gpio.h` and `Wire.h`; 2026-10-11: `run_picco_touch.sh` and `picco_stubs`, which also lacked
     `SharedAdc.h`'s `freertos/semphr.h`, `esp_system.h`, `soc/soc_caps.h` and `esp32-hal-periman.h`). Extend the
     stubs so both compile, and syntax-check them. The SDK's host tests are listed by their READMEs and `run*.sh`
     / `run*.py` scripts; check them all, and note one that fails at upstream's own pin (2026-10-11:
     `run_uc8279c_a4.py`) instead of mending it.
   - Commit message: `Merge <pin> (Free-Ink main) into silkscreen`, the body listing the commits taken and
     every conflict resolution.
2. **CrossPoint.** `git merge upstream/develop` on `silkscreen`.
   - For each conflict, find the upstream PR in the commit message and read it before resolving: keep
     upstream's intent, then re-apply the Silkscreen hunk on top.
   - Conflict hotspots: `src/main.cpp`, `lib/hal/HalGPIO.*`, `lib/hal/HalPowerManager.*`, `src/SettingsList.h`,
     `src/CrossPointSettings.*`, `src/activities/reader/EpubReaderActivity.cpp`,
     `src/activities/reader/ClipSelectionActivity.*` and `EpubReaderClippingListActivity.cpp` (the key-access
     hunks), `src/activities/ActivityManager.cpp`, `src/util/PluginEvents.cpp` (keep the Silkscreen early stop on
     the awake ceiling), `platformio.ini` (the `[silkscreen]` and `[silkscreen368]` sections), `lib/I18n/translations/english.yaml`,
     `docs/` and `USER_GUIDE.md`. For the 3.68" variant also `lib/hal/HalDisplay.*` (`isX3Panel()` and the five sites that ask
     it), the splashless-wake rebase in `src/main.cpp`, `SleepActivity.cpp` (the last-screen sleep) and the
     `absoluteImageGrayscale` line of `EpubReaderActivity.cpp`, where upstream's edits to `gpio.deviceIsX3()` land, and
     `src/network/FirmwareBoardTag.cpp` (the two `FREEINK_SILKSCREEN_368` branches stay ahead of the 4.26" Silkscreen ones).
   - Resolve `freeink-sdk` to the step-1 merge (or the unchanged shared head).
   - Run clang-format 21 on the app files you touched (the app has a `.clang-format`), not on the SDK.
   - Settings are stored by index: append new enum values, never renumber. A list that differs by board maps
     positions to values (`SettingInfo::DynamicEnum`, saved by value in `CrossPointSettings::toJson`/`fromJson`).
   - When upstream takes a value a Silkscreen shortcut had (2026-10-10: Create Clipping took Short Power 6 and
     Long-press Confirm 5), upstream's value stays and Silkscreen's moves after it: upstream saves by value, and
     every other board must read its files as upstream does. Build the Silkscreen list from one ordered table
     (upstream's entries in upstream's order, then Silkscreen's), and test a saved value against what the board
     offers (`longPressMenuValueAccepted()`, `shortPowerButtonValueAccepted()`), so other boards equal upstream.
     A web page that hides a row by a list position must ask the device where it is (`toggleFrontlightIndex`).
   - Silkscreen-only behaviour stays gated on the board (`BoardConfig::isSilkscreen()` or profile data), so every
     other board builds and behaves as upstream's.
   - Keep new reader, network and plugin code out of the test image (`check_test_firmware.sh` fails on it).
   - Commit message: `Merge upstream develop (<sha>) into silkscreen`, the body listing the commits taken and
     every conflict resolution.
3. **CrossInk.** `git merge crossink/development` on `silkscreen`, following CrossInk's PORTING.md
   "Upstream merges": the SDK pin cases, `CHANGELOG.md` (move every "Silkscreen:" line back under
   `## [Unreleased]`), and its drain checklist. CrossInk has no plugin system; when it takes upstream CrossPoint
   features that carry Silkscreen rules (its PORTING.md lists them), port the rules in the same merge.
4. **Pins.** CrossInk's gitlink = the shared head = CrossPoint's gitlink. `upstream_status.sh --no-fetch`
   must report no `!` for pins.
5. **Drain and safety review of what came in.** Against POWER.md §3 and the CrossInk drain checklist: every new
   path that blocks the loop, adds a timer or a wake source, keeps the radio or the CPU up, touches the SD card
   near sleep, or lights the front light. The front light is only ever lit by a user action or Restore Light on
   Wake; nothing new may add sleep current.
   - A transfer that runs inside an activity and does not go through `HttpDownloader` (a sync client, a web
     stream, an upload) holds `loop()` as well, and its caller's cutoff check cannot run: it must poll
     `lowBatteryCutoffTripped()` (BAT-01). The 2026-10-10 round found two, CrossPoint's clipping sync
     (`ClippingSync.cpp`, `request()`) and CrossInk's web streams and sync uploads (`CutoffAbortPrint.h`,
     `StatsUploadClient.cpp`). Search the incoming diff for `SecureHttpClient`, `HTTPClient`, `NetworkClient`,
     `readFileToStream` and `WebServer` raw callbacks.
6. **Build and checks.** In each repo, `scripts/silkscreen/build_matrix.sh` (`--full` before a release), one repo at a
   time. It runs the sleep-wake guard, the real envs, the image checks (CrossPoint's test firmware checks; in both apps the
   3.68" variant's no-touch audit, then the same audit on the 4.26" `silkscreen` image as its control, which must
   fail), then the proxy envs **last**: a
   different config file changes PlatformIO's project checksum, which wipes all of `.pio/build`. It refuses to start
   (exit 3) while any `pio` process runs. An agent whose shell commands are killed after 10 minutes builds one env per
   call in the same order (CrossPoint: `silkscreen`, `silkscreen-batlog`, `silkscreen-test`, `silkscreen-368`,
   `silkscreen-368-batlog`, `silkscreen-368-test`, `x4pro`, then `c3check`; CrossInk: `silkscreen`, `silkscreen-batlog`,
   `silkscreen-368`, `silkscreen-368-batlog`, `x4-pro`, `x4-classic`, then `c3check`); a whole matrix takes 40-70
   minutes (CrossInk's seven envs took 46 minutes on 2026-10-12 with the machine quiet; earlier stage logs show 41
   and 66).
   `silkscreen-batlog` is the battery log's own env ([BATTERY-LOG.md](BATTERY-LOG.md)): the release env's sources and flags
   plus `-DSILKSCREEN_BATLOG=1`. It must still build in both repos, and its size next to the release env's is the log's
   footprint. A hook that a conflict resolution left outside its `#if SILKSCREEN_BATLOG` blocker fails every env that lacks
   the macro, but a hook that was dropped fails nothing: step 7 and `upstream_status.sh` look for those.
7. **Review.** Independent reviewers per lens (drain and sleep; Silkscreen behaviour kept through the
   conflicts; upstream intent kept; other boards unchanged; docs; the battery log, below), then a skeptic who tries to
   refute each finding, then a fix pass for the confirmed ones and a rebuild. Every round so far has found real defects.
   **The battery log lens runs after every sync, whether or not the merge touched the log: the SDK's BatteryLog files and
   the apps' hooks must be re-checked.**
   - *SDK* (`libs/hardware/PowerManager`): `include/BatteryLogCore.h`, `include/BatteryLog.h`, `src/BatteryLog.cpp` and
     `test/host/` are intact and wholly inside `#if SILKSCREEN_BATLOG`; the hook is still the first statement of
     `PowerManager::deepSleep()`, before `esp_sleep_config_gpio_isolate()`; the APIs the log reads still mean what they did
     (`adc::Lock(timeout)` and `held()` in `SharedAdc.h`, `BoardConfig::ACTIVE.batteryAdc` and
     `batteryDividerMultiplier`, `esp_rtc_get_time_us`). Compile the core test with the xtensa g++ command in
     `test/host/README.md`.
   - *Apps* (`src/main.cpp`, `lib/hal/HalPowerManager.*`, `lib/hal/HalGPIO.cpp`, CrossPoint's `lib/TrustedTime/TrustedTime.cpp`,
     CrossInk's `lib/hal/HalClock.cpp`, `src/util/BatteryLogWriter.*`): every hook is still there, inside the blocker, at the
     point the design needs. `stageBatteryLogEntry()` is the first thing in `enterDeepSleep()`, before the CPU goes to full
     speed; `onWake()` follows the early wake decision and precedes the touch probe; `early()` is at the early-ghost and
     boot-cutoff exits (CrossPoint also the SD-fail and late-ghost ones); `tickBatteryLog()` runs before the
     exclusive-storage branch of `loop()`; `flushIfDue()` follows the render lock's scope; `flushAtEntry()` runs before the
     Wi-Fi teardown with the card mounted; `carry()` is at the silent-restart funnel; and the release-wake, reset-loop and
     power-pin-fault exits still call no hook. Upstream edits to `enterDeepSleep()`, the early exits of `setup()`, `loop()`,
     the seed branch of `HalPowerManager::getBatteryPercentage()` (and its `#if FREEINK_DEVICE_SILKSCREEN &&
     !SILKSCREEN_BATLOG` return, which cuts the shown value to 5% steps everywhere but in the log image) and
     `HalGPIO::prepareForSleep()` move these points: read those diffs.
   - *Beyond the battery log, key access* (clipping and look-up, [PORTING.md](PORTING.md#upstream-merges), the "Not a
      merge: key access" row): `ClipSelectionActivity::loop()` still sends the keys through `loopButtons()` when
      `hasFrontButtons() && !touchInProgress()` and empties `pendingButtons` on the touch path; `textOffset()` and
      `textXOffset()` (and every touch hit test that adds them) still follow `hasFrontButtons()`, while
      `onEnter()` tests the opening long press with `wordAtPage()`; the clippings list's hint and the toolbar's
      `panelCursorShown` follow it too, and the second Confirm opens the Look Up / Clip / Bookmark list only where
      `hasTouch() && hasFrontButtons()` (upstream's code clips at once). Read each new touch-only control upstream
      adds to the selection, the lists or the reader menu against what a keys-only X4 reaches by keys.
   - *Sleep and wake calls in the SDK* (`PowerManager::armWakeOnPins()`, `deepSleep()`, `powerDownRailsForSleep()`;
     the apps' `HalPowerManager::startDeepSleep()`): read every upstream edit against POWER.md section 2's pin table
     and rules ZD-01, PB-01, PB-02 and WK-01, name the pads and pulls a change reaches on Silkscreen (IO18 is the only
     wake pad; IO1 and IO4 are ADC ladders, IO2, IO8 and IO9 ADC inputs, none of them armed) and say whether the sleep
     floor can move. The sleep guard (`scripts/check_sleep_wake_sources.sh` here, `.py` in CrossInk) fails on a new
     `esp_sleep_pd_config` on purpose: do not widen it before the pin table has been read. 2026-10-11: upstream's
     `ed88f03` put RTC_PERIPH on and an idle-level RTC pull on every ext1 wake pad, which on IO18 (R62 and R76) is
     forbidden, so the SDK leaves a power pin the board biases itself (`InputPins::powerExternalPull`) out of it
     and both guards allow that one call behind that exemption.
   - *The 3.68" variant* ([MODULARITY.md](MODULARITY.md#the-368-variant-silkscreen-368), run after every sync):
     `upstream_status.sh` finds the SDK's macros, profile and driver selection, the strip upload's soft-lock fix and the
     `BUSY_N` pull-up's release before sleep, the five 368 envs, both tags, `isX3Panel()` and its sites, the audit's
     wiring and its control, and in CrossInk its envs, device type, key gate and `deviceHasX3Panel()` sites, but not
     what a merge changed around them. Read the incoming diff for each place
     that tests `gpio.deviceIsX3()` or `Board::XteinkX3`: a site about the X3's *panel* (a refresh mode, a gray base, a
     waveform choice, a sleep-screen path) must ask `display.isX3Panel()`, one about the X3 *board* (the gauge, the SD
     rail, side hints, device names) must not. A new driver or probe path that only `FREEINK_DEVICE_X3` links must stay
     off for `FREEINK_SILKSCREEN_368`: the variant is the X3's UC8279d alone, fixed in its profile and probed by
     nothing, so it links `Uc8279Driver` and no other panel driver and compiles no probe, and
     `scripts/silkscreen/check_no_touch.sh` must still pass for every `silkscreen-368*` image (it fails one that
     links `Uc8253X3Driver`, the SSD1677 or UC8279 X4 driver, or any probe code). In the SDK the hotspots are
     `BoardConfig.h`'s driver macros (`FREEINK_DRIVER_UC8279` is the one line the variant adds itself to;
     `FREEINK_DRIVER_UC8253_X3` must stay the C3 X3's alone), `XteinkDetect.cpp`'s variant block
     (`#if FREEINK_SILKSCREEN_368 ... #elif FREEINK_XTEINK_DISPLAY_PROBE`, ahead of the probe code), `EpdBus`'s X3
     `BUSY_N` pull-up (`FREEINK_EPD_X3_BUSY_N`) and its release before every sleep in
     `PowerManager::powerDownRailsForSleep()`, `PowerManager::sleepPanelWithoutDriver()`'s 60 ms settle and
     `Uc8279Driver::writeGrayscalePlaneStrip()`'s `txn.end()` (if upstream fixes that function, take theirs and drop
     ours, and its `FREEINK_SILKSCREEN_368` line from the status script);
     `libs/hardware/BoardConfig/test/host/run_profile_asserts.py` (41 cases) must still hold. CrossInk's copy of this
     lens is its own page (its PORTING.md, drain checklist item 15).
   - *Tooling and rules:* the analyzer's tests pass (`cd scripts/silkscreen && python -m unittest test_battery_log_report`;
     they compare its column table, flag bits and notch table with `BatteryLogCore.h` and `BatteryMonitor.cpp`, so a
     changed battery curve upstream fails them on purpose), the two apps' `BatteryLogWriter` files still agree (CrossInk's
     differs by one line and one include), and POWER.md BL-01 still holds for what came in.
8. **Record.** A row in PORTING.md's "Upstream merges" table (parent merge, upstream commit, SDK merge, what it
   brought for Silkscreen) and the date and hashes in its Base bullet, and the same in CrossInk's PORTING.md.
   Write them after the fix pass: quote the sizes of the final rebuild (`silkscreen-batlog` included), list the
   review-fix commits, and move CrossInk's "last CrossPoint commit ported" marker and the `git log` base in its
   "Keeping up with CrossPoint" to CrossPoint's head. Then `upstream_status.sh` must print `STATUS: current`.
9. **Bench.** The user flashes (`pio run -e silkscreen -t upload`). Measure the sleep current (POWER.md MEAS-01)
   when a merge changed sleep, boot, `loop()` or network code, and repeat M1 of BATTERY-LOG.md (the release image against
   the `silkscreen-batlog` image) when it also changed the log's hooks.

## Rules the history taught

- Merge, never rebase: older parent commits pin SDK commits that must stay reachable, and the
  `silkscreen-port-1` tags must stay valid.
- Never repair a bad merge or a formatter run with context-free patches (`git apply --unidiff-zero`): on
  2026-10-01 they put a struct field outside its struct and a `static_assert` inside an expression. Restore the
  file and re-apply the edits by hand, then read the full-context diff.
- Read the diff with context (`git diff -U5`) before every commit, and build every target the change touches.
- Upstream commits sometimes need the SDK first: `5b1f0605` needed Free-Ink `bbd528c`. Do step 1 before step 2.
- On Windows, stopping a shell or ending an agent can leave its `pio` children running, and a later build in the same
  checkout then fails on missing `.pio/build` paths. Before a build, check nothing is left
  (`tasklist //FI "IMAGENAME eq pio.exe"`; `build_matrix.sh` does this), and end leftovers with `taskkill /PID <pid> /T /F`.
- Never run `pio check` or any other `pio` command next to a build: on 2026-10-02 that broke PlatformIO's package
  install (esptool, urllib3 and charset-normalizer had to be restored).
- A file appended to with `>>` must end in a newline first: CrossInk's `.gitattributes` did not, and the new rule was
  glued onto its last line.
- Commit messages carry no AI co-author or "generated with" trailer (the bullet "No assistant co-author or
  generated-by trailers" under Git Operation Rules in `.agents/rules/git-workflow.md`; it was rule 4 of that file
  until upstream's #3951 turned its numbered rules into bullets, and AGENTS.md rule 4 until #3890).
- On Windows the working tree can be CRLF while the index is LF (`core.autocrlf`; `git ls-files --eol` shows
  it). A scripted edit must keep each file's endings, or `git diff` shows the whole file changed.
- `firmware.map` is not rewritten when SCons restores `firmware.elf` from its cache. To attribute a size
  change, read the ELF (`xtensa-esp32s3-elf-nm -S --defined-only`): fonts are `*Glyphs` and `*Bitmaps`, the
  translations `STRINGS_*_DATA` and `OFFSETS_*`.
