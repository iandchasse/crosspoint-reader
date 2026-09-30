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

**Display runs.** T4, T5, T6 and T8 show full-screen frames. Confirm starts a run. Each refresh is one step, drawn and refreshed a loop pass after the one before, so Back stops a run between two refreshes. A frame that says "Any key or tap: continue" stays on the glass until you press a key or tap; Back stops there too. A tap in the top-left corner, where the header's back button sits, counts as Back. The touch buttons are not on the glass during a run and do nothing. The run ends with the test's screen repainted by a HALF refresh, which clears the last frame, and then a question about what you saw. A stopped run records nothing.

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
| T4 | Refresh modes | B2 | [Built](#t4-refresh-modes), not yet run on a board |
| T5 | Grayscale | B2 | [Built](#t5-grayscale), not yet run on a board |
| T6 | Inversion | B2 | [Built](#t6-inversion), not yet run on a board |
| T7 | Orientation and bezel ruler | B2 | [Built](#t7-orientation-and-bezel-ruler), not yet run on a board |
| T8 | 40 MHz panel SPI trial | B2 | [Built](#t8-panel-spi-clock), not yet run on a board |
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

### Display tests (T4-T8)

The display tests run the reader's own calls (`GfxRenderer`, `HalDisplay`) and read what the SDK driver did from its bring-up record (`EpdBus::diagnostics()`, freeink-sdk README "Bring-up diagnostics"). For every refresh they show and log:

- the mode the driver actually ran. The SDK promotes a FAST to HALF on the first paint after boot or a wake, after a gray pass left without its cleanup, and on the first refresh after a polarity change, so a FAST request is not proof that a FAST ran;
- the update-sequence byte (`0x22`). The Silkscreen profile uses the X4 defaults: FULL `0xF7`, HALF `0xD7` (after the `0x5A` temperature write), FAST `0xFC`, and `0xCC` for the gray waveform (`0xCF` with the Fading Fix power-down);
- the BUSY time, from the activation to the end of the completion wait;
- the host time, which is the rest of the call: SPI transfers, the RAM resync after the refresh, and the power-down.

A refresh is in the **BUSY window** when the driver reported it, it did not time out, and it took at least 100 ms and less than 30 s. The floor is 100 ms, not the 20 ms of the first proposal, because the completion wait gives up after 20 ms when BUSY never rises. That happens with no panel (the BUSY pull-down reads idle) or with BUSY stuck LOW, so a 20 ms floor would pass a missing panel. The quickest real waveform, FAST, takes about 500 ms. 30 s is the SDK's timeout, reached when BUSY is stuck HIGH.

The tests change only what the reader's settings change: `SETTINGS.fadingFix` (Sunlight Fading Fix, which the loop hands to the renderer) and `SETTINGS.screenInverted` (night mode, which the render task hands to the display before each render). T8 also changes the panel's SPI clock. All three live in RAM only. They go back to the pinned values at the end of every run and when the screen closes, and a sleep and wake restores the clock anyway. When a test frame is still on the glass as the screen closes, the next screen's first refresh is a HALF. No display test turns the radio on (PWR-02: no Wi-Fi TX during a refresh).

### T4 Refresh modes

Left/Right (or Up/Down) pick a refresh; Confirm runs it. A run alternates two frames. Each frame is cut into six bands: a 20 px checkerboard, a 1 px checkerboard, 1 px lines, UI text, solid black and white, and dithered grays. Frame B puts a different pattern in every band, so a ghost of the other frame shows up as a foreign texture.

| Refresh | Run | Expect |
|---|---|---|
| FULL, HALF, FAST | 6 refreshes | FULL: no ghost. HALF: a faint ghost at most. FAST: ghosting builds up, every frame readable |
| FAST, async | 6 refreshes through `displayBufferAsync()`, each waited out and its baseline rebuilt as the reader's overlay chrome does | as FAST |
| FULL, HALF, FAST with turn-off | 6 refreshes with the Sunlight Fading Fix on | the same as without it |
| Cadence | the reader's page turns at its refresh frequency N (the reader's setting: 5, 10, 15 or 30; 1 runs as 2): N-1 FAST, then a HALF | ghosting builds, then the HALF clears it; the frame before the HALF stays up for a look |

The screen shows the last run per mode (count, sequence byte, BUSY min/mean/max, host time, promotions, refreshes outside the window, and for the variants the async returns and turn-off requests), the mean BUSY per mode over this boot's runs against the X4-class references, and whether FULL >= HALF > FAST holds.

PASS:

- every refresh is in the BUSY window, and none was promoted (the test starts from a clean panel state);
- async: every call returned with BUSY still asserted, sooner than its BUSY time;
- turn-off: the driver saw the power-down request on every refresh. `0xF7` and `0xD7` always power down; after a `0xFC` the driver runs the separate power-off;
- once FULL, HALF and FAST have all been run this boot, their means keep FULL >= HALF > FAST;
- the frames looked as the refresh expects.

A mean more than 50 % from the X4-class reference (FULL about 1800 ms, HALF about 1720 ms, FAST about 500 ms) is flagged in the log (`x4_ref=outside:...`) and on the screen, but does not fail the test. Record the Silkscreen figures as its baseline.

### T5 Grayscale

Left/Right pick one of the reader's gray-pass routes; Confirm runs it. The chart has a four-level ramp (white, light gray, dark gray, black), narrow columns cycling through the four levels, and two lines in the reader's anti-aliased font. The same lines are drawn below in B/W only, for comparison. After the pass, a plain page gets a FAST refresh. Each stays up for a look.

| Route | The reader's calls |
|---|---|
| Text page, planes buffered | FAST base through `displayBufferAsync()`; both planes rendered in 80-row bands into two 48,000 B buffers while the base refreshes; then `waitRefreshComplete()`, both planes streamed, `displayGrayBuffer()`, `cleanupGrayscaleWithFrameBuffer()` (`EpubReaderActivity` overlapped path) |
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
- Record logs the four as INFO, with the profile's values, for the SDK profile (`viewableInsets` is still the X4 default, `{9, 3, 3, 3}`);
- Back returns to the orientations.

The renderer goes back to portrait when the screen closes.

### T8 Panel SPI clock

The SSD1677 is specified to 20 MHz for writes, which is the profile's clock. This test tries a faster one. Left/Right pick the profile clock, 26.7 MHz or 40 MHz. Confirm runs 20 B/W refreshes at that clock: four FAST, then a HALF, four times, with the T4 frames. It then runs the T5 chart through the strip route and a plain page. The last B/W frame, the chart and the plain page each stay up for a look.

The clock is set in RAM only, through the SDK's `FreeInkDisplay::setSpiClockTrial()` (a `FREEINK_DIAGNOSTICS` control). It goes back to the profile's at the end of the run, when the screen closes, and at every wake, when the display restarts. The panel has no read-back on this board, so the frames are compared by eye with a run at the profile clock: run that first. The host time per refresh shows the clock at work. At 20 MHz, a blocking FAST moves 144,000 B (the frame, then both RAM planes resynced), about 58 ms of transfer, and at 40 MHz about half that. The screen keeps each clock's mean BUSY and host time for FAST and HALF over this boot, with its verdict.

PASS: every refresh is in the BUSY window, none was promoted, the gray waveform ran and is in the window, the plain page ran as FAST, and every frame, the chart and the plain page looked as they do at the profile clock. Raise `displaySpiHz` in the profile only after clean runs at the higher clock on more than one board (REQUIREMENTS: the EPD SPI clock is an open question).

## Safety

- **Front light.** The TPS923610 boost must never run without a load. A front-light test asks, every time, that the J3 panel light or a J6 strip is connected, and lights nothing until the operator confirms. Nothing else in the test firmware can light it.
- **Pins.** Tests never call `pinMode` or `gpio_reset_pin` on IO1, IO2, IO4, IO8, IO9, IO10, IO18, IO40, IO42 or the SD pins (POWER.md ADC-01, REQUIREMENTS PWR-19), and every ADC read goes through `freeink::adc`.
- **Battery.** A screen that holds auto-sleep off also holds the CPU at full clock. Live readings on battery run for a bounded time or only on USB. The 15-minute ceiling, the 60-minute input cap and the low-battery cutoff are the reader's and still apply on battery.
- **Sleep and wake.** No new sleep or wake source; `scripts/check_sleep_wake_sources.sh` is unchanged and covers the test code. Timing across a sleep uses a power-button wake and a stopwatch.
- **Card.** Besides `/.silkscreen-test/`, the only writes are the reader's own: `/crash_report.txt` after a panic, `/screenshots/` (POWER + Down), and the host during USB Drive.
- **Display.** The display tests change only Sunlight Fading Fix, night mode (both through `SETTINGS`) and the panel SPI clock, all in RAM, and put them back at the end of every run and on exit. They never turn the radio on during a refresh (PWR-02).

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
- A display test derives from `PanelRunTest` (`tests/PanelRun.h`). `startRun(steps)` starts a run. `runStep()` draws one frame and refreshes it on the render task. `afterStep()` stores the measurement, and calls `pauseRun()` to hold the frame. `onRunEnd()` asks and records after the closing HALF repaint. `restorePanel()` puts back anything the run changed on the panel. `refreshTimed()`, `refreshAsyncStart()`/`refreshAsyncFinish()` and `grayTimed()` return a `RefreshSample` with what the driver ran. `tests/PanelPatterns.h` has the frames, and `tests/GrayPass.h` has the reader's gray-pass routes.
- `TestScreen` also offers `onBack()` (use Back for a sub-mode, or to stop a run), `edgeBands()` (edge bands the test draws itself, such as T7's rulers) and `requestCleanRepaint()` (the next repaint is a HALF).
- A test that needs an SDK object behind the HAL uses `TestSuiteAccess` (`src/activities/testsuite/TestSuiteAccess.h`). The HAL class declares it a friend under `#if CROSSPOINT_TEST_SUITE`, so the reader gains no API. `HalDisplay` does so today.
- A front-light test calls `confirmLightLoad()` and lights the boost only in `onLightLoadConfirmed()`.
- Scratch files go in `/.silkscreen-test/` and end in `.tmp`, never `.txt` (a book type).
