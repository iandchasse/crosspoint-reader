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

**A test screen** has the test id and name in the header, the last result recorded on this screen, the test's instructions and live readings, and four touch buttons along the bottom. The buttons sit in the order of the front keys and do what the keys do: Exit (Back), then the test's Confirm, Left and Right actions. Up/Down step values where a test has any. Back, the header's back button or Exit leaves the test. In a test that reads the keys itself (T3), a front key held 1.5 s does what the button above it does (Back: Exit), and the screen says so. Leaving any test switches the front light and the radio off.

**Questions.** When a result needs something only the operator can see (T2 asks whether D2 is lit), a list of answers opens: Up/Down and Confirm, or tap an answer. Back, or a tap outside the list, records nothing.

**Results.** Each result is shown on the screen, logged to serial and appended to `/.silkscreen-test/results.csv`:

- Serial: `[INF] [TEST] TEST,<id>,<verdict>,<details>`.
- CSV columns: `timestamp,time_source,session,board,firmware,test,verdict,details`. The timestamp is the RTC's local time when the RTC holds a valid time (`time_source` = `rtc`), otherwise seconds since boot (`uptime`). `session` is random per boot, `board` is the end of the Wi-Fi MAC, and `details` is quoted, with `key=value` pairs by convention.
- Verdicts: `PASS`, `FAIL`, `SKIP` and `INFO` (a reading with no pass criterion).
- Tests keep scratch files in `/.silkscreen-test/` with names ending `.tmp`. Any left behind by a sleep or a crash are removed at the next boot.

## Tests

The ids are stable; `results.csv` and the serial log use them.

| Id | Test | Bring-up step | Status |
|---|---|---|---|
| P1 | System info and boot checks | B1 | [Built](#p1-system-info-and-boot-checks), not yet run on a board |
| P2 | I2C bus scan | B7, B9 | [Built](#p2-i2c-bus-scan), not yet run on a board |
| T1 | Battery (`BAT_MONIT`) and cell detection | B6 | [Built](#t1-battery), not yet run on a board |
| T2 | Charge status (`USB_STAT`) and USB presence | B6 | [Built](#t2-charge-status), not yet run on a board |
| P4 | USB presence and USB Drive | B4, B6 | Not implemented yet |
| T3 | Keys: eight ladder keys and the power button | B5 | [Built](#t3-keys), not yet run on a board |
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

### P1 System info and boot checks

Each opening logs a result, so open it after a cold boot and again after a power-button wake; Record logs it again. It shows the profile and board tag, the firmware, the chip, ESP-IDF and the Arduino core, flash, PSRAM and heap, the reset reason, the wake cause with the ext1 mask, the abnormal resets this boot counted and the count in RTC memory now, any aborted sleep entry, eFuse `PIN_POWER_SELECTION` (information only; HW-01), and whether the card, the RTC, touch and the front light are there. The reset records are read with the SDK's non-consuming accessors, after `main.cpp` has used them.

It also watches the idle clock: with no input for 3 s the loop drops to 80 MHz (POWER.md §5). A USB data host or the radio holds the full clock by design, and the screen says "held".

PASS: profile `silkscreen`; 16 MB flash; at least 7 MB PSRAM (the 8 MB part less the allocator's share); no abnormal reset and no aborted sleep entry this boot; after a deep-sleep wake, cause `EXT1` with the power button's bit (IO18, `0x40000`) in the mask; and the idle clock not missing for 10 s while nothing holds it.

### P2 I2C bus scan

Confirm scans with J6 empty; Right (Scan+J6) scans with an accessory on J6. Each scan is logged. It probes 0x08-0x77 by address only, at 100 kHz and at 400 kHz, through `freeink::i2c::scanSensorBus()`. A bus the RTC or touch driver already runs is used as it is: only its clock changes for the 400 kHz pass, and it is put back. A bus no driver runs is started for the scan and ended after it. The SDA/SCL idle levels are read only while a driver owns the bus, and nothing here runs the stuck-bus recovery. Every device that answers is listed with what the profile puts at that address.

PASS: exactly one RTC answers (0x68 DS3231 or 0x51 RV-8263); the touch controller found at boot answers; no touch address answers when none was found at boot; both clocks find the same devices; both lines idle HIGH; and, with J6 empty, nothing else answers. An FT6336U in Monitor mode may not answer a scan started from a key: tap Scan instead, and the touch wakes it.

### T1 Battery

Measure P+ at J5 or across the cell, and enter it: Left/Right -/+10 mV, Up/Down +/-100 mV, starting from the firmware's reading. Confirm records. Repeat on battery, on USB with a cell, and on USB with no cell after a cold boot.

It shows P+ (16 samples of IO8, x2), the pin's average and one single sample, the percentage in 10 % steps with its deadband, whether a cell is fitted (P+ at least 2500 mV), the `USB_STAT` state and external power, the pin's range over the last 30 s, where P+ sits against the cutoff that applies (3400 mV, or 3300 mV with the radio on; not judged on external power or with no cell), whether a firmware write is allowed (3600 mV floor on battery), and the time on the screen. The no-cell case wants 2 min on USB with no cutoff sleep. "Charging" means P+ follows the charger's output, not the cell's charge.

It takes one reading a second, repaints at most every 5 s, and takes no reading within 0.9 s of a repaint, whose load would move P+. It never takes a cutoff reading of its own (`isBelowCutoff()` state belongs to the main loop), and it never sets a mode or a pull on IO8.

PASS: |P+ - meter| at most 150 mV (+/-50 mV ADC error at the pin, doubled by the divider, plus the 12-14 mV load offset and the 1 % divider tolerance); with a cell, the pin at 1500-2100 mV; off the charger, a 30 s spread at the pin of at most 20 mV. Without a meter value the result is INFO.

### T2 Charge status

Pick the bench state with Left/Right, set it up, then Confirm: it asks whether D2 is lit, then records. D2 shows USB power, not charging (HARDWARE.md §3).

| Stage | Set-up | Expect | USB connected | D2 |
|---|---|---|---|---|
| 1 | Battery only; USB_VBUS about 0 V on a meter (HW-08) | OnBattery, 1800-2600 mV | no | dark |
| 2 | USB charger, partly discharged cell | Charging, 800-1350 mV (about 1110) | yes | lit |
| 3 | Weak USB source (optional) | Charging (about 950 mV) | yes | lit |
| 4 | Nearly full cell on USB until the charger stops (optional, long) | Complete, 350-700 mV (about 550) | yes | lit |
| 5 | Cell pulled with USB in (optional; protected cell, insulated tools) | NoBattery: the 550/410 mV blink, never Complete | yes | lit |
| 6 | USB, no cell, after a cold boot | ExternalIdle, above 2600 mV | yes | lit |

The ladder is sampled by the SDK only, once a second (usbstat-slow-sampling); this screen reads its record and never touches IO9. It shows the last sample and its window, the published and decoded states, the debounce counters (candidate, the 8 Complete samples, the 30 readings outside every window after which the state is dropped), `isUsbConnected()` and its changes, the lost flag and the P+ cross-check (HW-08: 5 min of P+ at the cutoff while Charging or Complete publishes OnBattery), and the last four state changes with the time from the first sample in the new window to the published state.

PASS: the stage's state is published; the last sample sits in its window; `isUsbConnected()` and D2 are as the table says; a change into the state during the stage settled in time (3 s; Complete 10 s, since it needs 8 stable samples, about 8.5-9.5 s after the change itself; the no-battery blink 8 s); and in stage 5 Complete was never published. Switching the stage restarts its history.

### T3 Keys

The keys are this test's input. A front key held 1.5 s does what the button above it does: Back leaves, OK records, Left changes the mode, Right switches the radio (an idle station, for the "with Wi-Fi on" runs). Each press is matched to its switch by sampling the ladders the way `InputManager` does.

- **Keys.** For each switch (SW2 RIGHT, SW3 LEFT, SW8 OK, SW9 BACK, SW1 DOWN(1), SW4 UP(1), SW5 DOWN(2), SW7 UP(2)): its presses, its lowest and highest reading while pressed, and the smallest margin to its window's edge. For POWER (IO18): clicks, press and release edges, the last hold and the pin level. Both ladders now, with the lowest released reading. Presses that came through the loop's idle path: after 3 s with no input the loop sleeps in 50 ms slices at 80 MHz and wakes on a contact; the screen counts presses after such a pause, how many found the loop at 80 MHz, and the shortest. A USB data host keeps the loop at full speed, so do this part on battery.
  PASS: every switch and POWER pressed, all the same number of times (50 each for B5; count them), every margin at least 25 mV (BACK and UP(2) are known to be tight), every press matched to a switch, and as many POWER releases as presses. Record starts a new run.
- **Chords.** The six chords of §9.1 come up one by one: press the two keys together, hold about half a second, release. Each contact is judged by the level the ladder held longest. Expected: LEFT+BACK gives LEFT or no key (it can land under LEFT's window after ADC error); UP(1)+UP(2) gives UP or no key; RIGHT+OK gives RIGHT and DOWN(1)+UP(2) gives DOWN (the 100-ohm keys win); LEFT+OK and OK+BACK give no key. PASS when all six decode as expected; INFO while some are missing.
- **Hands off.** Leave every key alone for at least 30 s: any key event is a phantom. It also shows the released level's range per ladder and whether the raw reading tops out. PASS with no phantom; INFO under 30 s. Repeat with the radio on.

Holding POWER longer than 0.4 s sleeps the board (the reader's long press); a click does nothing else. POWER + DOWN takes a screenshot to `/screenshots/`, as in the reader.

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

- Derive from `testsuite::TestScreen` (`src/activities/testsuite/TestScreen.h`): draw the body in `drawBody()`, name the Confirm/Left/Right actions in `slotLabel()`, act in `onSlot()`, `onStep()` and `onTick()`, and record results with `record(Verdict, "key=%d ...", ...)`. One file per test in `src/activities/testsuite/tests/`; declare its factory in `tests/TestFactories.h` and set it in `TestCatalog.cpp`.
- `drawBody()` runs on the render task. Take readings, I2C traffic and `usbHostAttached()` in `onStart()`, `onTick()` or `onSlot()` (the loop task), and draw from what they stored.
- A live reading repaints at most every few seconds (T1 and T2 use 5 s): every repaint runs at the full clock, and a screen that repaints often keeps the board off its idle clock on battery. Never hold auto-sleep off for a live reading.
- Helpers: `ask()` puts a question to the operator and answers through `onAnswer()`; `msSinceInput()` gives the time since the last key or touch input; `TestPage::setCompact()` switches rows to the small font. A test whose input is the keys returns true from `keysAreTestInput()` and reads `HalGPIO` itself; its Confirm/Left/Right actions then run on a 1.5 s hold as well as on the touch buttons.
- UI text is English `constexpr` strings in the test code, not `tr()` keys. This is a deliberate exception to AGENTS.md rule 5: `gen_i18n.py` collects `STR_` keys from every source file whatever its `#if`, so test-only keys would ship in every reader image. Shared screens (popups, Wi-Fi, keyboard) keep `tr()`, with the language pinned to English.
- The env builds the SDK with `-DFREEINK_DIAGNOSTICS=1`, so the bring-up records in the freeink-sdk README ("Bring-up diagnostics") are available.
- The main loop and the render task re-apply `SETTINGS.fadingFix` and `SETTINGS.screenInverted` on every pass: a display test changes those fields in RAM, never the renderer or the HAL directly.
- A front-light test calls `confirmLightLoad()` and lights the boost only in `onLightLoadConfirmed()`.
- Scratch files go in `/.silkscreen-test/` and end in `.tmp`, never `.txt` (a book type).
