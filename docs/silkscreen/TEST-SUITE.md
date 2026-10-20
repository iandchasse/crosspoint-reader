# Silkscreen hardware test firmware

A second firmware image for bring-up. It is built from this branch as `[env:silkscreen-test]` (`-DCROSSPOINT_TEST_SUITE=1`), boots straight into a menu of separate tests and cannot open a book. It runs the reader's own boot, sleep and wake code and the same SDK managers, so a failure in a test points at the board, not at a second implementation. The bring-up steps it serves are in [PORTING.md](PORTING.md#bring-up-checklist-real-board), and the sleep budget and pin rules in [POWER.md](POWER.md).

Everything test-only is under `#if CROSSPOINT_TEST_SUITE` or in `src/activities/testsuite/`, which the reader envs compile to nothing. The reader image does not change.

The 3.68" X3-panel variant has its own build of it, `[env:silkscreen-368-test]`, from the same sources: no touch, the SDK's X3 UC8279d driver, and the screens that say what is true there. It is [below](#the-368-variant-silkscreen-368-test), with its bench list.

## Build and flash

```bash
pio run -e silkscreen-test -t upload
```

- The env inherits the `[silkscreen]` hooks from `platformio.local.ini`:
  - `frontlight_flags`, so a board with no LED load that drops the light drops it here too;
  - `display_flags`, the controller override;
  - `touch_flags`, the GT911 fallback for the alternate jumpers.
- Go back to the reader over USB: `pio run -e silkscreen -t upload` (`-e silkscreen-368` for the 3.68" variant). The two images carry different board tags (`silkscreen-test` and `silkscreen`; `silkscreen-368-test` and `silkscreen-368`), so neither accepts the other through OTA or the SD firmware update, and the test image has neither.
- While a USB data host is attached the test firmware holds auto-sleep off, so esptool can reach it. If the board is asleep, hold POWER to wake it first.
- CI builds the env and keeps `firmware-silkscreen-test.bin` as a build artifact. It is never a release asset.
- The 3.68" variant: `pio run -e silkscreen-368-test -t upload`. It inherits `[silkscreen368]`'s hooks (`frontlight_flags` and `test_serial_flags` for an opt-in USB-serial wait; it has no `touch_flags` and no `display_flags`: its controller is the UC8279d, fixed in the build), carries the board tag `silkscreen-368-test`, and CI keeps `firmware-silkscreen-368-test.bin`. `bash scripts/check_test_firmware.sh silkscreen-368-test` and `bash scripts/silkscreen/check_no_touch.sh silkscreen-368-test` check it.
- `bash scripts/check_test_firmware.sh [env]` (after a build) checks the image, as CI does: no reader, library, book-parsing, web-server, OTA, SD-update, plugin, Bluetooth-radio or light-panel function is linked; no static initialiser from reader code runs except seven allowed ones from `lib/Epub`, `lib/LibraryIndex` and the SDK's Opds library (below); and the board tag `CROSSPOINT-BOARD-V1:silkscreen-test;` appears exactly once. It stops with an error when `nm` fails or its output lacks a symbol every image has.

## How the test firmware differs from the reader

| Area | Test firmware |
|---|---|
| Boot | `setup()` runs unchanged up to the routing: release wake, reset loop, early wake decision, touch probe, boot cutoff, SD, settings. Then the splash, then the test menu, on every boot. When the menu first opens, the reader's files are compared with the P8 baseline (the first boot with a card takes it). After a panic, the crash screen first, then the menu. |
| What is built | The reader, library, home screen, file browser, image viewer, OPDS browser, the plugin catalog (with the plugin event and HTTP helpers), the reader's sleep screen and the web server (with WebDAV) are left out of the build (`build_src_filter`), so a live reference to any of them fails the link. The reader's plugin-event, loan-clock (`TrustedTime`) and Bluetooth page turner (`bleturner::begin()` and `tick()`) calls in `setup()` and `loop()`, the sleep entry and the Wi-Fi join are compiled out, and the two test envs build `lib/BlePageTurner` without its radio (`-UFREEINK_CAP_BLE_HID_HOST`: its no-radio port, the SDK's `BleKeyboardHost` stubs) and ignore NimBLE-Arduino, so no Bluetooth radio code (NimBLE, `BleKeyboardHost`) or its static initialisers are linked, only the module's small no-radio runtime (PlatformIO links library objects directly, so `[base]`'s NimBLE would otherwise keep about 32 KB of flash and 3 KB of RAM here). Settings, OTA and the SD firmware update are compiled but unreachable, and the linker drops them. `lib/Epub` and `lib/LibraryIndex` are still linked, because headers the test build shares include them: none of their functions stays, but seven static initialisers (the hyphenation registry, `ImageBlock`, `ImageDecoderFactory`, `library::libraryIndexPath` and, since 2026-10-19, `Epub`'s GIF generation mutex, `BookMetadataCache`'s size-window mutex and the SDK Opds library's `kNoValue`) run at boot, and the hyphenation tables they reference (about 351 KB of flash) stay in the image. Every reader entry point (`goHome`, `goToReader`, `goToSettings` and the rest) lands on the menu. |
| Settings | `/.crosspoint/settings.json` is read once, then these are pinned in RAM: night mode off, Sunlight Fading Fix off, portrait, Short Power Button = Ignore, Double-Click Power for Light off, light off with no restore on wake, the keys in their default roles, English, the Lyra theme, no SD reader font, and a 10-minute sleep timeout if it was Never. |
| Card writes | Never under `/.crosspoint/`: every settings, state, Wi-Fi, recent-books, OPDS and KOReader save is a no-op, `state.json` is not read, and the Quick Resume frame is neither written nor removed. The test firmware writes only `/.silkscreen-test/`: `results.csv`, the P8 baseline `crosspoint.sha`, the board options `options.cfg` (OPT), and scratch files ending in `.tmp`. |
| No card | The menu opens with "SD not mounted" and results go to the serial log only. The reader's SD-card error screen and its 60 s timeout are not in the test firmware. |
| Front light | Off at every boot and after every silent restart, with no light panel (gesture, status-bar tap). Only a front-light test lights it, after the operator names the load; T10's step 0 lights it with no load, opt-in. The over-voltage trip handling and the SDK's switch-on load probe are the reader's, but the "No front light detected" message is not shown: T9 and T10 report the probe's result themselves. T10 lowers the trip level in RAM for one run. |
| USB | While a USB data host is attached and the `USB_STAT` ladder reports USB power, auto-sleep is held off (the USB Serial/JTAG host frames, with a 2 s hold). A charger alone or a cell never holds it, and neither does a data link with no VBUS on the cell: the S3's USB Serial/JTAG enumerates without VBUS (HW-11). The 15-minute ceiling and the 60-minute input cap still apply whenever the `USB_STAT` ladder does not report external power, so a misread ladder shows up as a sleep during a bench session. |
| Sleep | The same sleep entry, wake source and timeouts as the reader, less its plugin-event delivery. The sleep screen shows the firmware and board tag, the MEAS-01 current targets and the pins to probe. Once its refresh has returned it times the RTC and keeps a record of the entry in RTC memory, for T14 and P5. A power-button click does nothing and a hold longer than 400 ms sleeps; waking needs POWER held. |
| Render watchdog | The reader's watchdog ([POWER.md AW-04](POWER.md#31-the-render-watchdog-aw-04)) runs in the test firmware with the reader's bounds: a test screen whose `render()` made no progress for 5 minutes on battery would end the session by the same restart, as would a sleep entry that took 3 minutes (none does: a scope loop is one refresh per `render()`, and U2 draws one frame per `render()`). [P9](#p9-render-watchdog) is the test that hangs one on purpose, a `render()` or the sleep entry, with a 30 s bound that it sets in RAM. |

## Using it

**The menu.** Up/Down or Left/Right move, Confirm opens. With touch, tap a row, and swipe to scroll. Back does nothing here. The line under the title says where results go (`/.silkscreen-test/results.csv`, or "SD not mounted") and shows the firmware version. Each row shows the test id and name, what it checks, and the last result this boot. The first row, OPT, holds the [board options](#opt-board-options): set them before the tests. With touch, a swipe up from the bottom edge returns to the menu from any test, except while T11's targets or gestures are on.

**A test screen** has the test id and name in the header, the last result recorded on this screen, the test's instructions and live readings, and four touch buttons along the bottom. The buttons sit in the order of the front keys and do what the keys do: Exit (Back), then the test's Confirm, Left and Right actions. Up/Down step values where a test has any. Back, the header's back button or Exit leaves the test. In a test that reads the keys itself (T3), a front key held 1.5 s does what the button above it does (Back: Exit), and the screen says so. In a test whose input is the touch panel (T11's targets and gestures), the touch buttons do nothing and the header has no back button: the keys drive the screen, and Back stops. Leaving any test switches the front light and the radio off.

**Questions.** When a result needs something only the operator can see (T2 and P4 ask whether D2 is lit), a list of answers opens: Up/Down and Confirm, or tap an answer. Back, or a tap outside the list, records nothing.

**Display runs.** T4, T5, T6 and T8 show full-screen frames. Confirm starts a run. Each refresh is one step, drawn and refreshed a loop pass after the one before, so Back stops a run between two refreshes. A frame that says "Any key or tap: continue" stays on the glass until you press a key or tap; Back stops there too. A tap in the top-left corner, where the header's back button sits, counts as Back. The touch buttons are not on the glass during a run and do nothing. The run ends with the test's screen repainted by a HALF refresh, which clears the last frame, and then a question about what you saw. A stopped run records nothing. T4's scope loops are the exception: any key or tap stops them, and they record however they end.

**Results.** Each result is shown on the screen, logged to serial and appended to `/.silkscreen-test/results.csv`:

- Serial: `[INF] [TEST] TEST,<id>,<verdict>,<details>`. A log line holds 256 bytes, so details past 200 bytes go on following `[INF] [TEST] TEST+,<id>,<more>` lines, in order.
- CSV columns: `timestamp,time_source,session,board,firmware,test,verdict,details`. The timestamp is the RTC's local time when the RTC holds a valid time (`time_source` = `rtc`), otherwise seconds since boot (`uptime`). `session` is random per boot, `board` is the end of the Wi-Fi MAC, and `details` is quoted, with `key=value` pairs by convention.
- Verdicts: `PASS`, `FAIL`, `WARN` (every check passed, but a reading sits outside its nominal band), `SKIP` and `INFO` (a reading with no pass criterion).
- A record that judges checks carries `fails=` with the failed ones joined by `+` (or `none`). On a FAIL the screen shows them on a line of their own ("Failed: ...") under the verdict, then the rest of the details, cut to fit; a WARN names its readings in `warns=` and shows them the same way ("Warnings: ..."). The serial log and `results.csv` carry the whole record (up to 383 bytes).
- Tests keep scratch files in `/.silkscreen-test/` with names ending `.tmp`. Any left behind by a sleep or a crash are removed at the next boot.
- P8 also records a row at every boot with a card (`when=boot`).
- P9 records a row the first time the menu opens after a render-watchdog restart (`action=watchdog`, `kind=render` or `kind=sleep`), and one when it is armed (`action=arm`).

## Bench bring-up (BRING_UP.md)

The hardware audit of 2026-09-30 (Rev 1.01) found no mismatch between the firmware and the board, and set out a bench procedure for the first boards: `de-link_pcb/docs/audit-2026-09-30/BRING_UP.md`, meter, supply and scope checks in order, with the steps that need firmware marked [FW]. This table maps each of its sections to what the test firmware runs for it; "meter only" or "scope only" means no firmware takes part. The [bring-up checklist](PORTING.md#bring-up-checklist-real-board) names the same sections on each B-step.

| BRING_UP section | In the test firmware |
|---|---|
| §0 Tools | Meter and scope only. A 10 MΩ meter reads the 1 MΩ-class nodes (`USB_STAT`, `BAT_MONIT`, `LED_MONIT`) low, so compare T1 with P+ at `J5`, not with a meter on `IO8`. |
| §1 Visual and orientation | Loupe only. A reversed `D2` shows later as `D2` dark on USB, which T2 and P4 ask about. |
| §2 Unpowered resistance | Meter only. |
| §3.1 USB input, no cell | The rails: meter only. T2 stage 6 (ExternalIdle above 2600 mV, `D2` lit); T1 on USB with no cell (P+ about 0 V, and the reader's label "No battery"); P4 stage 2 (USB power, no host). |
| §3.2 USB hot-plug transient | Scope only. |
| §3.3 Battery input | The current and the 4.20-3.30 V sweep: supply and meter. The brown-out sweep [FW]: [U1](#u1-wi-fi-tx-bursts) on a bench supply named on its screen, down to the reader's cutoff only: about 3.3 V as read on P+ with the radio on, where the board sleeps (`reset=DEEPSLEEP` after the wake). TX below it, in the 3.2-3.3 V band where BRING_UP expects the sag, is not covered. A brown-out shows as `reset=BROWNOUT` in P1 and U1 only when P+ is still at about 3.4 V or more as the board reboots (a current-limited supply, say); one below that is followed by the boot cutoff's sleep, so only the scope at `U3.5` and the ROM's reset line on the UART log show it. The optional USB sweep with a real cell: T2 stage 3 (weak USB, about 950 mV). |
| §4 Battery only: rails and back-feed | [T2](#t2-charge-status) stage 1 (HW-08): OnBattery, `D2` dark, and 10 samples since the last change (`hw08_clamp` for any at or below 1350 mV, the charging or complete band; WARN `partial_clamp` for 1350-2100 mV, `above_nominal` for 2300-2600 mV, and `st_not_asserting` above 2600 mV, where `ST` is not asserting: `U2.8`/`R17`). T1: P+ against the meter (`BAT_MONIT` = P+/2). T9: the dark `LED_MONIT` reading, 100-700 mV at the pin. The deep-sleep back-feed probes [FW]: [P5](#p5-sleep-wake-and-sleep-current) (across `R5`, across `R34`, the `J2` flex in and out). The other node voltages: meter only. |
| §5 Charging | T2 stages 2-4 (charging, weak USB, complete) and stage 5 (the cell pulled on USB: the blink's period and duty, or a steady 3.3 V, from its 12 s capture). T1 on USB with a cell (P+ follows the charger). The charge current, U11's temperature, the float voltage and the protection trips: meter, supply and thermocouple only. |
| §6 Sleep current [FW] | P5: every sleep path, the current judged against the [POWER.md §1](POWER.md#1-sleep-current-budget) budget, what a high reading points at, and the pins to probe, listed on the sleep screen. T12: the touch tail's current. The cutoff paths, forced panics and the SD-error timeout: the [reader image](#checks-that-need-the-reader-image). |
| §7.1 Flex map | Bench supply only, board unpowered, before T9's first Confirm on a `J3` flex (a reversed map must never be enabled; [PORTING.md B8](PORTING.md#bring-up-checklist-real-board)). |
| §7.2 First enable [FW] | [T9](#t9-front-light): the enable sequence for the scope, every brightness step, and each string's `LED_SW` at 100 % and about 10 % duty (`vf=`, `dvf=`). Across `R37`: meter. |
| §7.3 Colour change [FW] | T9's switch run: 30 of the reader's colour changes at about 100, 50 and 10 % duty. |
| §7.4 Open load [FW] | [T10](#t10-front-light-over-voltage-trip) step 0, opt-in: the one deliberate unloaded enable (below). |
| §8 E-paper power [FW] | [T4](#t4-refresh-modes)'s scope loops (FULL, HALF, FAST). The `3V3` rise at plug-in: scope only. |
| §9 RTC and miscellaneous | RTC: T13 (the oscillator starts at the first I2C write: OSF, then set the time) and T14. Power button: T3 (`power_not_low`, and the meter check at `R76` pad 1) and P1/T3 (`pwr_idle`). QR codes: a phone. UART: the serial log. LDO temperature [FW]: [U2](#u2-sustained-load). SD switch-on [FW]: [P3](#p3-sd-card)'s power cycles. |
| §10 Cut-down boards | [OPT](#opt-board-options) first (Cut-down board, which also sets UP(2) is POWER). T3: `SW7` as POWER on `IO18` (`up2pwr=`), and `up2_window` if UP(2)'s window still fires on `IO4`. P1 and T3: `power_idle` for `R72` fitted with `R36` still in. T10 step 0: with no `D3`, U10's OVP is the only limit, and the TPS923611 swap is refused. P2 and T9 record `j6=n/a`. The cut and the sealed edge: no firmware. |

**Board options.** What the firmware cannot see about the build, the operator sets in [OPT](#opt-board-options) before the tests: **Cut-down board** (no `J6`, no `SW10`) and **UP(2) is POWER** (`R72`/`R74` in, `R36`/`R73` out). They are kept in `/.silkscreen-test/options.cfg`; without a card they last until the next restart.

**Bench utilities and their limits.** Each runs for a bounded time, holds auto-sleep off only while it runs, and leaves the radio off, the light off and the card mounted on every way out, except a P3 power-cycle run whose remount fails, which leaves the card parked and gated off, as at a boot without one.

| Utility | BRING_UP | Supply and load | Bound | Stops on |
|---|---|---|---|---|
| T4 scope loop | §8 | any | 5 min or 1000 refreshes | any key or tap (held until the refresh in progress ends) |
| T9 switch run | §7.3 | any; lit on the load the operator named | 30 colour changes, about 35 s | Back; the first trip, a light that goes out, `latched_off`, `over_22v` |
| T10 step 0 | §7.4 | any; no load, opt-in | one switch-on at 10 %, then the 10 s connect wait; about 15 s | its own end, or Back |
| P3 power cycles | §9 | any | 10 cycles, about 20 s | Back or leaving (the card is mounted again first); a failed mount |
| U1 TX bursts | §3.3 | USB; a bench supply named on screen (below the 3.4 V Wi-Fi gate too); a cell at 3.4 V or more | 1, 2 or 5 min of 1 s TX and 1 s quiet | Back; USB lost on a USB run; the cell at 3.3 V; on a bench supply the reader's cutoff (about 3.3 V on P+) |
| U2 sustained load | §9 | USB only, checked every second | 5, 10 or 20 min | any key or tap; USB lost |
| T2 stage 5 capture | §5 | USB, cell pulled | 12 s at 20 Hz, once per Record | Back (records nothing) |

**Dated exceptions.** Three tests step outside a rule on purpose, and each is recorded in [REQUIREMENTS.md](REQUIREMENTS.md):

- T10 step 0 enables the boost with `J3` and `J6` empty, once, after the operator names the driver build and confirms both empty (`fl-no-open-load`; see [Safety](#safety));
- U1 transmits below the 3.4 V Wi-Fi gate, down to the reader's cutoff, on a bench supply the operator names in place of the cell, for the brown-out sweep (`bat-wifi-gate`);
- T2 stage 5 reads `USB_STAT` at 20 Hz for 12 s to time the blink (`usbstat-slow-sampling`).

## Tests

The ids are stable; `results.csv` and the serial log use them. Every test in the menu is built; none has run on a board yet. Each section gives the purpose and the bring-up step, the steps, what the screen shows and the PASS criteria. The thresholds marked "proposal" are to tune after the first board.

| Id | Test | Bring-up step | Status |
|---|---|---|---|
| OPT | Board options: cut-down board, UP(2) as POWER | - (set before the tests) | [Built](#opt-board-options), not yet run on a board |
| P1 | System info and boot checks | B1 | [Built](#p1-system-info-and-boot-checks), not yet run on a board |
| P2 | I2C bus scan | B7, B9 | [Built](#p2-i2c-bus-scan), not yet run on a board |
| T1 | Battery (`BAT_MONIT`) and cell detection | B6 | [Built](#t1-battery), not yet run on a board |
| T2 | Charge status (`USB_STAT`) and USB presence | B6 | [Built](#t2-charge-status), not yet run on a board |
| P4 | USB presence and USB Drive | B4, B6 | [Built](#p4-usb-presence-and-usb-drive), not yet run on a board |
| T3 | Keys: eight ladder keys and the power button | B5 | [Built](#t3-keys), not yet run on a board |
| T4 | Refresh modes | B2 | [Built](#t4-refresh-modes), not yet run on a board |
| T5 | Grayscale | B2 | [Built](#t5-grayscale), not yet run on a board |
| T6 | Inversion | B2 | [Built](#t6-inversion), not yet run on a board |
| T7 | Orientation and bezel ruler | B2 | [Built](#t7-orientation-and-bezel-ruler), not yet run on a board |
| T8 | 40 MHz panel SPI trial | B2 | [Built](#t8-panel-spi-clock), not yet run on a board |
| T9 | Front light | B8 | [Built](#t9-front-light), not yet run on a board |
| T10 | Front-light over-voltage trip | B8 | [Built](#t10-front-light-over-voltage-trip), not yet run on a board |
| T11 | Touch probe, mapping and gestures | B9 | [Built](#t11-touch-probe-mapping-and-gestures), not yet run on a board |
| T12 | Touch sleep command | B9 | [Built](#t12-touch-sleep-command), not yet run on a board |
| T13 | RTC | B7 | [Built](#t13-rtc), not yet run on a board |
| T14 | RTC across deep sleep | B7, B10 | [Built](#t14-rtc-across-deep-sleep), not yet run on a board |
| P3 | SD card | B4 | [Built](#p3-sd-card), not yet run on a board |
| T15 | Wi-Fi and NTP | B7, B6 | [Built](#t15-wi-fi-and-ntp), not yet run on a board |
| U1 | Wi-Fi TX bursts (bench tool: the brown-out sweep) | B6 | [Built](#u1-wi-fi-tx-bursts), not yet run on a board |
| U2 | Sustained load (bench tool: the LDO's temperature) | B6 | [Built](#u2-sustained-load), not yet run on a board |
| P5 | Sleep, wake and sleep current | B3, B10 | [Built](#p5-sleep-wake-and-sleep-current), not yet run on a board |
| P8 | Reader-data integrity | - (guards the reader's card) | [Built](#p8-reader-data-integrity), not yet run on a board |
| P9 | Render watchdog: a `render()`, or a sleep entry, that never returns | - (a firmware rule, POWER.md AW-04) | [Built](#p9-render-watchdog), not yet run on a board |

### Variant coverage

This table lists the variants the firmware detects ([MODULARITY.md](MODULARITY.md)) and where the test firmware reports each one. Run each check on the variants you have; one board seldom has them all.

| Variant | Found by | Reported in | Cases to try |
|---|---|---|---|
| Panel family: 4.26" or 3.68" X3 panel | The build (board tag: `silkscreen-test` or `silkscreen-368-test`) | P1: profile and board tag; on the 3.68", `x3_mode=` and `geom=` (FAIL `x3_mode`, `geometry`) | Each image on its own panel |
| Display controller, 4.26": SSD1677 or UC8279D | The SDK probe before `display.begin()` | P1 (`action=panel`); the `panel=` token in P1's boot row and in every T4 row | Each panel, after a cold boot (`source=probed`) and after a wake (`source=cached`) |
| Display controller, 3.68": the UC8279d, and nothing else | Nothing: the build's profile fixes it, and nothing is probed (no VER read at a cold boot or a wake) | P1 (`action=panel`: `controller=UC8279 source=fixed probe=none`, `driver=`, `x3_mode=`, `geom=`, `seq=`); the `panel=` token (`UC8279:fixed:-:-`) | Each panel, after a cold boot and after a wake (the same row: nothing was read). A panel that is not a UC8279d is not supported: it shows as a blank or garbled picture and BUSY times that make no sense (T4), not as another P1 row |
| Touch tail: none, FT6336U, or GT911 (alternate-jumper build) | The probe and its chip-id check | P1 (`touch_id=`), P2, T11 (`touch_id=`, `gt911_fallback=`) | No tail; an FT6336U tail; a `J6` device at 0x38, which must be refused |
| Touch, 3.68": none in the build | `FREEINK_CAP_TOUCH=0` | P1 (`touch_id=no_touch`, "none in this build"); T11 and T12 (SKIP, `reason=no_touch_in_build`) | A logic analyser at boot and at sleep entry |
| RTC: U13, U14 or none | The DS3231 pattern at 0x68, then 0x51 | P1 (`rtc_id=`), P2, T13 (`rtc_id=`, `pattern=`) | The fitted part; a `J6` device at 0x68, which must be refused |
| Key ladders | The presence test at a reset | P1 (`ladders=`, and the readings with each pull); T3 (`ladders=`, and the keys it leaves out) | Both fitted; one built without its pull-up |
| Power button idle level | `IO18` read on the ADC at a reset | P1 and T3 (`pwr_idle=`, fail `power_idle`) | Normal; `R72` fitted with `R36` (a rework only) |
| Board build: full length or cut down; UP(2) as POWER | The operator (OPT): the firmware cannot see either | T3 (`up2pwr=`, `up2_hits=`, fail `up2_window`); P2 and T9 (`j6=n/a`) | A full board; a cut-down board (UP(2) as POWER by default); a full board reworked for UP(2) as POWER |
| Front-light load and strings | The switch-on probe | T9 (`probe=`, `cool=`, `warm=`, fail `no_light`); T10 (`probe2=`, `probe2_dark=`, `probe2_strings=`, `probe2_load=`, `restored_load=`) | A `J3` light; a `J6` pair; a one-colour `J6` strip. No light at all: T10's step 0 (`step=0`, `strings=`, `load=`), and on the reader image ([PORTING.md B8](PORTING.md#bring-up-checklist-real-board)). |
| Front-light driver: TPS923610, or the TPS923611 swap | The operator (T10's step 0 asks): the firmware cannot see it | T10 step 0 (`build=`; the swap refused unless `D3` is an SMAJ33A) | The standard build; the swap with an SMAJ33A |

### The 3.68" variant (`silkscreen-368-test`)

The same suite on the 3.68" X3-panel build (no touch, the SDK's X3 UC8279d driver; [MODULARITY.md](MODULARITY.md#the-368-variant-silkscreen-368)). Everything that is the board's runs as it does on the 4.26" one (the keys, the battery and charge tests, the card, the RTC, Wi-Fi, the front light, the sleep paths, P8 and P9, the render watchdog, whose three hangs are the same on this panel); these differ:

| Test | On the 3.68" build |
|---|---|
| T11, T12 | One screen, "No touch in this build". It records a SKIP (`touch=none reason=no_touch_in_build cap_touch=0`) when it opens, and names the logic-analyser check. The touch tests and every `touchDiagnostics()` read are not compiled. |
| P1 | The panel row says what the build knows, not a reading of the panel: `controller=UC8279 source=fixed probe=none variant=00 driver=Uc8279Driver x3_mode=1 geom=792x528` and `seq=` (the CDI byte the last refresh reported, `97` for the first refresh after `begin()` and `D7` after, or `-` before any refresh). There is no `ver=`, no `verdict=` and no `BUSY_N` reading: the build reads no VER, at a cold boot or at a wake (the module marked `SEO368NQW25` read `00 0F 00` through the stock X3 probe of the earlier build, a read this build no longer takes). `driver=` is the facade's rule and `geom=` the configured geometry (nothing reads the glass back). INFO when consistent; FAIL `x3_mode`, `geometry` (the facade, its X3 constants and the profile disagree), `controller` (the profile's controller is not the UC8279d) or `driver` (`seq=` is not the UC8279d's CDI byte: the last refresh ran another driver). **An INFO means the build is consistent, not that a panel is there**: with the flex out the row still says INFO. A panel is there when T4's FULL shows `busy_ms` in the hundreds and the picture changes. The touch id reads `no_touch`, and the touch row "none in this build". |
| T4 | The SSD1677's reference BUSY times (FULL 1800, HALF 1720, FAST 500 ms), the FULL >= HALF > FAST order and "promoted" are not judged, in the lettered runs or in the scope loops: the rows say "no reference yet, record it", and `x4_ref=none`. **What the UC8279d driver does** (`Uc8279Driver::displayStart()`): FULL and HALF both run its one GC bank (it has no half bank) and are reported as asked; FAST runs the DU bank only when the controller holds a previous frame, and is reported FAST. A FAST runs the GC bank, and is reported FULL, for the first two content paints after `begin()` (the driver's boot budget) and for the paint after a resync request. `HalDisplay` turns every HALF request into `requestResync(1)` on the X3 panel: the next refresh is the same GC refresh, still reported HALF, with no conditioning pass. So the three scope loops show two waveforms, not three (GC for FULL and HALF, DU for FAST); FULL and HALF times should be about equal, so "FULL >= HALF > FAST" can read no by noise, and it is recorded, not judged. After the splash and the menu's first paint have spent the boot budget, a FAST reports FAST; `promoted=` counts a FAST that ran GC (after a resync request, or a gray pass left without its cleanup). The BUSY window (100 ms to 30 s), async and turn-off are still checked. The window's floor is the SSD1677's. The UC8253 X3 driver's header quotes about 130-770 ms for its waveforms, and the UC8279d's DU time is not in hand, so a healthy FAST that reads `busy_window` is possible: read the real time from the serial `T4 fast 1/6: ... BUSY N ms` line (the record's `busy_ms` averages only the refreshes inside the window). With the flex out the refresh waits give up at about 70 ms (a 50 ms confirm loop and a 20 ms edge wait: computed from the wait loops), which is what `busy_window` is there to catch. |
| T5, T6, T8 | The UC8279d driver reports its B/W refreshes only, so the gray waveform and the settle passes read "not reported" (`gray=not_reported`), where they read BUSY 0 ms and "skipped". The checks that need a record are not made (`gray_skipped`, `gray_busy`, `turn_off`, a promoted base, and T6's gray-while-inverted detection): a reported B/W refresh must still end inside the BUSY window, and the operator's look at the chart is the verdict. A `displayGrayscaleBase()` route fires no refresh the driver reports on its differential path: its base row reads "not reported". **T5's first route is not a reader route on this driver**: it takes no async base (`asyncBase` is false), so the reader never overlaps the base with the plane render there; its text pages take `displayGrayscaleBase(FAST)` and then the strips (route 3's call, which T5 runs with the Fading Fix on), its cleanup pages route 4 and its image pages route 5. Route 2's on-screen label ("Text page, 80-row strips", "the reader's strip path") is the 4.26" board's: on this driver its sequence, a plain FAST base and then the strips, is the reader's image-page sequence (route 5's), and the reader's text page is route 3's call. Judge routes 2-6; route 1 is a driver trial, and a garbled route 1 is not a reader fault. |
| T7 | Unchanged. Look for the orientation the case drawing gives (the flex at the bottom): the X3 driver cannot mirror. |
| T8 | The profile clock is the X3's 10 MHz and the screen opens on it (the 4.26" board's opens on the 40 MHz trial); the 26.7 and 40 MHz trials have no reference. The record and the screen print the FAST, HALF and FULL columns, `n/a` for a mode no refresh ran in, which is not a measurement of 0: the UC8279d reports a HALF turn as HALF (its one GC bank) and a FAST that ran GC as FULL, so a FULL column can hold such turns. A plain-page promotion is recorded, not judged (as in T5). Run the profile clock first. |
| P5, sleep screen | No touch lines. The 58-73 µA typical, the 95 µA flag and the 110 µA limit are the board's with the 4.26" panel's 2-6 µA in them, and say so: the Expected row reads "Expected, 4.26" panel in it", the record carries `limit_basis=4.26in_panel`, and the J2 flex gap is recorded with no reference (the panel's deep-sleep current is not in hand): its class reads `backfeed` for 100-300 µA (the board's) and `recorded` otherwise. |

**Bench list.** In order. **What has run on a board (the owner's facts as of 2026-10-12).** One module (`SEO368NQW25`) has run, in the owner's first session (2026-10-11, a CrossPoint build): the stock X3 probe read `VER=00 0F 00`, the UC8253 default recipe left it blank (`Wait complete: X3_PON (41 ms)`), and with the UC8279d driver forced by a local build flag it displayed a picture. That forced build then froze (no panic, no watchdog) on its first grayscale image page (`[IMG] Rendering image at 214,443 ... (100x20)`). The code reads as a bus transaction held across a bus command (a deadlock on arduino-esp32's non-recursive SPI mutex): that cause is inferred from the code, not from a backtrace. Fixed upstream: Free-Ink PR #139 (`c2d711a`, merged as `7fcd3b3` on 2026-10-12, the same one-line change); CrossPoint #3960 (`93a14afd`) bumped the pin. Our own gated copy (`4988be4`) was reverted by SDK `b460001` to take upstream's text, and the C3 X3 build has the fix too. The 2026-10-12 builds show it in their disassembly (CrossPoint's `silkscreen-368` and `c3check`, CrossInk's `silkscreen-368`: `Transaction::end()` ahead of `EpdBus::cmd(0x92)` in `writeGrayscalePlaneStrip()`); not yet seen on glass by us (the owner has not yet flashed it). The owner also reported a quiet, harsh buzz at every display refresh, from the e-paper boost stage; it is unexplained (step 7 has the questions), and the boost inductor `L1` (47 µH, sized to the SSD1677 reference and unverified for this panel: HARDWARE.md §6.2) and the 4.7 µF / 50 V X5R MLCCs `C11` and `C13`-`C17` are the usual singers. Nothing else has run on a board, and CrossInk's variant images have never been flashed. Every figure marked *inference* or *computed* below comes from a datasheet, a netlist or the driver code. It is written for the cut board (the x3 build: Rev 1.01 cut at its cut line, no `SW10`). A full-size board skips step 1, sets neither of OPT's options, and has a POWER key throughout; the other steps are the same. Steps 1-5 are the first-power gate, after BRING_UP §1-§3.3 has passed with `J2`, `J3` and `J4` empty (a Rev 1.0 board also needs §10's 3V3 and GND wires): nothing after them is judged until they have passed.

1. **Power key, cut board only (no firmware).** A cut board has no POWER key until UP(2) is made POWER, and every step that sleeps or wakes needs one. Without the rework nothing can be slept from the keys (the auto-sleep timeout still can) and the board leaves deep sleep only through `SW11`, which is a reset, not a wake (`esp_reset_reason()` is not `DEEPSLEEP`, so the deep-sleep wake path, P5's entries and the ghost and release wakes are never exercised). Do it first: remove `R36` and `R73`, then fit `R72` (10 kΩ) and `R74` (0 Ω). Never `R73` with `R74`, never `R72` with `R36` still in (HARDWARE.md §9.2, BRING_UP §10). Check `R76` pad 1 (`IO18`): 0 V idle, about 3.0 V with `SW7` (UP(2)) pressed (1.76 V idle means `R36` and `R72` are both fitted). A full-size board keeps `SW10`: skip this step.
2. **The flex (no firmware, cell and USB out).** `J2` is a Hirose FH34SRJ-24S-0.5SH, 24 contacts at 0.5 mm; "-RJ" is top and bottom contact, so either face of the tail may go up (PCB final review §06). It takes a tail 0.30 ± 0.03 mm thick; the 3.68" drawing gives the stiffener end as 0.30 ± 0.05 mm, so a nominal tail fits and a thick one may not (*inference*): try it dry and never force it. Conductor 1 must land on `J2` pad 1, the low-x end (x = 67.74 mm; pad 24 at 79.24 mm; the back silkscreen dot at 67.74, 123.20). The intended build is the panel face up on the top of the board, the tail down the slot, folded 180° under the board and inserted travelling toward -y (the latch is hinged on the `D6` side). In the 3.68" drawing's front view, with the tail pointing down, "1" must be on the left, as for the GDEQ0426T82 (*inference*: that drawing is not in these repos). With the tail latched and the board unpowered, ohm out tail contact 1 to `J2` pad 1 (final review EPD-01). A reversed tail puts 3V3 (`J2` 15 and 16) on the panel's RES# and BUSY pins, an output, if the 3.68" pin table is in the GDEQ0426T82's order (final review EPD-02; *inference* for this panel). The flex goes in and out only with the cell and USB disconnected: 3V3 is live through deep sleep (PORTING.md B2).
3. **Unpowered readings, flex in, cell and USB out.** `U3.5` (3V3) to GND is not a short (above 1 kΩ, usually tens of kΩ and climbing: BRING_UP §2). `C14.2` (PREVGH), `C16.2` (PREVGL), `C13.2` (VSH1), `C15.2` (VSL) and `C20.2` (VCOM) each to GND are not a short: write the five readings down, this panel's are in no repo. `J2` pad 8 (BS) to GND is the board's own tie.
4. **First power.** With the flex out, flash the image first: `pio run -e silkscreen-368-test -t upload`, card in (FAT32; results go to `/.silkscreen-test/results.csv`, with no card only to the serial log). Then unplug, seat the flex (step 2), and power from USB alone, no cell, through the inline USB-C meter or a bench supply into a breakout, **limit 500 mA** (BRING_UP §3.1's second step; its first step, 100 mA, is for `J2` empty, and with the panel in a boost pulse may fold a 100 mA limit back and look like a fault: *inference*). **Arm the scope before plugging in**, 10x probes rated 50 V or more (two rails are negative; BRING_UP §8): CH1 on `U3.5` (3V3), single shot on the rise, since a rise faster than about 50 µs lets `L1` (47 µH) and `C14` ring toward 0.8-1.0 A (HARDWARE.md §6.2); CH2 on `C14.2` (PREVGH, about +22 V) for the first boot, then one rail per boot: `C16.2` (PREVGL, about -22 V), `C13.2` (VSH1, about +15 V), `C15.2` (VSL, about -15 V), `C20.2` (VCOM, no value in the docs: record it), `Q4.1` (GDR) and `R14.2` (RESE, peaks well under 2.4 V). The first boost run is the boot splash, a few seconds after power-up. Expect tens of mA at idle (BRING_UP §3.1's estimate) and tens of mA more during a refresh. Stop and write down: a 3V3 rise faster than about 50 µs, 3V3 below 3.0 V, GDR or RESE running away or ringing, the supply at its limit, a hot `U3`, `L1`, `Q4` or flex.
5. **The first boot, and a panel that stays blank.** What the code does at a cold boot (computed, not measured): `display.begin()` (an 80 ms reset and a register script; nothing is read from the panel), then the splash (the first paint: about 140 ms of SPI at 10 MHz for its three planes, then a GC waveform whose time on this panel is not in hand: the UC8253 X3 driver's header quotes about 130-770 ms, and the UC8279d's own times are not in the repo), then the test menu with OPT as row 1 and P1 as row 2, a second GC paint (the driver's boot budget makes the first two content paints GC). The panel flashes a few times and settles. If it is still blank after about two minutes:
   - Power off, and do not reflash. A `BUSY_N` held LOW makes every driver wait run to its 30 s ceiling (PON and DRF: about a minute for the first paint, then 30 s per screen); with the flex out the PON wait gives up after about 1 s and the refresh wait after about 70 ms, so the first screen takes 1-2 s (computed from the wait loops).
   - **The blind route.** The board is running with no picture. After the first paint's waits have run out, RIGHT once moves from OPT to P1 and OK opens it (in the menu RIGHT is Down); it records the `action=panel` and `action=boot` rows with no picture. Power off and read `results.csv` on a PC: `controller`, `driver` and `seq` are in the panel row (there are no VER bytes: the build reads none). The panel row is also on the serial log whenever a monitor is attached as P1 opens.
   - **The serial line.** `silkscreen-368-test` has no USB-serial wait (below), so a monitor attached after the reset misses the boot lines. The dev reader image `silkscreen-368` has it (250 ms, log level 2): flash it and read `[XTDET] controller forced by the build: UC8279 (not probed)`, then `Panel controller UC8279 (forced by the build)`, then the `Wait complete:  8279_PON (N ms)` and `8279_DRF (N ms)` lines (two spaces after the colon: the driver's tag has a leading space, so grep `8279_PON`, not the quoted string; the first session's UC8253-recipe PON wait, `X3_PON`, read 41 ms). The wait lines are `Serial` prints, so a monitor must be attached when they are printed, and the PON one comes only at the first paint after a `begin()`. A panel that answers prints them with a time; no such line means `BUSY_N` never fell (no panel, or not seated); about 30000 ms means it is held LOW. There is no `[XTDET] X3 stock probe` line: the build reads no VER.
   - **The rails with no picture.** T4's scope loops run blind: from P1, BACK, RIGHT six times (P2, T1, T2, P4, T3, T4), OK; in T4 LEFT three times is "FULL, scope loop" (two: HALF, one: FAST), OK. Any key stops it.
   - Then step 3's readings again, and step 2.
6. **Set OPT, then open P1.** Before any test: OPT's "Cut-down board" on a cut board (it sets "UP(2) is POWER" too; T3 then counts `SW7` as POWER and `up2_window` stays quiet). The controller is not identified: the build drives every unit with the UC8279d driver. Open P1 after a cold boot:

   | P1 `action=panel` row | Means | Next |
   |---|---|---|
   | `controller=UC8279 source=fixed probe=none variant=00 driver=Uc8279Driver x3_mode=1 geom=792x528 seq=<97, D7 or ->`, INFO | the build is consistent; nothing was read from the panel | go on: the picture, and T4's BUSY times, say whether a panel answers |
   | the same, picture blank or garbled | no panel answering, a flex not seated, or a panel that is not a UC8279d (not supported) | power off; the rails (step 5), then step 2 and the flex |
   | T4 `fails=busy_window` with `bad=` equal to the run's refreshes and `busy_ms=-` (an out-of-window time is counted, never averaged; the screen's BUSY row reads 0 / 0 / 0 ms; the time itself is only in the serial log, `T4 full 1/6: ... BUSY 71 ms`) | no `BUSY_N` edge: panel absent or not answering; on a FAST row alone it can also be a real DU under 100 ms, which the serial BUSY time tells apart | as the row above; if `BUSY_N` is held LOW every wait runs to 30 s: wait or power off, do not reflash |
   | FAIL `driver`, `x3_mode`, `geometry` or `controller` | the facade, the profile and the driver disagree: a build or SDK fault, not the panel | stop; report the row |

   **Nothing to force.** The 3.68" variant supports the UC8279d only (owner decision, decided 2026-10-11, made 2026-10-12): the build has no `display_flags` hook, `-DSILKSCREEN_DISPLAY_CONTROLLER=uc8279` is accepted and changes nothing, and `=uc8253` and `=ssd1677` stop the build with the reason. A panel that is not a UC8279d is not a case this firmware handles: record the P1 row and the picture instead.

   **The serial wait, opt-in.** The 4.26" test firmware has none so that its wake timing is a release build's (P5's ghost-wake check judges 0.3 s), and the 3.68" one is the same. To catch the boot lines with a monitor, add to `platformio.local.ini`:

   ```
   [silkscreen368]
   test_serial_flags = -DCROSSPOINT_WAIT_FOR_USB_SERIAL
   ```

   and every boot of `silkscreen-368-test`, ghost wakes included, takes 250 ms longer: do P5's ghost-wake and release-wake checks without it.
7. **Boost.** T4's scope loops (FULL, HALF, FAST) with BRING_UP §8's probes: PREVGH `C14.2`, PREVGL `C16.2`, VSH1 `C13.2` (VSH2 is `C17.2`), VSL `C15.2`, VCOM `C20.2`, GDR at `Q4.1` and RESE at `Q4.2` / `R14.2`. The parts are the SSD1677 reference (`L1` 47 µH, `R14` 2.2 Ω, `Q4`); this panel's reference values are not in any repo, so record each rail and compare with the 4.26" board on the same probes. On the UC8279d the HALF loop is FULL's GC bank again (T4 above): two waveforms are scoped, not three. The X3's own waveform banks were tuned on the X3's boost: T4 and T5 must be clean in every mode. **The buzz.** The first session heard a quiet, harsh buzz at every display refresh, from this stage (unexplained: `L1` and the 4.7 µF / 50 V MLCCs `C11` and `C13`-`C17` are the usual singers). T4's scope loops run the refreshes back to back, which is the easy way to listen. Three questions for the owner, each a minute's test: does the 4.26" panel do it on this board; does it persist with the front light off; does it last the whole refresh or only its start and end.
8. **A wake.** Hold POWER longer than 0.4 s to sleep, wake with POWER held until the splash, open P1: `reset=DEEPSLEEP wake=EXT1` and the cold boot's row (`source=fixed`): there is no verdict to keep, check or drop, no probe at a wake and no `BUSY_N` guard that restarts the boot. The wake is `begin()` and a first paint (POWER.md's awake table). Watch the first few wakes; not measured. **Then repeat the power-up and the wake ten times each**, one of each at a low cell voltage (a bench supply on `J5` at about 3.5 V, just above the reader's 3.4 V cutoff, below which a boot sleeps at once): every one must end in a picture. The first session showed a picture after a cold power-up (the one 80 ms reset, then the init script) and nothing about a wake: it did not show that a deep-sleep wake repaints on this module. `Uc8279Driver::begin()` gives its controller an 80 ms reset and then writes its script with no `BUSY_N` wait or read-back, so a lost command is silent (a blank panel, no log line); if one cycle in ten comes up blank, that missing gate is the first suspect. The fix, a bounded 300 ms wait for `BUSY_N` HIGH after `bus.reset(50)` in `Uc8279Driver::begin()`, under `FREEINK_SILKSCREEN_368`, is not implemented: the owner has not asked for it.
9. **Panel deep-sleep current and the `BUSY_N` level.** P5 with the `J2` flex in and out (POWER.md row 17 and §4): the gap is this panel's draw once the `R5` test has ruled back-feed out. With the cell and USB disconnected pull the flex, then connect the cell through the meter, boot, sleep, measure; disconnect, seat the flex, connect, boot, sleep, measure: never swap the flex asleep. IO48's level to GND in sleep is not in hand for the UC8279d: note it (and, separately, the voltage across `R34`, about 0 mV: each 1 mV is 30 µA into IO48). Record the gap; there is no reference to judge it by. P5's `current` check FAILs above 110 µA with `limit_basis=4.26in_panel`: on this panel a FAIL `current` may be the panel's own draw, so read the flex gap before treating it as a board fault. **Awake, with a page on screen.** The UC8279d stays in PON between refreshes unless the reader asks for a turn-off (the Sunlight Fading Fix is what it passes as `turnOffScreen`, and it is off by default): the panel's analog then stays up until the next sleep entry, and its PON-idle current is not in hand. Measure the panel's share of the 3V3 current (the flex in against out, as above) with a page on screen after a normal refresh (PON idle), and again after a refresh that ended in a power-off (Sunlight Fading Fix on). Record both; there is no reference ([POWER.md](POWER.md#the-368-variant-silkscreen-368)).
10. **The 60 ms driver-free settle.** `sleepPanelWithoutDriver()`'s reset-loop path (after 3 abnormal resets in a row) is covered only by a host test over a recording GPIO model (`run_sleep_panel.py` in the SDK), and has never run on a board. No image forces a panic: P6 is not built and the images carry no panic hook, and none is added to a shipping image. To exercise it, make a scratch copy of the tree, add `abort();` as the first statement of `loop()` in `src/main.cpp`, build and flash `silkscreen-368` (never commit it), and boot: three panics, then the fourth boot sleeps at once (`3 abnormal resets in a row; sleeping until the power button`, `Panel reset and asleep`). The panel has been refreshed fast before the crashes, which is the case that path is for. Measure the floor of that sleep against a menu sleep; they must agree. Restore `main.cpp` and reflash. That sequence is read from `sleepIfResetLoop()` and `countAbnormalResets()`, not run. Without such a patch this item cannot be run.
11. **Orientation.** T7: the text upright against the bottom keys with the flex at the bottom, and the `{0,0,0,0}` insets against the case window. A rotated mounting needs a driver change.
12. **No touch.** A logic analyser on `IO38`/`IO39` from reset release through the first screen and across a sleep entry: no transaction to `0x38`, `0x5D` or `0x14`, `IO11` HIGH and still, `IO41` never driven. (P2 and T13 do scan the bus on purpose; keep them out of this capture.)
13. **Gray.** T5: the UC8279d driver's LUT header says "pending hardware validation". The first session's grayscale image page froze the device (the code reads as a transaction held across a bus command in the strip upload; fixed in the code by upstream's change, Free-Ink PR #139, in SDK `7fcd3b3`, which this image carries; our own gated copy `4988be4` was reverted for it; not yet seen on glass), so T5's strip routes (2-5) are the first run of that path with the fix: run one, and expect it to finish. Look at the four levels and the plain page after it; the log cannot judge them. Judge routes 2-6 (T5 above). Then reopen the page that froze in the reader image `silkscreen-368` (the page whose log line read `[IMG] Rendering image at 214,443 ... (100x20)`, with Text Anti-Aliasing on): it runs the strip path with the fix for the first time on this module, and its tone separation is unknown until run.
14. **Clock.** T8: the profile clock first (the screen opens on it), then 26.7 MHz over `R29`-`R34` (33 Ω) and the longer flex; the 10 MHz is the X3's proven value.
15. **Sleep current and the log.** MEAS-01 with the release image `silkscreen-368-gh_release` (never the dev env, which waits 250 ms at every boot), then the A/B of [BATTERY-LOG.md](BATTERY-LOG.md#9-bench-proof) with `silkscreen-368-batlog`.

**What settles the open items on this module** (the stage review's list, in order; each is also in its step):

1. First boot of the reader image `silkscreen-368` with a monitor attached (step 5): `[XTDET] controller forced by the build: UC8279 (not probed)`, then `Panel controller UC8279 (forced by the build)`, then `Wait complete:  8279_PON (N ms)` (two spaces: grep `8279_PON`) and `8279_DRF` on the first paint, and no `[XTDET] X3 stock probe` line. That is the first session's sequence.
2. The page that froze, reopened (step 13): the strip path with the fix, for the first time on this module.
3. One sleep and one wake (step 8): power key, wait for the image to hold, power key. The first session did not show that a deep-sleep wake repaints on this module.
4. Ten cold power-ups and ten deep-sleep wakes, one of each at a low cell voltage (step 8), each ending in a picture. A blank one points first at the missing `BUSY_N` gate between reset release and the first init command.

**Front light on a cut board.** The 368 envs compile it in and the test firmware leaves it off. A cut board has no `D3`/`D8`: U10's own OVP is the only limit (BRING_UP §10), and an enable with nothing on `J3` is the safe-but-avoidable open load. Do not switch it on in the reader image with `J3` empty, or drop it from the build in `platformio.local.ini` (every Silkscreen env, the 368 ones included; set it under `[silkscreen368]` instead to drop it from the 368 envs alone):

```
[silkscreen]
frontlight_flags =
```

| Step | Cut board | Full-size board |
|---|---|---|
| 1 Power key | the rework, first | skip: `SW10` is POWER |
| 2-5 The gate | as written | as written |
| 6 OPT | "Cut-down board" (sets UP(2) as POWER) | neither option (or only "UP(2) is POWER" after the rework) |
| 7-15 | as written | as written; the front light has `D3`/`D8` |

### OPT Board options

**Purpose:** what the firmware cannot see about the board under test, for the tests that depend on it. It is no test: each change logs an INFO row.

Two options, kept in `/.silkscreen-test/options.cfg` (never under `/.crosspoint`) and read at every boot; without a card they last until the next restart. Each change is saved at once.

- **Cut-down board** (Confirm): the board was shortened at its cut line (HARDWARE.md §16), so `J6` and `SW10` are gone. P2 offers no J6 scan and the front-light load question offers no J6 strip; P2 and T9 record `j6=n/a`. Turning it on turns UP(2) as POWER on too, its default there; either can be changed afterwards.
- **UP(2) is POWER** (Left): `R72`/`R74` fitted and `R36`/`R73` removed (HARDWARE.md §9.2), on a cut-down board or a reworked full one. T3 counts `SW7` as POWER on `IO18` (on a full-length board `SW10` too, so POWER expects twice a key's count), skips the two chords with UP(2), and fails `up2_window` if ladder 2's UP(2) window (2745-2995 mV on `IO4`) ever fires.

The row carries `cut=<0|1> up2pwr=<0|1> saved=card|ram_only`.

### P1 System info and boot checks

**Purpose:** the module, the firmware and the boot records are what this board should have, and the wake and the idle clock behave. **Bring-up:** B1 (and the wake of B3).

Each opening logs the panel controller (`action=panel`), then the boot checks (`action=boot`), so open it after a cold boot and again after a power-button wake; Record logs both again. It shows the profile and board tag, the firmware, the chip, the panel controller, ESP-IDF and the Arduino core, flash, PSRAM and heap, the reset reason, the wake cause with the ext1 mask, the abnormal resets this boot counted and the count in RTC memory now, any aborted sleep entry, eFuse `PIN_POWER_SELECTION` (information only; HW-01), and whether the card, the RTC, touch and the front light are there. The reset records are read with the SDK's non-consuming accessors, after `main.cpp` has used them.

It also watches the idle clock: with no input for 3 s the loop drops to 80 MHz (POWER.md §5). A USB data host or the radio holds the full clock by design, and the screen says "held". The idle clock logs its own row (`action=idle_clock`), once per opening: PASS when the loop reached 80 MHz, with the time it took; FAIL when it had not after 10 s without input while nothing held it. Held logs nothing. The boot row carries the idle clock as it stood, without judging it.

**Panel controller.** The 4.26" panel is a Good Display GDEQ0426T82 (SSD1677) or the custom panel (UC8279D). The SDK probes it at every cold boot with a half-duplex read on SDI (IO12) and keeps a conclusive verdict in RTC memory for a deep-sleep wake (never in NVS). An UltraChip part answers the read; an SSD1677 is recognized only because nothing answers. P1 shows:

- the controller and how it was chosen: probed this boot, kept from the probe before the sleep, or forced by the build (`-DSILKSCREEN_DISPLAY_CONTROLLER`, no probe);
- the probe's verdict: UltraChip answered; no UltraChip answer (SSD1677); or inconclusive, which runs as SSD1677;
- the VER bytes and the FLG status byte, and on a UC8279 its LUT_VER (VER byte 2) and the driver variant (the same byte: 0x02/0x03 select the QY gray tables, 0x67 no gray, anything else the default tables);
- the first MTP bytes when the probe read them.

The `action=panel` row logs all of it (`controller`, `source`, `verdict`, `ver`, `flg`, `lut_ver`, `variant`, and the 48 MTP bytes as `mtp`). PASS: a probe resolved the controller, this boot or before the sleep. INFO: the build forced it. FAIL: `probe_inconclusive`, or `not_resolved`. After a wake the row must say `source=cached` with the cold boot's IDs. On the 3.68" variant nothing is probed: the row says the controller is fixed in the build (`source=fixed probe=none`, INFO, no VER bytes), and an INFO means the build is consistent, not that a panel is there; [the variant's table](#the-368-variant-silkscreen-368-test) has the row.

**Boot checks.** The SDK's boot-time checks (rules in `ProbeChecks.h`), each shown on its own row and carried in the boot row:

- the key ladders (`ladders=IO1:fitted,IO4:fitted@probed`): at a reset other than a deep-sleep wake each ladder pin was read with the internal pull-down, then the pull-up, and a ladder with no pull-up fitted (it follows both from rail to rail) is off for the session (`off`); the screen shows both readings in mV (a fitted ladder reads above about 2.2 V pulled down). After a deep-sleep wake the result is the one kept from before the sleep (`@cached`);
- the power button's idle level (`pwr_idle=low:3mV`): IO18 read on the ADC at a reset other than a deep-sleep wake. `high` means held at boot; `undefined` means it idles between the logic levels (R36 and R72 both fitted, about 1.76 V), so the pin is off and every sleep has no wake source: press SW11 (reset) to start the board;
- the touch chip id (`touch_id=ok:64`, `rejected:1c` for a device at 0x38 that is not a FocalTech controller, `none` with nothing there) and the RTC pattern (`rtc_id=ok`, `rejected@0x68` for a device there without the DS3231 register pattern). Both devices refused are left alone; P2, T11 and T13 judge them.

PASS (`action=boot`): profile `silkscreen`; 16 MB flash; at least 7 MB PSRAM (the 8 MB part less the allocator's share); no abnormal reset and no aborted sleep entry this boot; after a deep-sleep wake, cause `EXT1` with the power button's bit (IO18, `0x40000`) in the mask; the panel controller resolved (the row carries it as `panel=<controller>:<source>:<VER>:<FLG>` and fails `panel` with the panel row); and the power button not idling between the logic levels (`power_idle`). A ladder found off is no failure here (a build may leave a ladder out); T3 shows it. The touch address shown is the one the driver uses (a GT911 fallback, in an alternate-jumper build, may answer at its alternate, 0x14).

### P2 I2C bus scan

**Purpose:** the sensor bus is healthy and the expected devices answer on it. **Bring-up:** B7 (RTC), B9 (touch).

Confirm scans with J6 empty; Right (Scan+J6) scans with an accessory on J6. On a cut-down board (OPT) there is no J6: Right is not offered, and the record says `j6=n/a`. Each scan is logged. It probes 0x08-0x77 by address only, at 100 kHz and at 400 kHz, through `freeink::i2c::scanSensorBus()`. A bus the RTC or touch driver already runs is used as it is: only its clock changes for the 400 kHz pass, and it is put back. A bus no driver runs is started for the scan and ended after it. The SDA/SCL idle levels are read only while a driver owns the bus, and nothing here runs the stuck-bus recovery. Every device that answers is listed with what the profile puts at that address.

PASS: at most one RTC answers (0x68 DS3231 or 0x51 RV-8263), and the one `HalClock` began at boot still answers (`rtc_lost` otherwise). With none answering and none begun at boot, P2 asks whether an RTC is fitted, as T13 does: "None fitted" is no failure (the RTC is optional, rtc-devices), a fitted one fails `no_rtc`, and Back records nothing. The touch controller found at boot answers at the address its driver uses (a GT911 on the fallback, alternate-jumper builds only, may sit at 0x14); no touch address answers when none was found at boot; both clocks find the same devices; both lines idle HIGH; and, with J6 empty, nothing else answers. An FT6336U in Monitor mode may not answer a scan started from a key: tap Scan instead, and the touch wakes it.

A device the boot's identity checks refused is listed as "not FocalTech, left alone" (0x38) or "not a DS3231, left alone" (0x68), and counts neither as the touch controller nor as an RTC. With J6 empty only the touch tail or U13 can sit there, so a refusal fails `touch_id_rejected` or `rtc_id_rejected`; with an accessory on J6 (Scan+J6) it is the expected result. The record carries both checks (`touch_id`, `rtc_id`, as in P1).

### T1 Battery

**Purpose:** the battery reading that the cutoff, the firmware-write floor and the percentage use. **Bring-up:** B6.

Measure P+ at J5 or across the cell, and enter it: Left/Right -/+10 mV, Up/Down +/-100 mV, starting from the firmware's reading. Confirm records. Repeat on battery, on USB with a cell, and on USB with no cell after a cold boot.

It shows P+ (16 samples of IO8, x2), the pin's average and one single sample, the percentage in 10 % steps with its deadband, whether a cell is fitted (P+ at least 2500 mV), the battery label the reader's header and status bar show ("No battery" while `USB_STAT` shows the charger's no-battery blink, or on external power with P+ below 2500 mV until it reads 2600 mV again, `HalPowerManager::noChargeableCell()`; `no_cell_notice=` in the record), the `USB_STAT` state and external power, the pin's range over the last 30 s, where P+ sits against the cutoff that applies (3400 mV, or 3300 mV with the radio on; not judged on external power or with no cell), whether a firmware write is allowed (3600 mV floor on battery), and the time on the screen. The no-cell case wants 2 min on USB with no cutoff sleep. "Charging" means P+ follows the charger's output, not the cell's charge.

It takes one reading a second, repaints at most every 5 s, and takes no reading within 0.9 s of a repaint, whose load would move P+. It never takes a cutoff reading of its own (`isBelowCutoff()` state belongs to the main loop), and it never sets a mode or a pull on IO8.

PASS: |P+ - meter| at most 150 mV (+/-50 mV ADC error at the pin, doubled by the divider, plus the 12-14 mV load offset and the 1 % divider tolerance); with a cell, the pin at 1500-2100 mV give or take the ADC's 50 mV (1450-2150 mV; not judged while `USB_STAT` reports Charging or Complete, when P+ is the charger's output); off external power, a 30 s spread at the pin of at most 20 mV. Without a meter value the result is INFO.

### T2 Charge status

**Purpose:** the `USB_STAT` ladder decodes every charger state, and USB presence follows it. **Bring-up:** B6.

Pick the bench state with Left/Right, set it up, then Confirm: it asks whether D2 is lit, then records. D2 shows USB power, not charging (HARDWARE.md §3).

| Stage | Set-up | Expect | USB connected | D2 |
|---|---|---|---|---|
| 1 | Battery only; USB_VBUS about 0 V on a meter (HW-08) | OnBattery, 1800-2600 mV (about 2200) | no | dark |
| 2 | USB charger, partly discharged cell | Charging, 800-1350 mV (about 1110) | yes | lit |
| 3 | Weak USB source (optional) | Charging (about 950 mV) | yes | lit |
| 4 | Nearly full cell on USB until the charger stops (optional, long) | Complete, 350-700 mV (about 550) | yes | lit |
| 5 | Cell pulled with USB in (optional; protected cell, insulated tools) | NoBattery: the 550/410 mV blink; or ExternalIdle: a steady 3.3 V, the charger switched off (also safe). Never Complete | yes | lit |
| 6 | USB, no cell, after a cold boot | ExternalIdle, above 2600 mV | yes | lit |

The ladder is sampled by the SDK, once a second (usbstat-slow-sampling); this screen reads its record, and reads IO9 itself only in stage 5's capture (below). It shows the last sample and its window, the published and decoded states, the debounce counters (candidate, the 8 Complete samples, the 30 readings outside every window after which the state is dropped), `isUsbConnected()` and its changes, the lost flag and the P+ cross-check (HW-08: 5 min of P+ at the cutoff while Charging or Complete publishes OnBattery), the last four state changes with the time from the first sample in the new window to the published state, and the battery label the reader shows (as in T1; `no_cell=` in the record).

PASS: the stage's state is published (stage 5: NoBattery or ExternalIdle); the last sample sits in its window; `isUsbConnected()` and D2 are as the table says; a change into the state during the stage settled in time (3 s; Complete 10 s, since it needs 8 stable samples, about 8.5-9.5 s after the change itself; the no-battery blink 8 s); and in stage 5 Complete was not published during the stage (a Complete still published from stage 4 when stage 5 was picked does not count; one still published at Record fails the state). Switching the stage restarts the stage's own record (the states seen, the settle); the last four changes stay listed.

**Stage 1 (HW-08, BRING_UP §4).** The last 10 samples since the stage was picked or the published state last changed (so the unplug itself does not count) are judged too, each in one band:

- at or below 1350 mV, the charging or complete band: FAIL `hw08_clamp` at any count, since a clamp seen once is a clamp. The unpowered TP4056 clamps CHRG/STDBY (CE back-feed), so firmware cannot tell the cell from USB, and the screen says so;
- 1350-2100 mV: WARN `partial_clamp` (check `USB_VBUS` and `D2`);
- 2300-2600 mV: WARN `above_nominal`, ladder tolerance or a weak `ST`, not a clamp (a clamp only pulls the node down);
- above 2600 mV, about 3.3 V: WARN `st_not_asserting`, beside the state's FAIL: `ST` is not asserting (`U2.8`/`R17`), and the screen says so.

Stage 1 needs those 10 samples, one a second, since the last change: wait 10 s after unplugging before Record (the screen counts them, "n of 10, wait"). With fewer it records INFO with `too_few_samples=<n>/10` in place of a PASS or WARN; a failure still fails. The record carries `recent=<lo>-<hi>/<n>`, `clamped=`, `partial=`, `above=` and `st_high=`. A `hw08_clamp` on real boards is the trigger for the `P+` trend check that [POWER.md HW-08](POWER.md#6-hardware-notes-for-a-future-revision) holds in reserve: keep the rows.

**Stage 5.** Record first captures `USB_STAT` for 12 s at 20 Hz (four readings each through `freeink::adc`, no mode or pull set, auto-sleep held off for the run; Back stops it and records nothing), then asks about D2. The capture times the blink: edges at the midpoint of the levels seen, with a quarter of the swing as hysteresis, give the period (from each edge to the next one in the same direction: mean, minimum and maximum) and the share of each whole period at the low level (CHRG and STDBY both on). The record carries `outcome=blink|steady` (the published state) and `cap=blink edges=<n> period_ms=<mean>(<min>-<max>) low_duty=<%> levels=<high>/<low>`, or `cap=steady cap_mv=<lo>-<hi>`: these are the period and duty the firmware's blink rules want. The settle limit is 8 s for the blink and 3 s for the steady level.

### P4 USB presence and USB Drive

**Purpose:** USB presence as the firmware sees it, with no VBUS sense on the board (HW-11), the D2 LED, and USB Drive in and out. **Bring-up:** B4 (USB Drive), B6 (presence, AW-03).

Left/Right pick the bench stage. Set it up, wait for the readings to settle, then Confirm, which asks whether D2 is lit and records. The screen shows:

- the USB host: `HWCDC::isPlugged()`, which follows the host's start-of-frame traffic, and whether the CDC port is open;
- USB power as the reader sees it: `HalGPIO::isUsbConnected()` from the `USB_STAT` ladder, with the ladder state;
- the last four changes of either, and how far apart the two signals' last changes were.

| Stage | Set-up | USB host | USB power | D2 |
|---|---|---|---|---|
| 1 Data cable | A USB data cable to a computer | yes | yes | lit |
| 2 Charger only | A charger or a power-only cable | no | yes | lit |
| 3 Unplugged | On the cell | no | no | dark |
| 4 USB Drive | A data cable | | | |

PASS (stages 1-3):

- the host and USB power as the table says, and D2 as the operator saw it (D2 shows USB power, usb-power-led);
- in stage 2, no host at any time;
- in stages 1 and 3, the two signals' last changes at most 3 s apart. With the ladder at Complete or in the no-battery blink the limit is 10 s: Complete needs 8 stable samples, about 8.5-9.5 s after the plug.

Stage 4: with no card mounted, Start records SKIP (`reason=no_card`) and starts nothing. Otherwise Confirm asks first. The host gets the raw card and can write anything on it, `/.crosspoint` included (P8 shows it afterwards). The test logs an INFO row and opens the reader's own USB Drive screen. End the session by ejecting on the host, or, on the cell, by pulling the cable: the session must end within about 5 s (AW-03). Either way the board restarts. The next opening of P4 asks how it ended. PASS: an eject, or a cable pull that ended it. FAIL: the host never mounted the card, or the session did not end as expected.

### T3 Keys

**Purpose:** every key decodes to its own switch with margin, chords are harmless, and nothing fires by itself. **Bring-up:** B5.

The keys are this test's input. A front key held 1.5 s does what the button above it does: Back leaves, OK records, Left changes the mode, Right switches the radio (an idle station, for the "with Wi-Fi on" runs; on the cell below 3.4 V it stays off and records SKIP, bat-wifi-gate). Each press is matched to its switch by sampling the ladders the way `InputManager` does.

- **Keys.** A boot-checks row first: each ladder fitted or off (found with no pull-up at boot), and the power button's idle level at boot (as in P1). The keys of a ladder that is off are not expected: the screen says so in place of its switches, and they are left out of the counts. For each switch (SW2 RIGHT, SW3 LEFT, SW8 OK, SW9 BACK, SW1 DOWN(1), SW4 UP(1), SW5 DOWN(2), SW7 UP(2)): its presses, its lowest and highest reading while pressed, and the smallest margin to its window's edge. For POWER (IO18): clicks, press and release edges, the last hold and the pin level. Both ladders now, with the lowest released reading. Presses that came through the loop's idle path: after 3 s with no input the loop sleeps in 50 ms slices at 80 MHz and wakes on a contact; the screen counts presses after such a pause, how many found the loop at 80 MHz, and the shortest. A USB data host keeps the loop at full speed, so do this part on battery.
  PASS: every switch and POWER pressed, all the same number of times (50 each for B5; count them), every margin at least 25 mV (BACK and UP(2) are known to be tight), every press matched to a switch, as many POWER releases as presses, and the power button not idling between the logic levels at boot (`power_idle`; it then reads no presses at all). The record carries `ladders=` and `pwr_idle=` as in P1. With UP(2) as POWER (OPT), `SW7` is not on ladder 2: its row shows how often UP(2)'s window decoded a press (`up2_hits=`; any fails `up2_window`), and its presses count as POWER (`IO18` edges), the same number as every other key. On a full-length board reworked that way `SW10` stays on `IO18` beside `SW7`: press each as often as every other key, so POWER expects twice that count (the POWER row says `SW10 + SW7`; `pwr_expect=2x` in the record). On a cut-down board only `SW7` is left (`pwr_expect=1x`). Record starts a new run.
- **Chords.** The six chords of §9.1 come up one by one: press the two keys together, hold about half a second, release. Each contact is judged by the level the ladder held longest. Expected: LEFT+BACK gives LEFT or no key (it can land under LEFT's window after ADC error); UP(1)+UP(2) gives UP or no key; RIGHT+OK gives RIGHT and DOWN(1)+UP(2) gives DOWN (the 100-ohm keys win); LEFT+OK and OK+BACK give no key. A chord on a ladder that is off is skipped ("ladder off") and not expected, and so are the two with UP(2) when UP(2) is POWER. PASS when every expected chord decodes as expected; INFO while some are missing.
- **Hands off.** Leave every key alone for at least 30 s: any key event is a phantom. A ladder with no pull-up is off, so it cannot report phantoms; the record carries `ladders=`. It also shows the released level's range per ladder and whether the raw reading tops out. PASS with no phantom; at most one idle reading that is in no key's window and not released, that is, below the 3000 mV released floor (btn-idle-topcode; one is a key's release edge, more fails `unclean_idle`); over a full watch, every ladder read released at least once (`never_released`); the power button LOW on every reading of the watch (`power_not_low`: with `R36` and `R72` both fitted `IO18` idles at about 1.76 V, HARDWARE.md §9.2) and not found between the logic levels at boot (`power_idle`; the boot check's `pwr_idle=` is shown and logged); and, with UP(2) as POWER, no reading in UP(2)'s window (`up2_window`). WARN when ladder 2 (`IO4`) released does not top-code while ladder 1 (`IO1`) does (`io4_not_top`), or its mean released level sits 100 mV or more below `IO1`'s (`io4_low`). INFO under 30 s with nothing failed. The screen gives the meter check: `IO18` at `R76` pad 1 below 0.3 V released, about 3.0 V pressed. The record carries each ladder's released range and whether it topped out, `mean=<IO1>,<IO4>`, `pwr_idle=`, `pwr_high=` (HIGH readings), `up2pwr=` and `up2_hits=`. Repeat with the radio on.

Holding POWER longer than 0.4 s sleeps the board (the reader's long press); a click does nothing else. POWER + DOWN takes a screenshot to `/screenshots/`, as in the reader.

### Display tests (T4-T8)

The display tests run the reader's own calls (`GfxRenderer`, `HalDisplay`) and read what the SDK driver did from its bring-up record (`EpdBus::diagnostics()`, freeink-sdk README "Bring-up diagnostics"). For every refresh they show and log:

- the mode the driver actually ran. The SDK promotes a FAST to HALF on the first paint after boot or a wake, after a gray pass left without its cleanup, and on the first refresh after a polarity change, so a FAST request is not proof that a FAST ran;
- the update-sequence byte (`0x22`). The Silkscreen profile uses the X4 defaults: FULL `0xF7`, HALF `0xD7` (after the `0x5A` temperature write), FAST `0xFC`, and `0xCC` for the gray waveform (`0xCF` with the Fading Fix power-down). A UC8279 panel reports its `TSSET` byte instead: `0x1E` (GC) for FULL and HALF, `0x5A` (DU) for FAST and for the non-flashing base of a gray page, and its CDI (`0x97`) for the gray waveform;
- the BUSY time, from the activation to the end of the completion wait;
- the host time, which is the rest of the call: SPI transfers, the RAM resync after the refresh, and the power-down.

On the 3.68" variant the driver is the SDK's X3 UC8279d driver, which reports its B/W refreshes only (the sequence byte is its CDI byte: `0x97` for the first refresh after `begin()` and `0xD7` after): see [the variant](#the-368-variant-silkscreen-368-test) for what the display tests then judge.

A refresh is in the **BUSY window** when the driver reported it, it did not time out, and it took at least 100 ms and less than 30 s. The floor is 100 ms, not the 20 ms of the first proposal, because the completion wait gives up after 20 ms when BUSY never rises. That happens with no panel (the BUSY pull reads idle) or with BUSY stuck LOW, so a 20 ms floor would pass a missing panel. (That is the SSD1677's polarity, BUSY active HIGH. On the 3.68" variant BUSY_N is active LOW: a missing panel reads idle and the waits end after about 70 ms, but BUSY_N stuck LOW runs each wait to 30 s, as step 5 of its bench list says.) It also catches a UC8279 panel driven as an SSD1677 after a wrong verdict: its waits either end at once or run to the 30 s timeout. The quickest real waveform, FAST, takes about 500 ms. 30 s is the SDK's timeout, reached when BUSY is stuck HIGH.

The tests change only what the reader's settings change: `SETTINGS.fadingFix` (Sunlight Fading Fix, which the loop hands to the renderer) and `SETTINGS.screenInverted` (night mode, which the render task hands to the display before each render). T8 also changes the panel's SPI clock. All three live in RAM only. They go back to the pinned values at the end of every run and when the screen closes, and a sleep and wake restores the clock anyway. When a test frame is still on the glass as the screen closes, the next screen's first refresh is a HALF. No display test turns the radio on (PWR-02: no Wi-Fi TX during a refresh).

### T4 Refresh modes

**Purpose:** the three B/W waveforms run as asked, in time and cleanly, and a loop of one of them for the panel's rails on a scope. **Bring-up:** B2.

Left/Right (or Up/Down) pick a refresh; Confirm runs it. A run alternates two frames. Each frame is cut into six bands: a 20 px checkerboard, a 1 px checkerboard, 1 px lines, UI text, solid black and white, and dithered grays. Frame B puts a different pattern in every band, so a ghost of the other frame shows up as a foreign texture.

| Refresh | Run | Expect |
|---|---|---|
| FULL, HALF, FAST | 6 refreshes | FULL: no ghost. HALF: a faint ghost at most. FAST: ghosting builds up, every frame readable |
| FAST, async | 6 refreshes through `displayBufferAsync()`, each waited out and its baseline rebuilt as the reader's overlay chrome does | as FAST |
| FULL, HALF, FAST with turn-off | 6 refreshes with the Sunlight Fading Fix on | the same as without it |
| Cadence | the reader's page turns at its refresh frequency N (the reader's setting: 5, 10, 15 or 30; 1 runs as 2): N-1 FAST, then a HALF | ghosting builds, then the HALF clears it; the frame before the HALF stays up for a look |
| FULL, HALF, FAST scope loop | that refresh back to back, the two frames alternating, with a counter, until any key or tap, or 5 min (1000 refreshes at most) | the panel's rails on a scope (BRING_UP §8), below |

**Scope loops (BRING_UP §8).** A loop keeps the panel's charge pump running so its rails can be scoped, with 10x probes (two rails are negative): `PREVGH` (`C14.2`) about +22 V, `PREVGL` (`C16.2`) about -22 V, `VSH1` (`C13.2`) about +15 V, `VSL` (`C15.2`) about -15 V, and `GDR` (`Q4.1`) and `RESE` (`Q4.2`/`R14.2`): clean pulses, with `RESE` peaks well under `Isat` × `R14` (2.4 V). Input is read between two refreshes, so hold a key until the refresh in progress has ended (a FULL takes about 2 s). The loop holds auto-sleep off while it runs. It records whenever it ends, INFO, with `refreshes=`, `stop=input|timeout|limit`, `elapsed_s=`, `ran=`, `busy_ms=`, `host_ms=`, `promoted=`, `bad=` and the panel token; it fails only on a refresh outside the BUSY window (`busy_window`) or promoted (`promoted`; not on the 3.68" variant, where a FAST that runs the GC bank is the driver's design and `promoted=` is only recorded). It asks nothing.

The screen shows the panel controller with the probe's verdict and IDs (as P1 does), the last run per mode (count, sequence byte, BUSY min/mean/max, host time, promotions, refreshes outside the window, and for the variants the async returns and turn-off requests), the mean BUSY per mode over this boot's runs against the X4-class references, and whether FULL >= HALF > FAST holds. Every run's result carries `panel=<controller>:<source>:<VER>:<FLG>`, so each row in `results.csv` names the panel it ran on.

PASS:

- every refresh is in the BUSY window, and none was promoted (the test starts from a clean panel state);
- async: every call returned with BUSY still asserted, sooner than its BUSY time;
- turn-off: the driver saw the power-down request on every refresh. `0xF7` and `0xD7` always power down; after a `0xFC` the driver runs the separate power-off;
- once FULL, HALF and FAST have all been run this boot, their means keep FULL >= HALF > FAST;
- the frames looked as the refresh expects.

A mean more than 50 % from the X4-class reference (FULL about 1800 ms, HALF about 1720 ms, FAST about 500 ms) is flagged in the log (`x4_ref=outside:...`) and on the screen, but does not fail the test. Record the Silkscreen figures as its baseline. The references are the SSD1677's; on a UC8279 panel the runs set that panel's baseline.

### T5 Grayscale

**Purpose:** every gray pass the reader runs gives four levels and cleans up after itself. **Bring-up:** B2.

Left/Right pick one of the reader's gray-pass routes; Confirm runs it. The chart has a four-level ramp (white, light gray, dark gray, black), narrow columns cycling through the four levels, and two lines in the reader's anti-aliased font. The same lines are drawn below in B/W only, for comparison. After the pass, a plain page gets a FAST refresh. Each stays up for a look.

| Route | The reader's calls |
|---|---|
| Text page, planes buffered | FAST base through `displayBufferAsync()`; both planes rendered in 80-row bands into two 48,000 B buffers (52,272 B on the 3.68") while the base refreshes; then `waitRefreshComplete()`, both planes streamed, `displayGrayBuffer()`, `cleanupGrayscaleWithFrameBuffer()` (`EpubReaderActivity` overlapped path) |
| Text page, 80-row strips | the same base, waited out, then each band rendered into an 8,000 B scratch and streamed (the reader's fallback when the plane buffers do not fit) |
| Text page, Fading Fix on | `displayGrayscaleBase(FAST)` with the Sunlight Fading Fix: no async base, a power-down after each refresh, then the strips |
| Cleanup page, HALF base | `displayBuffer(HALF)` and `preconditionGrayscale()`, then the strips |
| Image page, FAST base | a blocking `displayBuffer(FAST)`, then the strips |
| Buffered (sleep, images) | `displayGrayscaleBase(HALF)`, `storeBwBuffer()`, both planes drawn whole and copied, `displayGrayBuffer()`, `restoreBwBuffer()` |

The plane buffers and the scratch are allocated when the screen opens (`makeUniqueNoThrow`) and freed when it closes. The screen says whether they were allocated and whether they sit in PSRAM. Without them, the route that needs them records SKIP. The screen also shows the panel's gray capabilities, and for the last run: the base as asked and as run, the gray waveform, the time to render, wait and write the planes, the cleanup, the plain page's refresh, and the internal and PSRAM heap free before the pass and at its lowest.

PASS:

- the base refresh is in the BUSY window and ran as asked;
- the two async routes' base returned while BUSY;
- the Fading Fix route saw the power-down request on the base and on the gray waveform;
- the gray waveform ran and is in the window. The pass is skipped only while inverted (T6); here a skip fails;
- the plain page ran as FAST, not promoted to HALF. A promotion would mean the gray state was not cleaned up, and its HALF would hide leftover gray;
- the operator saw four distinct levels, and no gray on the plain page.

### T6 Inversion

**Purpose:** night mode on this panel: clean frames, the right waveforms, and no gray pass while inverted. **Bring-up:** B2.

Confirm runs night mode through `SETTINGS.screenInverted`, the way the reader does. Setting the display directly would be undone by the render task on the next render.

| Step | Night mode | Asked | Must run |
|---|---|---|---|
| 1 Normal frame | off | HALF | HALF |
| 2 Inverted frame, first after the switch | on | FAST | HALF |
| 3 Inverted frame | on | FAST | FAST |
| 4 Inverted frame | on | HALF | HALF |
| 5 Gray chart, the buffered route | on | HALF base | HALF; no gray waveform |
| 6 Normal frame, first after the switch back | off | FAST | HALF |

PASS: every step ran the mode in the table and is in the BUSY window. While inverted, no gray waveform ran, the B/W store worked, and the panel did not offer the tiled gray route, which is the reader's gate. The operator saw clean white-on-black frames on FAST and on HALF, a chart in crisp black and white, and no ghost after switching back.

### T7 Orientation and bezel ruler

**Purpose:** the four orientations are the right way up, and the bezel insets for `viewableInsets`. **Bring-up:** B2.

The whole screen, header and touch buttons included, is drawn in the chosen orientation. An arrow points to the logical bottom. Rotate (or Up/Down) steps through the orientations, each with a HALF refresh, so the ruler reads cleanly. Judge asks about the current one and moves on to the next unjudged one. When all four are judged, the result is recorded.

| Orientation | With the board upright, keys at the bottom, the arrow points |
|---|---|
| Portrait | down, at the front keys |
| Landscape CW | right |
| Portrait inverted | up, away from the keys |
| Landscape CCW | left (the panel's native orientation) |

PASS: in all four orientations the arrow points as in the table, and the text reads upright with the board turned so that the arrow points down.

Each edge carries a staircase of 1 px steps, each one pixel deeper than the last (0-20 px), labelled every 5 px. The first step that shows whole is the bezel inset on that edge. The screen also shows the insets the profile gives for the current orientation. **Insets** switches to portrait, the frame `BoardConfig::ViewableInsets` uses:

- Edge picks the next edge;
- +1 px (it wraps), Up and Down change the value;
- Record logs the four as INFO, with the profile's values, for the SDK profile (`viewableInsets` is `{0, 0, 0, 0}`: every measured case window is the active area plus 0.5-0.7 mm);
- Back returns to the orientations.

The renderer goes back to portrait when the screen closes.

### T8 Panel SPI clock

**Purpose:** whether the panel takes a faster SPI clock than the profile's 20 MHz. **Bring-up:** B2 (an open question in REQUIREMENTS).

The SSD1677 is specified to 20 MHz for writes, which is the profile's clock. This test tries a faster one. Left/Right pick the profile clock, 26.7 MHz or 40 MHz. Confirm runs 20 B/W refreshes at that clock: four FAST, then a HALF, four times, with the T4 frames. It then runs the T5 chart through the strip route and a plain page. The last B/W frame, the chart and the plain page each stay up for a look.

The clock is set in RAM only, through the SDK's `FreeInkDisplay::setSpiClockTrial()` (a `FREEINK_DIAGNOSTICS` control). It goes back to the profile's at the end of the run, when the screen closes, and at every wake, when the display restarts. The panel has no read-back on this board, so the frames are compared by eye with a run at the profile clock: run that first. The host time per refresh shows the clock at work. At 20 MHz, a blocking FAST moves 144,000 B (the frame, then both RAM planes resynced), about 58 ms of transfer, and at 40 MHz about half that. The screen keeps each clock's mean BUSY and host time for FAST and HALF over this boot, with its verdict.

PASS: every refresh is in the BUSY window, none was promoted, the gray waveform ran and is in the window, the plain page ran as FAST, and every frame, the chart and the plain page looked as they do at the profile clock. Raise `displaySpiHz` in the profile only after clean runs at the higher clock on more than one board (REQUIREMENTS: the EPD SPI clock is an open question).

### Front-light tests (T9, T10)

Both light the boost only after a question names the load, before each lighting session: T9's Confirm, and each T10 run. The answers are "No load: cancel" (preselected), "J3 panel light" or "J6 LED strip"; a cut-down board (OPT) has no J6, so its question offers no strip. Within a session the light can go dark and relight on the same load without a new question: a T10 run relights twice by itself (the retry at the lowered level, the restore at the profile's), and T9's string switch, like each of its switch run's 30 colour changes, takes the driver dark for at least 3 ms and pulses its enable again while lit. The one exception is T10's step 0, which lights the boost with no load on purpose, opt-in, after its own questions (below). Nothing else in the test firmware lights it. They read `LED_MONIT` (IO2) through `freeink::adc`, and the SDK monitor's record (`FrontlightManager::monitorDiagnostics()`, freeink-sdk README "Bring-up diagnostics"): the trips since boot with the last trip's `LED_SW` and limit, and the monitor's last reading. The main loop's trip handler switches a tripped light off, which clears `fault()`, before any screen runs. So a trip shows as the light dropping out and the record's count rising, never as `fault()`. `LED_SW` is the pin reading × 9.333.

Every switch-on and every string change while lit also runs the SDK's load probe (`FrontlightConfig::loadProbeWindowMs`, `loadProbePeriodMs`): `LED_MONIT` read every 20 ms for 500 ms after the enable pulse, on the SDK's monitor timer, after the call that lit it has returned; the tests wait for it to decide (`LightCheck::awaitSwitchOn()`). A reading above the trip there is a "no load" result for that string, not an in-use trip: the SDK then tries the other string once `LED_SW` has fallen to 15 V (at most 4 s), and either records the first one missing (a single-colour light, which then stays on the other string until a cold boot) or reports load absent and leaves the light off. The tests read that record through `FrontlightManager::probeDiagnostics()`: the load (unknown, present, absent), each string (unknown, present, missing), and the probes, their trips and the last probe's peak reading. Probe trips are counted apart from the monitor's.

The pads are read from the GPIO registers (`gpio_get_io_config()` and the output register), never set: IO42 (ADIM) on its LEDC carrier while lit, and held LOW by the GPIO output register while dark; IO40 (COLOR_SEL) at the lit string (HIGH = warm; the string the SDK picked, which differs from the one asked for when a string is missing), and LOW (cool) while dark, except after a trip on the warm string, until `LED_SW` is at 15 V (at most 4 s).

### T9 Front light

**Purpose:** both strings light at every brightness step, `LED_MONIT` reads the LED rail, the switch-on probe finds the load, each string's forward voltage (BRING_UP §7.2), and colour changes for a scope (BRING_UP §7.3). **Bring-up:** B8.

**First, with a `J3` flex:** BRING_UP §7.1's flex map, on the bench supply with the board unpowered, before T9's first Confirm. A flex that lights only with the leads swapped uses the reversed FT01C map: never answer "J3 panel light" for it, here or in T10 ([PORTING.md B8](PORTING.md#bring-up-checklist-real-board)).

Confirm lights the boost after the load question; Confirm again switches it off. The switch-on probe runs after that Confirm and after every string switch while lit, and the screen waits for it to decide: when it finds no load on any string, the light stays off and T9 records FAIL `no_light` at once. Up/Down step the brightness through 10, 25, 50, 75 and 100 %. That is the SDK's perceptual curve, so about 2, 10, 32, 62 and 100 % duty, or roughly 0.3, 1.3, 4.2, 8.3 and 13.3 mA of LED current (duty × 13.3 mA). Left steps cool → warm → mix → cool; between the strings, while lit, the driver goes dark for at least 3 ms and gets its enable pulse again. Mix (offered only where the SDK blends: `colorBlendHz` set and no string recorded missing) asks for 50 % warmth: `COLOR_SEL` runs its blend carrier while lit, at once if both strings have lit since the reset (cool then warm does that), and its readings fill a third table column. Right, while lit, starts the switch run (below). Take both strings through every step, switch off with Confirm, then Record (Right, while dark): it takes the after-off reading, then asks what the light did. Record before anything was lit logs INFO with the dark reading.

**dVf (BRING_UP §7.2).** The settled readings at the 100 % step and at the 25 % step (about 10 % duty) give each string's `LED_SW` at full and about 10 % current, and the difference between the strings, which is what `LED_SW` re-slews by at a colour change. The screen shows them, and the record carries `vf=<cool 100>/<cool 10>,<warm 100>/<warm 10>` and `dvf=<warm - cool at 100>/<at 10>`, in mV of `LED_SW` (`-` for a reading not taken). They are readings, not judged.

**Switch run (BRING_UP §7.3).** Right, while lit, repeats the reader's own colour change while lit (dark for the dwell, `COLOR_SEL`, the enable pulse, then the load probe's 500 ms window) once a second: 10 changes at about 100 % duty (brightness 100), 10 at about 50 % (66) and 10 at about 10 % (25), about 35 s in all, with auto-sleep held off. Scope CH1 on `TP3` (`LED_SW`) and CH2 on `COLOR_SEL` (`U12.2`/`Q5.1`), 10x probes. 0.3 s after each change has decided, a settled reading must show the light lit, at least 1 V above the dark rail (at the dark rail, U10 latched off while its carrier runs) and below 22 V. Any of these ends the run at once with the light off and FAIL: an in-use trip (`trip`), a probe trip (`probe_trip`: the new string read above 23 V right after the change, the both-off spike), a light that went out (`light_off`, `no_light`), `latched_off` or `over_22v`. Back stops the run (INFO). A run that completes switches the light off and asks what the scope showed: "Clean" (each re-slew within about 350 µs, below 23 V) is a PASS, "Spike, slow re-slew or dropout" fails `operator`, and "Not scoped" records INFO. The row carries `mode=colour_switch`, the changes at each duty (`at_duty=`), the last settled `LED_SW` per duty as `<cool>/<warm>` mV (`vf_100=`, `vf_50=`, `vf_10=`), the lit range (`led=`), `trips=`, `probe_trips=`, `scope=` and `stop=`. With a string recorded missing (a one-colour strip) there is nothing to switch: SKIP `one_string`.

It shows:

- the dark reading, taken 0.3 s after the screen opens: `LED_SW` one diode below `LDO_IN`, about 0.25-0.55 V at the pin (fl-offstate-reading). Below 100 mV the monitor cannot see `LED_SW` and its trip is blind, so the test does not light and records FAIL;
- lit, a reading once a second after each change has settled for 0.3 s (fl-monit-settle), kept per string and step in a table of pin mV, and the SDK monitor's own last reading from its 100 ms tick, with its read and skipped counts;
- the trips on this screen and since boot, and the trip level;
- IO42 and IO40 as above, and any pull on IO42, IO40 or IO2 (fl-adim-no-pullup, fl-color-drive, fl-monit-pin); in Mix, IO40 must be on its carrier (a peripheral), not the GPIO register (`io40_state`);
- the blend carrier's frequency, or why there is none (fixed selection, or a string missing);
- the enable pulse, the dark dwell and the carrier from the profile, for the optional scope check: IO42 LOW for at least 3 ms, HIGH for about 150 µs, then the 25 kHz carrier; IO40 changes only while IO42 is LOW, at least 3 ms after it fell;
- what the switch-on probe found: the load, each string, the last probe's string, result, peak reading and reading count, and its trips on this screen and since boot. The table files each reading under the string the SDK lit.

The record adds `probe=`, `cool=` and `warm=` (the probe's findings), `probe_trips=`, `probe_peak=`, `mix_pin=` (the Mix column, `n/a` if never lit), `vf=` and `dvf=` (above) and `j6=` (`n/a` on a cut-down board). Mix readings are judged against the same lit window as each string. Started from Mix, the switch run first holds the dominant string (lit, no probe).

50, 100 and 200 ms after every light-up, and at every reading, `LED_SW` above 22 V (2357 mV at the pin) switches the light off at once and records FAIL: an open load or a miswired J3 (fl-j3-mapping-risk). The SDK's own trip is at 23 V, below U10's 24.25 V OVP.

PASS:

- the dark reading between 100 and 700 mV at the pin (above 700 mV, `LED_SW` is above any `LDO_IN`: the boost runs while it should be dark);
- every lit reading below 22 V; with J3, 11-18 V `LED_SW` (about 15 V at full current, less at 10 %; proposal); with J6, at least 1 V above the dark rail;
- no trip, no 22 V switch-off, and no switch-on that found no light;
- both strings measured at least once. With a J6 strip, a string the probe found missing is no failure (a single-colour strip); the J3 panel light has both;
- 0.3 s after switching off, the reading back below 700 mV;
- the pads as above;
- the operator saw each string light, get brighter at every step, and not flicker.

### T10 Front-light over-voltage trip

**Purpose:** the software over-voltage trip switches the boost off and keeps it off, the switch-on probe takes a reading above the trip for "no load", and (step 0, opt-in) the SDK catches a real open load. **Bring-up:** B8.

This replaces the rebuild with a lower `SILKSCREEN_FRONTLIGHT_TRIP_MV` in PORTING.md B8. Confirm, after the load question, runs steps 1-5, about 5 s. Back stops it, and a stopped run records nothing. Left runs step 0 (below), with nothing connected: run it first, wait 10 s, then connect the light for steps 1-5.

| Step | What happens | Expect |
|---|---|---|
| 1 | Cool string at 50 % with the profile's trip level (23 V), for 0.6 s | the switch-on probe finds the load; no trip; the monitor's lit reading is kept |
| 2 | The trip level is lowered in RAM to the lit reading less 2 V or a fifth, whichever is more (12 V for a 15 V string) | an in-use trip within 300 ms, not taken for "no load"; IO42 held LOW by the GPIO register; `LED_MONIT` back below 700 mV |
| 3 | 2 s | dark: no relight, no second trip, IO42 held LOW |
| 4 | Switched on again, level still lowered | the switch-on probe reads above the level at once: the load is taken as absent (the other string is tried too, and trips), IO42 held LOW within 1 ms of the reading (the SDK's record), the light stays off, IO42 LOW afterwards, and the monitor counts no trip |
| 5 | The profile's level back, switched on, for 1 s | the probe finds the load again; lit, no trip |

It then asks whether the light did that. The trip time runs from the lowering to the trip in the SDK's record; the probe's time runs from the carrier start to the reading above the level. A failed step ends the run with FAIL. The record carries `probe2=` (time and reading), `probe2_dark=` (the reading to IO42 held LOW; over 1 ms fails `probe_io42_slow`), `probe2_strings=` (strings tried), `probe2_load=`, `restored_load=` and `probe_trips=`. On a one-colour J6 strip, step 4's second try enables the empty string once: an open-load enable, which is safe (HARDWARE.md §13.1).

The level only ever goes down, never above the profile's. It goes back at the end of every run, on Back, when the screen closes (a sleep included) and whenever the menu opens; a restart restores it anyway.

PASS: every step as in the table, the level back at the profile's, and the operator's yes.

**Step 0: a real open load (opt-in, BRING_UP §7.4).** This is the one place the test firmware lights the boost with no load, an explicit exception to T10's rule, and only on the operator's word. Left asks, in turn:

1. the driver build: the standard TPS923610 with `D3` an SMAJ26A, or the TPS923611 swap. The swap goes on only if the operator then confirms that `D3` is an SMAJ33A, since the TPS923611's OVP (29.6-31.4 V) overlaps the SMAJ26A's 28.9 V breakdown; otherwise SKIP `swap_without_smaj33a`. A cut-down board (OPT) has no `D3`: its question says U10's OVP is the only limit, and the swap is refused outright (SKIP `swap_on_cut_down`);
2. that `J3` and `J6` are both empty (on a cut-down board, `J3`).

Cancel or Back on a question records nothing; a refused swap records its SKIP. Step 0 then needs a valid dark reading, 100-700 mV at the pin (above 700 mV `C9` is still charged: SKIP `not_dark`, wait 10 s; below 100 mV FAIL `dark_blind`), and switches the light on once, at 10 %, through the reader's own path at the profile's trip level. With nothing to light, U10 climbs to its OVP (24.25-25.5 V). The SDK's probe must read above 23 V and take `ADIM` LOW, try the other string once `LED_SW` has fallen to 15 V, and record no light, after which the light stays off.

| Check | Expect | Fails |
|---|---|---|
| The first trip, from the SDK's probe record (or the in-use monitor's, after the probe window) | within 500 ms of the switch-on | later: `trip_slow`. None within 1 s: the light is switched off at once, and at the end a question asks whether a light came on, since the firmware cannot tell the two causes apart: yes fails `lit` (a load answered: something is on J3 or J6), no or Back fails `no_trip` (the monitor missed the open load; U10 latched off by itself). `led_at_off=` helps: a lit string's steady 12-15 V against a level decaying from the OVP |
| IO42 when the trip is seen | held LOW by the GPIO register | `io42_not_low` |
| Any string lit | none | `lit`: something is on J3 or J6 |
| The 2 s after the switch-on has decided | IO42 held LOW, the light off, no new enable or trip, `LED_MONIT` not rising | `io42_released`, `relit` |
| `LED_SW` 10 s after the last trip | about 7 V or less (`C9` discharging through R39/R41); a reading, not judged | - |

Auto-sleep is held off for the step (about 15 s). The screen keeps the advice up throughout: **wait at least 10 s after step 0 before plugging a light into J3 or J6**. The row is recorded at the end of those 10 s (Back during them records it with `led_10s=stopped`; Back before records nothing), and carries `step=0`, `build=`, `cut=`, `dark_pin=`, `trip=probe|monitor|none`, `trip_ms=` (the switch-on to the trip), `after_us=` (the carrier start to the reading, the SDK's), `trip_led=`, `dark_after_us=` (the reading to IO42 LOW), `io42_low=`, `strings=` (strings tried), `load=`, `rail_timeout=` (the second string not tried within 4 s), `led_at_off=` (`LED_SW` as the switch-on ended, before the switch-off), `lit_seen=` (the answer after no trip: `yes`, `no`, `none`; `-` when not asked), `watch_pin=` (`LED_MONIT` at the start and end of the 2 s) and `led_10s=`. PASS: every check above.

### T11 Touch probe, mapping and gestures

**Purpose:** the touch probe, the controller's orientation and raw ranges, and the gestures. **Bring-up:** B9. On `silkscreen-368-test` there is no touch in the build and this is one screen that says so ([the variant](#the-368-variant-silkscreen-368-test)).

The screen opens on the probe and the mapping:

- the controller and the address it answered at; the probe's result (fresh, or from the RTC cache after a deep-sleep wake; primary or GT911 fallback); for a FocalTech controller, the ID bytes the probe read (0xA3 chip id, 0xA6 firmware, 0xA8 panel id); and the reset pulses this boot;
- the chip-id check (`TouchConfig::requireFocalTechId`): the id accepted (expect 0x64 for the FT6336U), or a device at 0x38 refused with its id (left alone: no probe writes and no sleep command), and whether this build has the GT911 fallback (only with `-DSILKSCREEN_TOUCH_ALT_JUMPERS=1`, for the alternate J4 jumpers);
- the mapping in force (`swapXY`, `flipX`, `flipY`, and the raw range of each axis after the swap), and whether it is the profile's;
- the last contact in raw controller units, and where the last tap lands (a cross).

Up/Down step through the eight swap/flip combinations; the ranges follow the raw axis when `swapXY` changes. Keys always work here.

- **Calibrate** (Confirm). Tap five targets one at a time: the corners of the area between the header and the buttons, then its centre. Each tap keeps the raw point of its touch-down, whatever the mapping in force makes of it. After the fifth, the test works out which raw axis follows each panel axis (`swapXY`) and fits a straight line through the five points for each. The line's ends at the panel's edges give `rawMin`/`rawMax`, and a falling line gives a flip. Both lines must correlate at 0.95 or better and span at least 100 raw units; otherwise the calibration fails and nothing changes. The fit goes into force in RAM (`InputManager::setTouchMapping()`), it is logged as INFO with its values, and the check follows. Left puts the profile's mapping back.
- **Check** (Left, and after a calibration). The five targets again, each tap measured where the mapping in force lands it. PASS: every target within 24 px on each axis (proposal). The record carries `swapXY`, `flipX`, `flipY` and the four ranges for the SDK profile, where they are still a guess (BoardConfig `SILKSCREEN.touch`).
- **Gestures** (Right). Tap, long-press, drag slowly, and swipe left, right, up and down. Start a drag moving: a finger held still for 0.5 s is a long press. Confirm records, Left clears. PASS: a tap; a long press that fired 0.5 s after the touch-down (20 ms early to 150 ms late); a swipe in each direction; and a drag of at least 8 points with no gap over 40 px between two samples (proposal). The last drag is drawn as a trail.

While the targets or gestures are on, every contact is the test's: the touch buttons do nothing, there is no header back button, and swipes from the left and bottom edges are gestures, not Back or Home. Back stops, and Confirm starts the targets again.

The mapping stays in force until the next boot, so the menu uses it. With no controller found, the screen shows the probe and the chip-id check, and Record asks whether a tail is fitted: none is a PASS (the board runs touchless, touch-optional); a fitted one is a FAIL. That record carries `touch_id` (as in P1) and `gt911_fallback`, so a tail refused by its chip id, or a GT911 tail on a default build, shows up there.

### T12 Touch sleep command

**Purpose:** the controller takes its sleep command, so it draws 55 µA and not 220 µA through deep sleep. **Bring-up:** B9, B10. On `silkscreen-368-test` there is no touch in the build and this is the same one screen as T11.

J4's 3V3 is not switched, so every sleep entry has to put a fitted FT6336U into hibernate (`0xA5 = 0x03`, TP-01).

- **The sleep entry.** Opening T12 after a deep-sleep wake logs the sleep entry that boot woke from, once per boot. That is the result of `prepareTouchForSleep()`, which the sleep path runs through `HalGPIO::prepareForSleep()`, from the SDK's RTC record, with whether its first command was NACKed and needed the reset and a second one (TP-02). A ghost wake's re-sleep, which runs before touch starts and does nothing to a FocalTech controller, keeps the record of the entry before it. Hold POWER to sleep, wake, then open T12. PASS: Asleep.
- **Hibernate** (Confirm): the same call, now. **Reset+hib** (Left): `resetAndHibernateTouch()`, the path of a sleep after repeated resets (a `TP_RST` pulse, the 300 ms settle, the chip id read back, one command). In an alternate-jumper build it never pulses `TP_RST`, which would restart a GT911 awake: a GT911 gets its sleep command at 0x5D, then 0x14, with `INT` held LOW. Each probes the address before (for information: an FT6336U in Monitor mode may not answer), runs the call and probes again. PASS: Asleep (the command was ACKed), then the address does not answer.
- After either, touch is off until a restart. **Restart** (Right) asks first. The restarted boot probes the controller with a reset (a software restart does not use the RTC probe cache), so touch works again in the menu.

J4 pin 2 current: a µA meter in place of R42 (3V3 to pin 2), or a 100 Ω resistor there and a meter across it (55 µA = 5.5 mV).

| State | J4 pin 2 |
|---|---|
| Awake, untouched for 10 s (Monitor mode) | about 220 µA |
| Hibernated (after Hibernate, and in deep sleep) | about 55 µA |
| `TP_RST` (IO11) LOW: a fault, the reset line's ~3 kΩ pull-up in the controller | about 1.1 mA |

`IO11` reads 3.3 V on a meter in every state. No test holds `TP_RST` LOW or writes a controller configuration register.

### T13 RTC

**Purpose:** which RTC is fitted, whether its init left the bits the sleep budget relies on, and whether it keeps and takes the time. **Bring-up:** B7.

The screen shows:

- which of the two addresses answers: 0x68 (U13 DS3231MZ) and 0x51 (U14 RV-8263-C7), and the RTC `HalClock` began at boot;
- a dump of its registers (DS3231 0x00-0x12, RV-8263 0x00-0x0A) and the time in them, in UTC, even while the lost-time flag is set;
- `hasValidTime()`;
- DS3231: OSF, EN32KHZ and BSY (0x0F), EOSC and INTCN (0x0E), the temperature and the aging offset. RV-8263: OS, STOP and the CLKOUT field;
- the boot's identity check (`SensorsConfig::rtcIdentityCheck`): `ok`, or the address it refused (a device without the DS3231 register pattern, left unwritten); and whether the dump now matches that pattern;
- the reads on this screen, the errors and the last read's latency.

The actions:

- **Record** (Confirm) reads again and records the register checks. With nothing answering, it asks whether an RTC is fitted: none is a PASS (the RTC is optional, rtc-devices), a fitted one a FAIL.
- **Set time** (Left) sets the time by hand, with no Wi-Fi. Year, month, day, hour and minute are in local time (the reader's time zone). Left/Right or Up/Down change the field, and Confirm moves to the next. On the minute Confirm writes, with the seconds at 0, so write on the minute. Back cancels. It writes through `Rtc::set()`, which writes the time and then clears OSF, and reads the time back.
- **Timing** (Right) asks which to run:
  - the 60 s check times 60 s of RTC seconds against the CPU clock, from one seconds tick to another;
  - the bench reads the time 1000 times at 100 kHz, then 1000 times at 400 kHz, then puts the bus back at its own clock. The touch controller shares the bus.

PASS:

- **Record:** exactly one RTC answers, and `HalClock` began it at boot. DS3231: the boot's identity check passed (`id_rejected` otherwise: with J6 empty only U13 answers at 0x68) and the dump shows the DS3231 pattern (`pattern`, the mismatch mask in `pattern=`), EN32KHZ 0 (rtc-en32khz-clear), EOSC 0 (rtc-eosc-zero), 10-40 °C (proposal), and OSF 0 once the time has been set on this screen. RV-8263: STOP 0, CLKOUT off, and OS 0 after a set;
- **Set:** written, read back within 2 s of the value written, the lost-time flag clear, and `hasValidTime()`;
- **60 s check:** RTC and CPU within 50 ms: both ends are timed to a seconds tick, polled every 5 ms (5 ppm is 0.3 ms). The check holds auto-sleep off while it runs (about 61 s);
- **Bench:** no error at either clock (rtc-i2c-bench-verify). Scope the SDA/SCL edges at 400 kHz before raising `SensorsConfig::i2cHz` (B7).

After the first cell insert, Record first (OSF reads 1: the oscillator has not kept time), then set the time here or in T15, and Record again (rtc-osf-check). The test never writes the control register, so EOSC stays 0. It writes the status register only through `Rtc::set()`, which clears OSF, and EN32KHZ with it on this board.

### T14 RTC across deep sleep

**Purpose:** the RTC keeps time by itself through a deep sleep (VBAT only, rtc-accuracy). **Bring-up:** B7, with B10's sleep.

1. Hold POWER to sleep. Start a stopwatch when the sleep screen has finished drawing.
2. Wait at least 10 min. MEAS-01 can run meanwhile.
3. Stop the stopwatch as you press POWER to wake, and hold POWER until the splash.
4. Open T14 and enter the stopwatch reading. The value starts at the RTC's figure; Left/Right change it by 0.1 s, Up/Down by 1 s. Then Record.

The sleep screen times the RTC once its refresh has returned, the moment the stopwatch starts, to the RTC's next seconds tick. It keeps the reading in RTC memory (`SleepRecord`): the record survives deep sleep and the ghost and release wakes, and any other reset clears it. T14 times the RTC the same way and works back to the moment the boot began, which is the press that woke the board. There is no timer wake (ZD-01): the stopwatch is the reference. The boot's own time before `millis()` starts (ROM and bootloader, about 0.1-0.3 s) counts as asleep.

PASS:

- |RTC elapsed - stopwatch| at most 2 s: each end has the operator's timing and the RTC's 1 s steps, while 5 ppm is 3 ms per 10 min;
- the time still valid (OSF 0);
- the wake is `EXT1` on IO18;
- the RTC was not set since the wake (T13 or T15 would set it).

Under 10 min, or without a stopwatch value, the result is INFO. With no RTC fitted (it is optional, rtc-devices), Record logs SKIP (`reason=no_rtc`). After a sleep of an hour or more, run T15 next: its "RTC - NTP before the sync" is the RTC's drift.

### P3 SD card

**Purpose:** the card, the 4-bit bus at 20 MHz, a transfer verified end to end, and `3V3` as the card is switched on (power cycles). **Bring-up:** B4.

The screen shows the card and the link as the mount left them, from the SDK's bring-up record (`SDCardManager::cardInfo()`, no card I/O):

- the type and CSD capacity, and the CID name, manufacturer, OEM, revision and date;
- the bus width;
- the clock the link runs, the host cap and the card's maximum;
- the power cycles the parked mount needed, and its last error;
- the volume's used and total space.

**Run** (Confirm) writes a scratch file, `/.silkscreen-test/sdcheck.tmp`, of 8, 64 or 200 MB (Left picks; one more Left gives the power cycles, below). The data is pseudo-random, hashed with SHA-256 as it is written. The test reads the file back, hashes it and compares every byte with the data regenerated, then removes the file. The writes go through `HalStorage` in 32 KB calls, the reader's own path (the SDK passes every 4 KB through its DMA buffer), so the MB/s are the reader's.

- Back stops a run: nothing is recorded, and the file is removed. A run cut short by a sleep or a crash leaves the `.tmp` file for the next boot to remove.
- On the cell a run needs the firmware-write floor, 3.6 V (`HalPowerManager::firmwareUpdateBlockedByBattery()`), or it records SKIP. A brown-out mid-write could damage the FAT the reader shares.
- **Overlap** (Right) runs Wi-Fi scans back to back and a FAST refresh every 2 s during the run (sd-clock: try a faster clock only after the overlap passes). USB power only: it is refused on the cell (PWR-02), and a supply on J5 reads as the cell and is refused too.
- The supply is checked again once a second during a run. When USB goes away mid-run, an overlap run stops (the radio off first, the file removed) and records SKIP `usb_lost_overlap`; a plain run does the same below the 3.6 V floor (`battery_floor_during_run`).
- A run needs the file's size and 16 MB free.

PASS:

- the profile's 4 data lines;
- the clock at the profile's cap (20 MHz) or up to 10 % below it;
- SHA-256 and every byte equal, with no I/O error;
- the file removed.

The MB/s go into `results.csv` as the card's baseline. B4 asks for 200 MB.

**Power cycles (BRING_UP §9).** With the mode after the sizes, Confirm (Cycle) takes the card through the reader's own sleep shutdown and boot mount ten times, one cycle about every 2 s: the cached sectors written back, unmounted, the bus parked LOW and `SD_VDD` gated off for 1 s (`R77` bleeds it below 0.5 V), then the parked mount (`SD_VDD` switched on by `Q7`, 20 ms to settle, the card initialised and sector 0 read). Scope `3V3` at `U3.5` (77.30, 83.20), triggered on `SD_VDD` (`C36`) rising: `3V3` must stay above 3.0 V as `Q7` switches into `C36`/`C37` and the card. Nothing is written but that write-back. Auto-sleep is held off for the run (about 20 s). Back stops it, and leaving the screen too: the card is mounted again first, and Back records INFO. A mount that fails ends the run with FAIL `remount`; the card then stays parked and gated off, as at a boot without one. After the tenth cycle it asks whether `3V3` stayed above 3.0 V: yes is a PASS, no fails `3v3_dip`, and "Not scoped" records INFO. The row carries `action=power_cycle`, `cycles=`, `attempts=` (the power-ons each mount needed, one digit per cycle, `x` for a failure), `max_attempts=`, `failed=`, `last_err=`, `mounted=`, `off_ms=`, `scope=` and `stop=`.

With no card mounted, Confirm asks whether one is fitted. None is a PASS: the parked mount failed cleanly, and the screen shows its power cycles and last error. A fitted card is a FAIL. `SD_VDD` at cold boot and in sleep is meter work (below).

### T15 Wi-Fi and NTP

**Purpose:** the radio and antenna, the RTC set over NTP (B7), and the radio's load on `BAT_MONIT` (B6). **Bring-up:** B7, B6.

Confirm runs:

1. On the cell with P+ below the cutoff, 3.4 V (bat-wifi-gate): SKIP. The light goes off.
2. The RTC before (valid, timed at a tick), and P+ with the radio off.
3. A scan: the number of networks and the three strongest.
4. The reader's Wi-Fi screen. A network saved in `/.crosspoint/wifi.json` connects by itself: the file is read, and the store's saves are no-ops. Or pick a network and type its password, which stays in RAM. Its first-connection hook syncs only a clock that has lost its time. With no connection, the radio goes off and a question asks whether an attempt failed (FAIL `no_connection`) or you backed out without trying (SKIP `reason=cancelled`; Back on the question too).
5. RSSI, channel, P+ with the radio on, then `HalClock::syncFromNTP()`, timed. The RTC against NTP time before the sync (its drift since the last set) and after, and OSF.
6. The radio off (`WIFI_MODE_NULL`), 1 s, P+ again, then the result, drawn and written with the radio off (PWR-02).

PASS:

- at least one network, and a connection;
- RSSI above -80 dBm (proposal);
- the sync within 5 s (`HalClock.h`);
- the RTC valid, OSF 0, and within 1 s of NTP time;
- Wi-Fi off at the end (AW-02);
- on the cell, P+ with the radio on at most 100 mV below the reading with it off (`cutoffRadioAllowanceMv`).

With no RTC fitted (optional, rtc-devices) there is nothing to sync: the NTP step is skipped (`rtc=none ntp=skipped`), and the scan, the connection, RSSI, the radio sag and the radio off are judged.

The idle station loads the radio less than the reader's web server, so the allowance check with the web server running (B6) needs the reader image.

### U1 Wi-Fi TX bursts

**Purpose:** a bounded, repeatable Wi-Fi load for the supply sweep of BRING_UP §3.3: scope `3V3` at `U3.5` for dips below 3.0 V and resets while the supply in place of the cell is swept down to the reader's cutoff. It shows dips and resets above about 3.3 V on P+; TX below the cutoff, in the 3.2-3.3 V sag band BRING_UP expects, is not covered. A bench tool: it records what ran, not a pass. **Bring-up:** B6.

Left sets the time (1, 2 or 5 min), Right the TX: active scans back to back (no network needed: probe requests on every channel, 40 ms a channel), or UDP datagrams (1 KB each, to the gateway's discard port) on a network joined through the reader's Wi-Fi screen (a saved network connects by itself; the store's saves are no-ops). Confirm runs one second of TX, then one second quiet, until the time is up or Back. Auto-sleep is held off for the run. The screen shows the elapsed time and repaints every 10 s, only in a quiet second once a scan has ended, so TX never overlaps a refresh (PWR-02). The radio goes off (`WIFI_MODE_NULL`) at the end, on Back and when the screen closes.

On USB power it runs. With no USB power it asks what is on J5:

- a bench supply in place of the cell (the §3.3 sweep): it starts at any P+, below the 3.4 V Wi-Fi gate too, a dated exception to bat-wifi-gate (REQUIREMENTS.md). The reader's cutoff still applies: at about 3.3 V as read on P+ with the radio on (3 readings a second apart), the main loop sleeps the board, which is a cutoff (`reset=DEEPSLEEP` after the wake), not a brown-out. That is where the sweep ends;
- a cell: P+ at least 3.4 V to start (SKIP `battery_gate`), and the run stops at the cutoff less the radio allowance, 3.3 V (`stop=cell_gate`).

A run on USB stops when USB power goes (`stop=usb_lost`). A brown-out resets the board mid-run, so it records nothing. P1's boot row and U1's screen show `reset=BROWNOUT` only when P+ is still at about 3.4 V or more as the board reboots (a current-limited supply, say). Below that the boot cutoff sleeps the board before the menu, and the power-button wake after it reads `DEEPSLEEP`: only the scope at `U3.5` and the ROM's reset line on the UART log show that brown-out. Three abnormal resets in a row and the board sleeps until POWER (WK-03); 60 s of uptime starts the count again.

The row is INFO (FAIL only `radio_still_on`), with `mode=scan|udp`, `supply=usb|bench|cell`, `dur_s=`, `ran_s=`, `bursts=`, `scans=`, `packets=`, `send_err=` (datagrams the full Wi-Fi queue refused), `pplus_start=`, `pplus_min=`, `ssid=`, `rssi=`, `tx_qdbm=` (the radio's TX power, 0.25 dBm steps), `boot_reset=`, `stop=` and `mode_null=`.

### U2 Sustained load

**Purpose:** the LDO's temperature under a sustained load on USB (BRING_UP §9): a thermocouple on `U3`, the board in its case. The ceiling on USB is about 250 mA continuous (HARDWARE.md §3.5). A bench tool: it records what ran. **Bring-up:** B6.

USB power only, from the `USB_STAT` ladder: checked at Confirm (SKIP `on_battery`) and every second of the run, which stops at once when it goes (`stop=usb_lost`). Left sets the time (5, 10 or 20 min), Right the TX as in U1. Confirm runs, with auto-sleep held off and no loop delay, until the time is up or any key or tap:

- Wi-Fi TX without a pause (scans, or UDP to a joined network);
- `/.silkscreen-test/load.tmp`, 8 MB, written and read back in turn, every chunk compared. It needs 8 MB plus 16 MB free (`sd=no_space` otherwise), and with no card the load runs without it (`sd=none`);
- full-screen FAST refreshes back to back, T4's two frames alternating, with the elapsed time and the counts in a box.

At the end the radio goes off, the file is removed, and the screen is repainted with a HALF refresh; a file a reset leaves is removed at the next boot. Note the thermocouple's reading at the end. The row is INFO (FAIL on `sd_io`, `sd_data`, `sd_remove` or `radio_still_on`), with `mode=`, `dur_s=`, `ran_s=`, `frames=`, `sd=`, `sd_w_mb=`, `sd_r_mb=`, `sd_err=`, `sd_mismatch=`, `removed=`, `scans=`, `packets=`, `send_err=`, `pplus=`, `ssid=`, `stop=` and `mode_null=`.

### P5 Sleep, wake and sleep current

**Purpose:** every sleep path ends in the same sleep state and current, and only the power button wakes the board. **Bring-up:** B3, B10 (MEAS-01).

Set up MEAS-01 (POWER.md §4): a µA meter or logger in series with the cell, or 3.8 V, at J5; USB and any UART adapter off; the card fitted; the light off; J6 empty. The test image has no USB-serial wait, so its wake timing matches a release build.

Left/Right pick a path and Confirm arms it; Confirm again disarms, and so does leaving P5. The test only arms: the reader's own loop puts the board to sleep.

| Path | How | Expect |
|---|---|---|
| Long press | Hold POWER longer than 0.4 s | A power-button entry |
| Auto-sleep | The timeout at 1 min, in RAM. No USB data host: one holds auto-sleep off | A timeout 58-75 s after arming |
| Awake ceiling | On the cell. Timeout Never and the ceiling at 1 min, in RAM | A timeout 58-75 s after arming, not on USB power |
| Input cap | On the cell. Timeout Never and the input-only cap at 1 min, in RAM | As above |
| No input after a wake | On the cell. Hold POWER to sleep, wake it, touch nothing. The boot after that wake raises a shorter saved timeout to 5 min, in RAM, so it cannot fire first | The next sleep a timeout 178-200 s after the wake |
| Ghost wake | Hold POWER to sleep, then tap POWER briefly while it sleeps | Each tap awake 0.3 s at most, the screen unchanged (asked) |
| Release wake | POWER taped down through the sleep entry, then released | The floor + about 30 µA within about 5 s, then one short wake at the release (asked) |

The sleep screen leaves on the glass the path, the current targets and the pins to probe while asleep:

- about 3.3 V: IO47, IO14, IO11, IO38, IO39, IO10;
- IO48: about 3.3 V with an SSD1677 panel; with the UC8279D panel, note the level (not in hand);
- 0 V: IO12, IO13, IO21, IO40, IO5-7, IO15-17;
- IO42 below 0.385 V; `SD_VDD` (C36) below 0.5 V;
- the un-held RTC pads IO1-4, IO8 and IO9: probe and note them, since their sleep state is undocumented (POWER.md §7);
- IO41: unheld with an FT6336U, 0 V with a GT911;
- the back-feed probes (below): across `R5` under 20 mV, across `R34` about 0 mV, the J2 flex in and out a few µA apart.

Wait at least 60 s after the sleep screen, then average at least 60 s. While it sleeps, also take the GPIO47/48 back-feed probes of BRING_UP §4 (HW-01), with the meter on its 200 mV range:

- across `R5` (3V3 to `EPD_RST`, the 10 kΩ pull-up): each 0.1 V is 10 µA into IO47; under 20 mV passes;
- across `R34` (33 Ω in `BUSY`): each 1 mV is 30 µA into IO48; under 0.2 mV passes (proposal; the range's last digit is 3 µA);
- J2 flex in vs out: measure a path with the flex in, then again with it out (disconnect the supply before unplugging or plugging it). The two should differ by only a few µA (the panel draws 2-6 µA); 100-300 µA is back-feed, and 40-70 µA a panel that never reached deep sleep.

Wake with POWER and open P5. It shows:

- the path that slept, and how the entry came: a timeout or POWER, the time from arming or the uptime, and USB power, a USB host and touch at the entry;
- the reset reason, wake cause and ext1 mask;
- the time asleep, from the RTC.

Enter what the meter showed, one line at a time: the current (Left/Right 1 µA, Up/Down 10 µA), the J2 flex (in or out), across `R5` (1 mV, Up/Down 10 mV) and across `R34` (0.1 mV, Up/Down 1 mV). Confirm moves to the next line and records on the last; Back steps back, and on the first line skips the result. A line left alone is not measured. The ghost and release paths then ask what the meter showed. Above the budget the screen lists what a high reading points at (BRING_UP §6): +100-300 µA the IO47/IO48 back-feed; +18 µA `R36` and `R72` both fitted; touch +55 µA hibernated, +220 µA awake, about 1.1 mA with `TP_RST` LOW; mA the CE/D2 back-feed (HW-08) or an unparked SD bus (`SD_VDD` about 3.2 V).

PASS:

- the entry came the path's way, as in the table;
- the wake is `EXT1` on IO18;
- the current is at most 110 µA without touch, or 165 µA with a tail: the POWER.md §1 budget with the U3 ground current (row 4, still to measure) counted. Above 95 or 150 µA, the budget without row 4, the row carries `above_ds_budget=1` but does not fail. Typical is 58-73 and 113-130 µA; above 150 µA work through the A/B list in POWER.md §4;
- within 3 µA of the other paths measured in the same set-up (touch, and the J2 flex in or out) since the last reset ("about 2 µA", plus the meter's last digit; the readings are kept in RTC memory);
- across `R5` under 20 mV and across `R34` under 0.2 mV, when measured (`r5_backfeed`, `r34_backfeed`);
- with an SSD1677 panel, the latest flex-in and flex-out readings in the same touch set-up at most 8 µA apart (`flex_gap`). The UC8279D panel's own sleep draw is not in hand, so there the gap is recorded, not judged;
- for the ghost and release paths, the operator saw what the table says.

The record adds `flex=in|out`, `r5_mv=` with `r5_ua=`, `r34_mv=` with `r34_ua=` (`n/a` when not measured), and `flex_gap_ua=<in - out>:<panel|panel_awake|backfeed|unclear>` once both are measured. With no current entered, or "Not measured", the result is INFO. The loop and boot cutoffs, three forced panics and the SD-error screen's 60 s timeout are sleep paths too; they need the reader image or a bench supply (below), and no image forces a panic (step 10 of the 3.68" variant's bench list says how to with a scratch patch).

### P8 Reader-data integrity

**Purpose:** the test firmware leaves the reader's data alone: it must never write `/.crosspoint`. It is not a bring-up step; it guards the card the reader shares.

The first boot of the test firmware with a card writes `/.silkscreen-test/crosspoint.sha`. It holds the SHA-256 and size of every file directly in `/.crosspoint`, up to 32: settings, state, Wi-Fi, recent books, OPDS, KOReader and the Quick Resume frame. It also holds the names of the folders there, in directory order (one per book cache; their contents are not hashed).

- Every later boot compares the card with it and records a P8 row (`when=boot`): PASS when nothing changed, FAIL with the files that did. The boot that takes the baseline records INFO.
- The screen compares again when it opens, lists each file with its state, and Confirm records a check (`when=on_demand`).
- After running the reader image in between, which changes these files itself, take a new baseline (Right; it asks first).

PASS: every file the same, none missing or new, the same folder names, `/.crosspoint` there or not as before, and no I/O error. Run P8 before and after a session with sleeps, Wi-Fi and light changes. After flashing the reader back, check that it resumes into its book.

### P9 Render watchdog

**Purpose:** the render watchdog ([POWER.md AW-04](POWER.md#31-the-render-watchdog-aw-04)) ends a session whose `render()` never returns, or whose sleep entry never reaches sleep, on battery, in the sleep state, whatever the stuck task holds, and the next boot says so. **Bring-up:** none: it is a firmware rule, and it needs the board for the pins and the current. Not run on a board yet.

Set up as for P5 (POWER.md §4, MEAS-01): a µA meter or logger in series with the cell at J5, the card fitted, the light off, J6 empty. Then **take USB off**: the watchdog acts on battery only. (With USB in, P9 arms the same way and nothing happens until the cable is pulled; see below.)

1. Open P9. The screen shows the watchdog task (running), the supply, the reader's bounds (300 s for a render without progress, 180 s for a sleep entry) and the bound P9 sets (30 s, in RAM only).
2. Confirm ("Hang") or Right ("Hang+ADC") asks "Hang render()?". Pick "Hang render()" (Up/Down, Confirm). The screen draws once more, with the arm row and "Hung", and the keys stop: the render task now holds **the render lock and the SPI bus twice over** (a nested `SPI.beginTransaction()`, which is what the 3.68" panel's freeze was), and with Right **the ADC lock** as well. The loop task should stall on the SPI lock at its next clock change (`setCpuFrequencyMhz()` takes it), and on the ADC lock in the key poll when that is held, so the watchdog has to read the charge pin itself (the row cannot show whether the loop stalled).
3. About 30-35 s later the chip restarts and sleeps. With a monitor on the UART you would see `[RWD] render() has run for ...` and then `Render watchdog: render() of Test P9 ... sleeping`; on battery there is usually none. The panel keeps the "Hung" page. Measure the sleep current from 60 s after the restart: it must be the floor of POWER.md §1, the same as after any other sleep path (P5 lists what a high reading points at). The pins are in section 2's sleep state.
4. Press POWER. The board boots normally into the menu (no abnormal reset is counted). The first opening of the menu records the P9 row.
5. **The sleep entry.** Left ("Hang sleep") asks "Hang the sleep entry?". Pick "Arm". The arm row (`mode=sleep`) is written and the screen says "Armed"; nothing hangs yet. Hold POWER until the sleep entry starts (or let a timeout do it): `enterDeepSleep()` sets its mark and then hangs, before the sleep screen is drawn, so the panel keeps the "Armed" page, the keys stop and the render task is idle with the render lock free. About 30-35 s later the chip restarts and sleeps; the log lines are `[RWD] the sleep entry has run for ...` and `Render watchdog: the sleep entry had run for 30 s without sleeping at the restart; sleeping`. The sleep current and the wake are as in steps 3 and 4. Back, while armed, disarms and stays; a second Back leaves the screen (which disarms too).

**Reading the result** (`results.csv`, test `P9`; the same `key=value` pairs go to the serial log):

- `action=arm` (INFO, written when you confirm): `mode=spi|spi_adc|sleep bound_ms=30000 supply=battery|usb expect_ms=30000..36000`.
- `action=watchdog`, written at the boot after the wake: `kind=render` and `activity=Test_P9` is the screen whose `render()` hung (`kind=sleep` and `activity=sleep_entry` is the sleep entry; P9 knows the restart was its own from a note in RTC memory that the hang writes just before it starts); `stuck_ms` is how long it had run when the watchdog acted; `uptime_ms` is the session's length; `stack_free` is the most the watchdog task's 4096-byte stack was free at the restart (the lowest it reached); then `fails=` and `warns=`.
- **PASS:** the activity is `Test_P9` (`kind=render`) or the restart is P9's sleep-entry hang (`kind=sleep`, `activity=sleep_entry`), `stuck_ms` is 30000-36000 and `stack_free` is at least 1024.
- **FAIL:** `early` (`stuck_ms` under 30000: the bound was not honoured) or `stack` (under 512 bytes free).
- **WARN:** `late` (`stuck_ms` over 36000: expected after arming on USB, where the watchdog waits for the cell and acts within a tick of the cable coming out; on battery it needs a look) or `stack` (512-1023 bytes free).
- **INFO** with `caused_by_p9=0`: the watchdog ended a session of the test firmware by itself, in the screen named (or in the sleep entry), after the reader's bound (5 minutes without progress, or 3 for the sleep entry).
- **No `action=watchdog` row:** the restart did not happen, or the record did not survive it. If the board was still awake after a minute, the watchdog did not act: note the supply, read POWER.md §3.1 and report the serial log of the next boot.

Two things the row cannot see, to check by hand: that the current really is at the floor, and that the panel still shows the "Hung" page (the driver-free panel sleep keeps the image).

**With USB in** when P9 is armed (the arm row says `supply=usb`): with **Hang** nothing happens, whatever the time; recovery is SW11 (reset). Pull the cable and the restart comes within about 10 s (the charge ladder needs two readings a second apart to change its answer, then the watchdog's next 5 s look); the row is WARN `late`, which is right. This is the USB rule's check, and the board draws from USB while it waits, so the test costs the cell nothing. With **Hang+ADC** the pin cannot be read (the render task holds the ADC lock), so once the loop has been stale for 15 s the watchdog acts as on battery, USB or not, at 30-35 s: the row is a PASS, and the supply the arm row names is the only sign that USB was in. That is the documented bias towards sleeping ([POWER.md AW-04](POWER.md#31-the-render-watchdog-aw-04)), and the check that a held ADC lock does not stop the watchdog.

**What it holds, and why:** the claim being tested is that the expiry path waits for no lock the stuck task can hold, so the hung `render()` holds what a stuck display driver could: the render lock (`TestScreen::render()` leaves it with the caller) and the SPI bus's two locks, and with Right the ADC lock too. The source and machine-code checks show the path does not name a lock; this shows it does not wait for one. The sleep-entry hang holds nothing but the loop task (it is a `vTaskDelay` loop in `enterDeepSleep()` right after the mark, before `HalPowerManager::Lock`, the sleep screen and the plugin delivery): it shows that the second mark fires with the render task idle and the render lock free, which is the case the render stamp cannot see. Nothing is written but `results.csv` rows. The same on `silkscreen-368-test`.

## Safety

- **Front light.** Ideally the TPS923610 boost never runs without a load; an unloaded enable is safe on the standard build (U10 latches off at its OVP, HARDWARE.md §13.1), and the SDK's switch-on probe takes the light dark at its first reading above the trip, inside its 500 ms window. A front-light test asks before each lighting session (T9's Confirm, one T10 run) which load is connected (the J3 panel light or a J6 strip, with "No load" preselected), and lights nothing without an answer; a T10 run then relights twice on that load by itself (retry, restore), and T9's switch run changes colour 30 times on it, stopping at the first trip. T9 does not light when `LED_MONIT` reads nothing in the dark, switches the light off above 22 V, and is dark before its Record question. T10 lowers the trip level in RAM only, never raises it, and puts it back on every way out. The one deliberate unloaded enable is T10's step 0 (BRING_UP §7.4): opt-in, at the profile's trip level, only after the operator names the driver build (the TPS923611 swap refused unless `D3` is an SMAJ33A, and refused outright on a cut-down board, which has no `D3`) and confirms `J3` and `J6` empty, once, through the reader's switch-on, followed by the 10 s advice. Nothing else in the test firmware can light it.
- **Render watchdog (P9).** P9 hangs the render task, or the next sleep entry, on purpose and the keys stop; on battery the board restarts into sleep about 30-35 s later, and with USB in it stays hung until the cable is pulled or SW11 (reset) is pressed. The bound it sets is in RAM and the reader's constant is not touched. It writes only `results.csv` rows.
- **Touch.** `TP_RST` is pulsed only by the SDK's own reset (the boot probe, T12's reset path); no test holds it LOW or writes a controller configuration register. T11's mapping changes are in RAM only.
- **Pins.** Tests never call `pinMode` or `gpio_reset_pin` on IO1, IO2, IO4, IO8, IO9, IO10, IO18, IO40, IO42 or the SD pins (POWER.md ADC-01, REQUIREMENTS PWR-19), and every ADC read goes through `freeink::adc`. T2 reads `IO9` itself only in stage 5's 12 s capture. The boot checks are the SDK's, as in the reader: the ladder test turns each ladder pin's pulls on for about 2 ms at a reset other than a deep-sleep wake, and IO18 is read on the ADC with no pull.
- **Power button fault.** A board whose IO18 idles between the logic levels boots into the test menu (P1 and T3 fail `power_idle`), but none of its sleeps has a wake source: press SW11 (reset) to start it again.
- **Battery.** A screen that holds auto-sleep off also holds the CPU at full clock. Live readings on battery run for a bounded time or only on USB. The runs that hold auto-sleep off are all bounded: P3's verify and its power cycles (about 20 s), T4's scope loop (5 min), T9's switch run (about 35 s), T10's runs (about 5 s; step 0 about 15 s), T13's 60 s check, T15, U1 (5 min at most) and U2 (20 min at most, USB only). The 15-minute ceiling, the 60-minute input cap and the low-battery cutoff are the reader's and still apply on battery.
- **Sleep and wake.** No new sleep or wake source; `scripts/check_sleep_wake_sources.sh` is unchanged and covers the test code. Timing across a sleep uses a power-button wake and a stopwatch.
- **Card.** Besides `/.silkscreen-test/`, the only writes are the reader's own: `/crash_report.txt` after a panic, `/screenshots/` (POWER + Down), and the host during USB Drive.
- **Display.** The display tests change only Sunlight Fading Fix, night mode (both through `SETTINGS`) and the panel SPI clock, all in RAM, and put them back at the end of every run and on exit. They never turn the radio on during a refresh (PWR-02).
- **RTC.** T13 writes the RTC only through `Rtc::set()` (the time, then OSF cleared) and never the control register, so EOSC stays 0 (rtc-eosc-zero). Its 400 kHz bench puts the bus back at its own clock. T15 sets the RTC through `HalClock::syncFromNTP()`, as the reader does.
- **Wi-Fi.** T15, P3's overlap, T3's idle station and U1 bring the radio up only on USB or with P+ at 3.4 V on the cell (bat-wifi-gate; P3's overlap and U2 only on USB, checked again every second of the run). The one exception is U1 on a bench supply the operator names in place of the cell, for the brown-out sweep (BRING_UP §3.3); the reader's cutoff still sleeps the board at about 3.3 V. T15, P3, U1 and U2 switch the radio off (`WIFI_MODE_NULL`) before they write or draw a result, and U1 repaints only between its bursts. T3's station never joins a network or scans, so it sends nothing; its screen keeps the reader's sleep timeout. Leaving any test switches the radio off. Wi-Fi credentials are read, never written.
- **Card.** P3 writes only `/.silkscreen-test/sdcheck.tmp`, needs 3.6 V on the cell, and removes the file at the end, on Back and on exit; U2 writes only `/.silkscreen-test/load.tmp`, on USB, and removes it the same way; a file a crash leaves is removed at the next boot. P3's power cycles use the reader's own shutdown (cached sectors written back first) and boot mount, and mount the card again on every way out. P4's USB Drive gives the host the whole card, after a warning, and P8 shows what changed.
- **Sleep paths.** P5 lowers the sleep timeout, the awake ceiling or the input cap to 1 min in RAM only (the timeout goes to Never for the ceiling and the cap), and puts them back when disarmed or left. The boot after a "no input after a wake" arming raises a saved timeout under 5 min to 5 min, in RAM, for that boot only. It never sleeps by itself: the reader's loop does, through `enterDeepSleep()`.

## Checks that need the reader image

The pinned settings, the no-write rule and the tests left out (P6 forced panics, P7 the low-battery ramp) take these paths out of the test firmware. Run them on the reader image (`silkscreen`, or `silkscreen-gh_release` for current and wake timing; `silkscreen-368` and `silkscreen-368-gh_release` on the 3.68" variant), with the bench equipment named:

- **Wake policy.** A click wake: Short Power Button = Sleep leaves `WAKE_POLICY_CLICK`, and the early wake decision then boots on a click. The test firmware pins Ignore, so every entry leaves the hold rule.
- **Resume.** Quick Resume and the splash-less wake (`sleep_frame.bin`, `APP_STATE`); the test firmware neither reads the state nor writes the frame.
- **Front light across sleep and restarts** (B8):
  - Restore Light on Wake;
  - with no light connected: switching it on shows "No front light detected" with the 10 s advice, and the light control stays for a retry; in the light panel, dragging the slider afterwards only sets the level (no new `load probe` lines until the lamp toggle);
  - a restore that finds no light (the light unplugged while asleep) shows the same message once the first screen is up and is not repeated at the next wake; a power cycle tries once;
  - a light trip shows the same message;
  - a single-colour J6 strip: the warmth row names the one string there is and ignores input, and after a power cycle the first switch-on lights that string at once (the saved warmth);
  - a ghost wake (short tap) must not light the LEDs;
  - a light trip stays off across a sleep, a wake and a Wi-Fi screen's silent restart (the saved preference);
  - IO42 and IO40 at 0 V after sleeping with the light on, warm and cool (ZD-08);
  - ADIM falls at least 3 ms before COLOR_SEL across a silent restart with the light on (a scope; the shutdown handler in `HalFrontlight::begin()`).

  The test firmware switches the light off before every sleep and restart, so none of these happens there.
- **No card.** The SD-card error screen and its 60 s sleep on battery (`storageErrorSleepS`, a MEAS-01 path), and "no card gives a clean SD card error with the bus parked" (B4). The test firmware opens its menu without a card.
- **Power button fault.** With R72 fitted next to R36 (a rework only), the reader shows "Power button fault ..." in place of the SD-card error or Home, stays on it while `USB_STAT` shows external power, and sleeps on the cell with no wake source; SW11 starts it again. The test firmware reports the fault but boots on.
- **Battery cutoff** (B6, BAT-01), with a bench supply on J5:
  - ramp it down: the reader sleeps at about 3.40 V, and at about 3.30 V as read with the web server running;
  - 10 min after the cutoff, the resting P+ is at least 3.5 V;
  - the chip's behaviour in LDO dropout: brown-out or a reset loop, and at what voltage (POWER.md §7);
  - HW-08's cross-check: Charging or Complete with P+ at or below the cutoff and not rising for 5 min counts as on battery. T2 shows the flag, but only a supply at the cutoff with `USB_STAT` held in the charging window drives it.
- **Web server and updates.** The 15-min ceiling (or the sleep timeout, if longer) with the web server idle, WebDAV polling not counting as traffic, and `IO8` with the web server running (the 100 mV allowance under its load; T15's idle station is lighter). OTA and the SD firmware update refusing below 3.6 V (the test firmware has neither), and the radio released at the OTA, Clock Sync and KOReader result screens, then the restart on exit.
- **Reset loop** (WK-03): 3 forced panics in a row, with the panel refreshed fast before them. The device then sleeps at the floor, with the driver-free panel and touch sleep. With the custom panel, check that the log names UC8279 before the driver-free sleep, and that the floor matches the one after a menu sleep. No image has a panic hook (P6 is not built, and none is added to a shipping image): force the panics with a scratch patch that is never committed, `abort();` as the first statement of `loop()` in `src/main.cpp`, then restore the file and reflash. The 3.68" variant's bench list (step 10) walks through it, and its 60 ms driver-free settle has only a host test behind it.
- **Reader overlay** (B2): on both panels, open the toolbar menu over a page with anti-aliased text and close it. The page is re-rendered with its gray levels, as on the X4 (`xteinkClassPanel()`).
- **Clip selection, clippings list and toolbar cursor with keys beside touch** (B9; a unit with the touch tail, a book with dialogue and short lines, a dictionary and a saved clipping):
  - hold Right: the cursor repeats about every 0.5 s and turns the page at a page end with no word skipped or repeated; after the release at most one or two more steps run and the panel is quiet; at the last loaded word it stops (no wrap);
  - tap Right quickly while a refresh runs: every press counts; press Right twice during a refresh and then tap a word: the cursor ends on the tapped word (a queued press must not replay after it);
  - the key cursor stays fully visible, clear of the key hints: the last line of a portrait page; the FIRST word of a line in landscape CW; the LAST word of a line in landscape CCW (the band is on the right there); the first line in portrait inverted (the page moves down);
  - long-press a word to start a selection in each orientation: on a word inside a band zone, and on a page whose middle line is one short word at the edge ("No."). Exactly the pressed word is selected, with handles (the page may then shift to keep it clear of the hints), never its neighbour and never an immediate exit; Confirm saves it;
  - Confirm anchors, Left, Right, Up and Down extend across a page end; the second Confirm opens the list Look Up, Clip, Bookmark with Clip chosen: Confirm clips, Up and Down move the choice, Back closes only the list and keeps the range, each entry works by key and by a tap, and a "too long" selection shows its popup and Back returns to the range; from Reader menu > Look Up the second Confirm looks up at once; a keys-only unit (a Silkscreen without the tail, an X4) clips at once as before; drag a handle on a shifted page; tap Look Up, Clip and Bookmark on the action bar: handles and bar sit where the text is;
  - touch while keys were just used and the reverse: no stuck state, no double action; with the error popup up ("too long" needs a synthetic page of 18-letter tokens or CJK text) Back closes only the popup, and a swipe or a tap outside closes it once with the selection unchanged;
  - a thumb resting on the glass: single steps and no repeat (a known limit); lifting it brings the repeat back;
  - Settings > Controls: swap Left and Right and toggle Orient Front Buttons: the arrows move the cursor the way the hints show in all four orientations, and the repeat follows the remapped key;
  - Reader menu > Look Up: the cursor is visible at the same edges, Confirm opens the definition, Back returns to a selection that is still right;
  - View Clippings: "Hold Select to Delete" is fully on screen above the key hints (Lyra, Classic and RoundedRaff; both landscapes and inverted; Hungarian does not truncate) and a held Confirm of 0.7 s deletes after confirmation;
  - Settings > Reader menu style > Toolbar: the first Right moves the pill from Contents to Text, a reopened toolbar starts on tool 0, More opens with row 0 highlighted and the first Down goes to row 1; a tap on a row hides the cursor and the next key shows it on the tapped row;
  - a dev-env serial log: no "Selection button queue full", heap at exit equal to heap at entry; a Silkscreen without the tail (C2) behaves as before this round, the incremental redraw included;
  - on a meter with USB off: a selection left open falls back to the reader page's idle current after the 3 s clock drop and auto-sleeps at the timeout; a key held at the last word, or a resting finger, causes no refresh.

Bench checks no test firmware screen can make, for a meter, a scope or esptool:

- B1: from EN release, CS (IO14) HIGH before the first SCLK (IO13) edge, IO42/IO40 LOW, IO10 HIGH;
- B3: `esptool chip_id` after a wake and after an SW11 reset (HW-01's flash back-feed symptom), and IO47 on a scope across a ghost wake (no LOW excursion);
- B3, B10: ghost-wake timing on a current logger, with and without a card;
- B4: `SD_VDD` at cold boot, below 0.5 V for at least 20 ms with the bus LOW before power-on (sd-coldboot-powercycle), and below 0.5 V in sleep;
- B9: unplug the touch tail while asleep; the next cold boot finds it absent;
- POWER.md §5: the awake idle current at 80 MHz (about 25-40 mA), and result screens idling at the Wi-Fi-off level.

## Writing a test

- Derive from `testsuite::TestScreen` (`src/activities/testsuite/TestScreen.h`): draw the body in `drawBody()`, name the Confirm/Left/Right actions in `slotLabel()`, act in `onSlot()`, `onStep()` and `onTick()`, and record results with `record(Verdict, "key=%d ...", ...)`. A record whose checks pass with a reading out of its nominal band is `Verdict::Warn`, with `warns=` naming it. One file per test in `src/activities/testsuite/tests/`; declare its factory in `tests/TestFactories.h` and set it in `TestCatalog.cpp`.
- `drawBody()` runs on the render task. Take readings, I2C traffic and `usbHostAttached()` in `onStart()`, `onTick()` or `onSlot()` (the loop task), and draw from what they stored.
- A live reading repaints at most every few seconds (T1 and T2 use 5 s): every repaint runs at the full clock, and a screen that repaints often keeps the board off its idle clock on battery. Never hold auto-sleep off for a live reading.
- Helpers: `ask()` puts a question to the operator and answers through `onAnswer()`; `msSinceInput()` gives the time since the last key or touch input; `TestPage::setCompact()` switches rows to the small font; `appendf()` and `addFailure()` (`tests/TextBuffer.h`) build a record's text. A test whose input is the keys returns true from `keysAreTestInput()` and reads `HalGPIO` itself; its Confirm/Left/Right actions then run on a 1.5 s hold as well as on the touch buttons.
- A test whose input is the touch panel returns true from `touchIsTestInput()` while it is (T11): the touch buttons are not routed and a touch Back (header button, left-edge swipe) does not leave, so only the keys drive the screen. It can keep the bottom-edge swipe from going home by overriding `handleHomeGesture()`. `showsLastResult()` returning false leaves the whole band to the body.
- UI text is English `constexpr` strings in the test code, not `tr()` keys. This is a deliberate exception to `.agents/rules/hardware-resources.md` rule 5: `gen_i18n.py` collects `STR_` keys from every source file whatever its `#if`, so test-only keys would ship in every reader image. Shared screens (popups, Wi-Fi, keyboard) keep `tr()`, with the language pinned to English.
- The env builds the SDK with `-DFREEINK_DIAGNOSTICS=1`, so the bring-up records in the freeink-sdk README ("Bring-up diagnostics") are available.
- The main loop and the render task re-apply `SETTINGS.fadingFix` and `SETTINGS.screenInverted` on every pass: a display test changes those fields in RAM, never the renderer or the HAL directly.
- A display test derives from `PanelRunTest` (`tests/PanelRun.h`). `startRun(steps)` starts a run. `runStep()` draws one frame and refreshes it on the render task. `afterStep()` stores the measurement, and calls `pauseRun()` to hold the frame, or `endAfterThisStep()` to end the run there. `stopsOnAnyInput()` makes any key or tap end a run as Back does (T4's scope loop). `onRunEnd()` asks and records after the closing HALF repaint. `restorePanel()` puts back anything the run changed on the panel. `refreshTimed()`, `refreshAsyncStart()`/`refreshAsyncFinish()` and `grayTimed()` return a `RefreshSample` with what the driver ran. `tests/PanelPatterns.h` has the frames, and `tests/GrayPass.h` has the reader's gray-pass routes.
- `TestScreen` also offers `onBack()` (use Back for a sub-mode, or to stop a run), `edgeBands()` (edge bands the test draws itself, such as T7's rulers) and `requestCleanRepaint()` (the next repaint is a HALF).
- A test that needs an SDK object behind the HAL uses `TestSuiteAccess` (`src/activities/testsuite/TestSuiteAccess.h`). The HAL class declares it a friend under `#if CROSSPOINT_TEST_SUITE`, so the reader gains no API. `HalDisplay` (the panel driver), `HalGPIO` (`InputManager`), `HalFrontlight` (`FrontlightManager`), `HalClock` (its RTC driver and read cache) and `HalStorage` (its mutex) do so today.
- A front-light test calls `confirmLightLoad()` and lights the boost only in `onLightLoadConfirmed(load)`, which says whether J3 or J6 was named. `tests/LightCheck.h` reads `LED_MONIT` and the SDK monitor's record, reads a pad's drive from the registers, and lowers and restores the trip level.
- Scratch files go in `/.silkscreen-test/` and end in `.tmp`, never `.txt` (a book type).
- The RTC: `tests/RtcAccess.h` gives HalClock's own driver (`Rtc::address()`/`type()` say which part `begin()` found), raw register reads, UTC epoch arithmetic, and `readAtTick()`, which times the RTC to its next seconds tick. A test that writes the RTC calls `rtc::noteWritten()`, so HalClock reads the chip again and T14 does not judge a span across the write. `TestSuiteAccess` reaches the driver behind `HalClock`, and HalStorage's mutex for the SD manager's bring-up record.
- A test that needs the sleep entry this boot woke from reads `SleepRecord.h` (`wokeFromSleep()`); P5 arms a path with `armSleepPath()`. `ActivityManager::goToSleep()` and the sleep screen fill the record, never a test.
- `BoardOptions.h` says what the operator set in OPT (`options::cutDown()`, `options::up2IsPower()`).
- `tests/Supply.h` reads what powers the board (P+, USB power, a cell) for a gate or a judgement on the cell; `tests/Sha256.h` wraps mbedTLS's SHA-256.
- A test holds auto-sleep off or skips the loop delay (`preventAutoSleep()`, `skipLoopDelay()`) only while a bounded run is on (P3, T4's scope loop, T9's switch run, T10, T13's 60 s check, T15, U1, U2), and every such run has a time limit of its own.
- `TestPage::paragraph(text, maxLines)` ends the last line with an ellipsis when the text needs more lines: give instruction text a cap that fits it, and keep it short on screens with many rows (the full procedure is in this file). `ask()` bodies show up to 12 lines.
