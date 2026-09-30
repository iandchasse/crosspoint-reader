# Silkscreen hardware test firmware

A second firmware image for bring-up. It is built from this branch as `[env:silkscreen-test]` (`-DCROSSPOINT_TEST_SUITE=1`), boots straight into a menu of separate tests and cannot open a book. It runs the reader's own boot, sleep and wake code and the same SDK managers, so a failure in a test points at the board, not at a second implementation. The bring-up steps it serves are in [PORTING.md](PORTING.md#bring-up-checklist-real-board), and the sleep budget and pin rules in [POWER.md](POWER.md).

Everything test-only is under `#if CROSSPOINT_TEST_SUITE` or in `src/activities/testsuite/`, which the reader envs compile to nothing. The reader image does not change.

## Build and flash

```bash
pio run -e silkscreen-test -t upload
```

- The env inherits `[silkscreen] frontlight_flags`, so a board with no LED load that drops the light in `platformio.local.ini` drops it here too.
- Go back to the reader over USB: `pio run -e silkscreen -t upload`. The two images carry different board tags (`silkscreen-test` and `silkscreen`), so neither accepts the other through OTA or the SD firmware update, and the test image has neither.
- While a USB data host is attached the test firmware holds auto-sleep off, so esptool can reach it. If the board is asleep, hold POWER to wake it first.
- CI builds the env and keeps `firmware-silkscreen-test.bin` as a build artifact. It is never a release asset.
- `bash scripts/check_test_firmware.sh` (after a build) checks the image, as CI does: no reader, library, book-parsing, web-server, OTA, SD-update or light-panel code is linked, and the board tag `CROSSPOINT-BOARD-V1:silkscreen-test;` appears exactly once.

## How the test firmware differs from the reader

| Area | Test firmware |
|---|---|
| Boot | `setup()` runs unchanged up to the routing: release wake, reset loop, early wake decision, touch probe, boot cutoff, SD, settings. Then the splash, then the test menu, on every boot. After a panic, the crash screen first, then the menu. |
| What is built | The reader, library, home screen, file browser, image viewer, OPDS browser and the reader's sleep screen are left out of the build (`build_src_filter`), so a live reference to any of them fails the link. Settings, the web server, OTA and the SD firmware update are compiled but unreachable, and the linker drops them. Every reader entry point (`goHome`, `goToReader`, `goToSettings` and the rest) lands on the menu. |
| Settings | `/.crosspoint/settings.json` is read once, then these are pinned in RAM: night mode off, Sunlight Fading Fix off, portrait, Short Power Button = Ignore, light off with no restore on wake, the keys in their default roles, English, the Lyra theme, no SD reader font, and a 10-minute sleep timeout if it was Never. |
| Card writes | Never under `/.crosspoint/`: every settings, state, Wi-Fi, recent-books, OPDS and KOReader save is a no-op, `state.json` is not read, and the Quick Resume frame is neither written nor removed. The test firmware writes only `/.silkscreen-test/`. |
| No card | The menu opens with "SD not mounted" and results go to the serial log only. The reader's SD-card error screen and its 60 s timeout are not in the test firmware. |
| Front light | Off at every boot and after every silent restart, with no light panel (gesture, status-bar tap). Only a front-light test lights it, after the operator confirms a load. The over-voltage trip handling is the reader's. |
| USB | While a USB data host is attached, auto-sleep is held off (the USB Serial/JTAG host frames, with a 2 s hold). A charger alone or a cell never holds it. The 15-minute ceiling and the 60-minute input cap still apply whenever the `USB_STAT` ladder does not report external power, so a misread ladder shows up as a sleep during a bench session. |
| Sleep | The same sleep entry, wake source and timeouts as the reader. The sleep screen shows the firmware and board tag. A power-button click does nothing and a hold longer than 400 ms sleeps; waking needs POWER held. |

## Using it

**The menu.** Up/Down or Left/Right move, Confirm opens. With touch, tap a row, and swipe to scroll. Back does nothing here. The line under the title says where results go (`/.silkscreen-test/results.csv`, or "SD not mounted") and shows the firmware version. Each row shows the test id and name, what it checks (or "Not implemented yet"), and the last result this boot. With touch, a swipe up from the bottom edge returns to the menu from any test.

**A test screen** has the test id and name in the header, the last result recorded on this screen, the test's instructions and live readings, and four touch buttons along the bottom. The buttons sit in the order of the front keys and do what the keys do: Exit (Back), then the test's Confirm, Left and Right actions. Up/Down step values where a test has any. Back, the header's back button or Exit leaves the test. A test that reads the keys itself (the keys test) leaves only on Exit or a 1.5 s Back hold, and says so. Leaving any test switches the front light and the radio off.

**Results.** Each result is shown on the screen, logged to serial and appended to `/.silkscreen-test/results.csv`:

- Serial: `[INF] [TEST] TEST,<id>,<verdict>,<details>`.
- CSV columns: `timestamp,time_source,session,board,firmware,test,verdict,details`. The timestamp is the RTC's local time when the RTC holds a valid time (`time_source` = `rtc`), otherwise seconds since boot (`uptime`). `session` is random per boot, `board` is the end of the Wi-Fi MAC, and `details` is quoted, with `key=value` pairs by convention.
- Verdicts: `PASS`, `FAIL`, `SKIP` and `INFO` (a reading with no pass criterion).
- Tests keep scratch files in `/.silkscreen-test/` with names ending `.tmp`. Any left behind by a sleep or a crash are removed at the next boot.

## Tests

The ids are stable; `results.csv` and the serial log use them.

| Id | Test | Bring-up step | Status |
|---|---|---|---|
| P1 | System info and boot checks | B1 | Not implemented yet |
| P2 | I2C bus scan | B7, B9 | Not implemented yet |
| T1 | Battery (`BAT_MONIT`) and cell detection | B6 | Not implemented yet |
| T2 | Charge status (`USB_STAT`) and USB presence | B6 | Not implemented yet |
| P4 | USB presence and USB Drive | B4, B6 | Not implemented yet |
| T3 | Keys: eight ladder keys and the power button | B5 | Not implemented yet |
| T4 | Refresh modes | B2 | Not implemented yet |
| T5 | Grayscale | B2 | Not implemented yet |
| T6 | Inversion | B2 | Not implemented yet |
| T7 | Orientation and bezel ruler | B2 | Not implemented yet |
| T8 | 40 MHz panel SPI trial | B2 | Not implemented yet |
| T9 | Front light | B8 | Not implemented yet |
| T10 | Front-light over-voltage trip | B8 | Not implemented yet |
| T11 | Touch probe, mapping and gestures | B9 | Not implemented yet |
| T12 | Touch sleep command | B9 | Not implemented yet |
| T13 | RTC | B7 | Not implemented yet |
| T14 | RTC across deep sleep | B7 | Not implemented yet |
| P3 | SD card | B4 | Not implemented yet |
| T15 | Wi-Fi and NTP | B7 | Not implemented yet |
| P5 | Sleep, wake and sleep current | B3, B10 | Not implemented yet |
| P8 | Reader-data integrity | - | Not implemented yet |

## Safety

- **Front light.** The TPS923610 boost must never run without a load. A front-light test asks, every time, that the J3 panel light or a J6 strip is connected, and lights nothing until the operator confirms. Nothing else in the test firmware can light it.
- **Pins.** Tests never call `pinMode` or `gpio_reset_pin` on IO1, IO2, IO4, IO8, IO9, IO10, IO18, IO40, IO42 or the SD pins (POWER.md ADC-01, REQUIREMENTS PWR-19), and every ADC read goes through `freeink::adc`.
- **Battery.** A screen that holds auto-sleep off also holds the CPU at full clock. Live readings on battery run for a bounded time or only on USB. The 15-minute ceiling, the 60-minute input cap and the low-battery cutoff are the reader's and still apply on battery.
- **Sleep and wake.** No new sleep or wake source; `scripts/check_sleep_wake_sources.sh` is unchanged and covers the test code. Timing across a sleep uses a power-button wake and a stopwatch.
- **Card.** Besides `/.silkscreen-test/`, the only writes are the reader's own: `/crash_report.txt` after a panic, `/screenshots/` (POWER + Down), and the host during USB Drive.

**Checks that need the reader image.** The pinned settings and the no-write rule take these paths out of the test firmware, so run them on the reader:

- a click wake (Short Power Button = Sleep) and Quick Resume;
- Restore Light on Wake, a ghost wake that must not light the LEDs, and a light trip that stays off across a sleep, a wake and a Wi-Fi screen's restart;
- the SD-card error screen and its 60 s sleep.

## Writing a test

- Derive from `testsuite::TestScreen` (`src/activities/testsuite/TestScreen.h`): draw the body in `drawBody()`, name the Confirm/Left/Right actions in `slotLabel()`, act in `onSlot()`, `onStep()` and `onTick()`, and record results with `record(Verdict, "key=%d ...", ...)`. Set the entry's factory in `TestCatalog.cpp`.
- UI text is English `constexpr` strings in the test code, not `tr()` keys. This is a deliberate exception to AGENTS.md rule 5: `gen_i18n.py` collects `STR_` keys from every source file whatever its `#if`, so test-only keys would ship in every reader image. Shared screens (popups, Wi-Fi, keyboard) keep `tr()`, with the language pinned to English.
- The env builds the SDK with `-DFREEINK_DIAGNOSTICS=1`, so the bring-up records in the freeink-sdk README ("Bring-up diagnostics") are available.
- The main loop and the render task re-apply `SETTINGS.fadingFix` and `SETTINGS.screenInverted` on every pass: a display test changes those fields in RAM, never the renderer or the HAL directly.
- A front-light test calls `confirmLightLoad()` and lights the boost only in `onLightLoadConfirmed()`.
- Scratch files go in `/.silkscreen-test/` and end in `.tmp`, never `.txt` (a book type).
