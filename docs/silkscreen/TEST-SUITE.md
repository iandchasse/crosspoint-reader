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
- `bash scripts/check_test_firmware.sh` (after a build) checks the image, as CI does: no reader, library, book-parsing, web-server, OTA, SD-update or light-panel function is linked; no static initialiser from reader code runs except four allowed ones from `lib/Epub` and `lib/LibraryIndex` (below); and the board tag `CROSSPOINT-BOARD-V1:silkscreen-test;` appears exactly once. It stops with an error when `nm` fails or its output lacks a symbol every image has.

## How the test firmware differs from the reader

| Area | Test firmware |
|---|---|
| Boot | `setup()` runs unchanged up to the routing: release wake, reset loop, early wake decision, touch probe, boot cutoff, SD, settings. Then the splash, then the test menu, on every boot. When the menu first opens, the reader's files are compared with the P8 baseline (the first boot with a card takes it). After a panic, the crash screen first, then the menu. |
| What is built | The reader, library, home screen, file browser, image viewer, OPDS browser, the reader's sleep screen and the web server (with WebDAV) are left out of the build (`build_src_filter`), so a live reference to any of them fails the link. Settings, OTA and the SD firmware update are compiled but unreachable, and the linker drops them. `lib/Epub` and `lib/LibraryIndex` are still linked, because headers the test build shares include them: none of their functions stays, but four static initialisers (the hyphenation registry, `ImageBlock`, `ImageDecoderFactory`, `library::libraryIndexPath`) run at boot, and the hyphenation tables they reference (about 351 KB of flash) stay in the image. Every reader entry point (`goHome`, `goToReader`, `goToSettings` and the rest) lands on the menu. |
| Settings | `/.crosspoint/settings.json` is read once, then these are pinned in RAM: night mode off, Sunlight Fading Fix off, portrait, Short Power Button = Ignore, light off with no restore on wake, the keys in their default roles, English, the Lyra theme, no SD reader font, and a 10-minute sleep timeout if it was Never. |
| Card writes | Never under `/.crosspoint/`: every settings, state, Wi-Fi, recent-books, OPDS and KOReader save is a no-op, `state.json` is not read, and the Quick Resume frame is neither written nor removed. The test firmware writes only `/.silkscreen-test/`: `results.csv`, the P8 baseline `crosspoint.sha`, and scratch files ending in `.tmp`. |
| No card | The menu opens with "SD not mounted" and results go to the serial log only. The reader's SD-card error screen and its 60 s timeout are not in the test firmware. |
| Front light | Off at every boot and after every silent restart, with no light panel (gesture, status-bar tap). Only a front-light test lights it, after the operator names the load. The over-voltage trip handling and the SDK's switch-on load probe are the reader's, but the "No front light detected" message is not shown: T9 and T10 report the probe's result themselves. T10 lowers the trip level in RAM for one run. |
| USB | While a USB data host is attached and the `USB_STAT` ladder reports USB power, auto-sleep is held off (the USB Serial/JTAG host frames, with a 2 s hold). A charger alone or a cell never holds it, and neither does a data link with no VBUS on the cell: the S3's USB Serial/JTAG enumerates without VBUS (HW-11). The 15-minute ceiling and the 60-minute input cap still apply whenever the `USB_STAT` ladder does not report external power, so a misread ladder shows up as a sleep during a bench session. |
| Sleep | The same sleep entry, wake source and timeouts as the reader. The sleep screen shows the firmware and board tag, the MEAS-01 current targets and the pins to probe. Once its refresh has returned it times the RTC and keeps a record of the entry in RTC memory, for T14 and P5. A power-button click does nothing and a hold longer than 400 ms sleeps; waking needs POWER held. |

## Using it

**The menu.** Up/Down or Left/Right move, Confirm opens. With touch, tap a row, and swipe to scroll. Back does nothing here. The line under the title says where results go (`/.silkscreen-test/results.csv`, or "SD not mounted") and shows the firmware version. Each row shows the test id and name, what it checks, and the last result this boot. With touch, a swipe up from the bottom edge returns to the menu from any test, except while T11's targets or gestures are on.

**A test screen** has the test id and name in the header, the last result recorded on this screen, the test's instructions and live readings, and four touch buttons along the bottom. The buttons sit in the order of the front keys and do what the keys do: Exit (Back), then the test's Confirm, Left and Right actions. Up/Down step values where a test has any. Back, the header's back button or Exit leaves the test. In a test that reads the keys itself (T3), a front key held 1.5 s does what the button above it does (Back: Exit), and the screen says so. In a test whose input is the touch panel (T11's targets and gestures), the touch buttons do nothing and the header has no back button: the keys drive the screen, and Back stops. Leaving any test switches the front light and the radio off.

**Questions.** When a result needs something only the operator can see (T2 and P4 ask whether D2 is lit), a list of answers opens: Up/Down and Confirm, or tap an answer. Back, or a tap outside the list, records nothing.

**Display runs.** T4, T5, T6 and T8 show full-screen frames. Confirm starts a run. Each refresh is one step, drawn and refreshed a loop pass after the one before, so Back stops a run between two refreshes. A frame that says "Any key or tap: continue" stays on the glass until you press a key or tap; Back stops there too. A tap in the top-left corner, where the header's back button sits, counts as Back. The touch buttons are not on the glass during a run and do nothing. The run ends with the test's screen repainted by a HALF refresh, which clears the last frame, and then a question about what you saw. A stopped run records nothing.

**Results.** Each result is shown on the screen, logged to serial and appended to `/.silkscreen-test/results.csv`:

- Serial: `[INF] [TEST] TEST,<id>,<verdict>,<details>`. A log line holds 256 bytes, so details past 200 bytes go on following `[INF] [TEST] TEST+,<id>,<more>` lines, in order.
- CSV columns: `timestamp,time_source,session,board,firmware,test,verdict,details`. The timestamp is the RTC's local time when the RTC holds a valid time (`time_source` = `rtc`), otherwise seconds since boot (`uptime`). `session` is random per boot, `board` is the end of the Wi-Fi MAC, and `details` is quoted, with `key=value` pairs by convention.
- Verdicts: `PASS`, `FAIL`, `SKIP` and `INFO` (a reading with no pass criterion).
- A record that judges checks carries `fails=` with the failed ones joined by `+` (or `none`). On a FAIL the screen shows them on a line of their own ("Failed: ...") under the verdict, then the rest of the details, cut to fit; the serial log and `results.csv` carry the whole record (up to 319 bytes).
- Tests keep scratch files in `/.silkscreen-test/` with names ending `.tmp`. Any left behind by a sleep or a crash are removed at the next boot.
- P8 also records a row at every boot with a card (`when=boot`).

## Tests

The ids are stable; `results.csv` and the serial log use them. Every test in the menu is built; none has run on a board yet. Each section gives the purpose and the bring-up step, the steps, what the screen shows and the PASS criteria. The thresholds marked "proposal" are to tune after the first board.

| Id | Test | Bring-up step | Status |
|---|---|---|---|
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
| P5 | Sleep, wake and sleep current | B3, B10 | [Built](#p5-sleep-wake-and-sleep-current), not yet run on a board |
| P8 | Reader-data integrity | - (guards the reader's card) | [Built](#p8-reader-data-integrity), not yet run on a board |

### P1 System info and boot checks

**Purpose:** the module, the firmware and the boot records are what this board should have, and the wake and the idle clock behave. **Bring-up:** B1 (and the wake of B3).

Each opening logs the panel controller (`action=panel`), then the boot checks (`action=boot`), so open it after a cold boot and again after a power-button wake; Record logs both again. It shows the profile and board tag, the firmware, the chip, the panel controller, ESP-IDF and the Arduino core, flash, PSRAM and heap, the reset reason, the wake cause with the ext1 mask, the abnormal resets this boot counted and the count in RTC memory now, any aborted sleep entry, eFuse `PIN_POWER_SELECTION` (information only; HW-01), and whether the card, the RTC, touch and the front light are there. The reset records are read with the SDK's non-consuming accessors, after `main.cpp` has used them.

It also watches the idle clock: with no input for 3 s the loop drops to 80 MHz (POWER.md §5). A USB data host or the radio holds the full clock by design, and the screen says "held". The idle clock logs its own row (`action=idle_clock`), once per opening: PASS when the loop reached 80 MHz, with the time it took; FAIL when it had not after 10 s without input while nothing held it. Held logs nothing. The boot row carries the idle clock as it stood, without judging it.

**Panel controller.** The 4.26" panel is a Good Display GDEQ0426T82 (SSD1677) or the custom panel (UC8279D). The SDK probes it at every cold boot with a half-duplex read on SDI (IO12) and keeps a conclusive verdict in RTC memory for a deep-sleep wake (never in NVS). An UltraChip part answers the read; an SSD1677 is recognized only because nothing answers. P1 shows:

- the controller and how it was chosen: probed this boot, kept from the probe before the sleep, or forced by the build (`-DSILKSCREEN_DISPLAY_CONTROLLER`, no probe);
- the probe's verdict: UltraChip answered; no UltraChip answer (SSD1677); or inconclusive, which runs as SSD1677;
- the VER bytes and the FLG status byte, and on a UC8279 its LUT_VER (VER byte 2) and the driver variant (the same byte: 0x02/0x03 select the QY gray tables, 0x67 no gray, anything else the default tables);
- the first MTP bytes when the probe read them.

The `action=panel` row logs all of it (`controller`, `source`, `verdict`, `ver`, `flg`, `lut_ver`, `variant`, and the 48 MTP bytes as `mtp`). PASS: a probe resolved the controller, this boot or before the sleep. INFO: the build forced it. FAIL: `probe_inconclusive`, or `not_resolved`. After a wake the row must say `source=cached` with the cold boot's IDs.

**Boot checks.** The SDK's boot-time checks (rules in `ProbeChecks.h`), each shown on its own row and carried in the boot row:

- the key ladders (`ladders=IO1:fitted,IO4:fitted@probed`): at a reset other than a deep-sleep wake each ladder pin was read with the internal pull-down, then the pull-up, and a ladder with no pull-up fitted (it follows both from rail to rail) is off for the session (`off`); the screen shows both readings in mV (a fitted ladder reads above about 2.2 V pulled down). After a deep-sleep wake the result is the one kept from before the sleep (`@cached`);
- the power button's idle level (`pwr_idle=low:3mV`): IO18 read on the ADC at a reset other than a deep-sleep wake. `high` means held at boot; `undefined` means it idles between the logic levels (R36 and R72 both fitted, about 1.76 V), so the pin is off and every sleep has no wake source: press SW11 (reset) to start the board;
- the touch chip id (`touch_id=ok:64`, `rejected:1c` for a device at 0x38 that is not a FocalTech controller, `none` with nothing there) and the RTC pattern (`rtc_id=ok`, `rejected@0x68` for a device there without the DS3231 register pattern). Both devices refused are left alone; P2, T11 and T13 judge them.

PASS (`action=boot`): profile `silkscreen`; 16 MB flash; at least 7 MB PSRAM (the 8 MB part less the allocator's share); no abnormal reset and no aborted sleep entry this boot; after a deep-sleep wake, cause `EXT1` with the power button's bit (IO18, `0x40000`) in the mask; the panel controller resolved (the row carries it as `panel=<controller>:<source>:<VER>:<FLG>` and fails `panel` with the panel row); and the power button not idling between the logic levels (`power_idle`). A ladder found off is no failure here (a build may leave a ladder out); T3 shows it. The touch address shown is the one the driver uses (a GT911 fallback, in an alternate-jumper build, may answer at its alternate, 0x14).

### P2 I2C bus scan

**Purpose:** the sensor bus is healthy and the expected devices answer on it. **Bring-up:** B7 (RTC), B9 (touch).

Confirm scans with J6 empty; Right (Scan+J6) scans with an accessory on J6. Each scan is logged. It probes 0x08-0x77 by address only, at 100 kHz and at 400 kHz, through `freeink::i2c::scanSensorBus()`. A bus the RTC or touch driver already runs is used as it is: only its clock changes for the 400 kHz pass, and it is put back. A bus no driver runs is started for the scan and ended after it. The SDA/SCL idle levels are read only while a driver owns the bus, and nothing here runs the stuck-bus recovery. Every device that answers is listed with what the profile puts at that address.

PASS: at most one RTC answers (0x68 DS3231 or 0x51 RV-8263), and the one `HalClock` began at boot still answers (`rtc_lost` otherwise). With none answering and none begun at boot, P2 asks whether an RTC is fitted, as T13 does: "None fitted" is no failure (the RTC is optional, rtc-devices), a fitted one fails `no_rtc`, and Back records nothing. The touch controller found at boot answers at the address its driver uses (a GT911 on the fallback, alternate-jumper builds only, may sit at 0x14); no touch address answers when none was found at boot; both clocks find the same devices; both lines idle HIGH; and, with J6 empty, nothing else answers. An FT6336U in Monitor mode may not answer a scan started from a key: tap Scan instead, and the touch wakes it.

A device the boot's identity checks refused is listed as "not FocalTech, left alone" (0x38) or "not a DS3231, left alone" (0x68), and counts neither as the touch controller nor as an RTC. With J6 empty only the touch tail or U13 can sit there, so a refusal fails `touch_id_rejected` or `rtc_id_rejected`; with an accessory on J6 (Scan+J6) it is the expected result. The record carries both checks (`touch_id`, `rtc_id`, as in P1).

### T1 Battery

**Purpose:** the battery reading that the cutoff, the firmware-write floor and the percentage use. **Bring-up:** B6.

Measure P+ at J5 or across the cell, and enter it: Left/Right -/+10 mV, Up/Down +/-100 mV, starting from the firmware's reading. Confirm records. Repeat on battery, on USB with a cell, and on USB with no cell after a cold boot.

It shows P+ (16 samples of IO8, x2), the pin's average and one single sample, the percentage in 10 % steps with its deadband, whether a cell is fitted (P+ at least 2500 mV), the `USB_STAT` state and external power, the pin's range over the last 30 s, where P+ sits against the cutoff that applies (3400 mV, or 3300 mV with the radio on; not judged on external power or with no cell), whether a firmware write is allowed (3600 mV floor on battery), and the time on the screen. The no-cell case wants 2 min on USB with no cutoff sleep. "Charging" means P+ follows the charger's output, not the cell's charge.

It takes one reading a second, repaints at most every 5 s, and takes no reading within 0.9 s of a repaint, whose load would move P+. It never takes a cutoff reading of its own (`isBelowCutoff()` state belongs to the main loop), and it never sets a mode or a pull on IO8.

PASS: |P+ - meter| at most 150 mV (+/-50 mV ADC error at the pin, doubled by the divider, plus the 12-14 mV load offset and the 1 % divider tolerance); with a cell, the pin at 1500-2100 mV give or take the ADC's 50 mV (1450-2150 mV; not judged while `USB_STAT` reports Charging or Complete, when P+ is the charger's output); off external power, a 30 s spread at the pin of at most 20 mV. Without a meter value the result is INFO.

### T2 Charge status

**Purpose:** the `USB_STAT` ladder decodes every charger state, and USB presence follows it. **Bring-up:** B6.

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

PASS: the stage's state is published; the last sample sits in its window; `isUsbConnected()` and D2 are as the table says; a change into the state during the stage settled in time (3 s; Complete 10 s, since it needs 8 stable samples, about 8.5-9.5 s after the change itself; the no-battery blink 8 s); and in stage 5 Complete was not published during the stage (a Complete still published from stage 4 when stage 5 was picked does not count; one still published at Record fails the state). Switching the stage restarts the stage's own record (the states seen, the settle); the last four changes stay listed.

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
  PASS: every switch and POWER pressed, all the same number of times (50 each for B5; count them), every margin at least 25 mV (BACK and UP(2) are known to be tight), every press matched to a switch, as many POWER releases as presses, and the power button not idling between the logic levels at boot (`power_idle`; it then reads no presses at all). The record carries `ladders=` and `pwr_idle=` as in P1. Record starts a new run.
- **Chords.** The six chords of §9.1 come up one by one: press the two keys together, hold about half a second, release. Each contact is judged by the level the ladder held longest. Expected: LEFT+BACK gives LEFT or no key (it can land under LEFT's window after ADC error); UP(1)+UP(2) gives UP or no key; RIGHT+OK gives RIGHT and DOWN(1)+UP(2) gives DOWN (the 100-ohm keys win); LEFT+OK and OK+BACK give no key. A chord on a ladder that is off is skipped ("ladder off") and not expected. PASS when every expected chord decodes as expected; INFO while some are missing.
- **Hands off.** Leave every key alone for at least 30 s: any key event is a phantom. A ladder with no pull-up is off, so it cannot report phantoms; the record carries `ladders=`. It also shows the released level's range per ladder and whether the raw reading tops out. PASS with no phantom; at most one idle reading that is in no key's window and not released, that is, below the 3000 mV released floor (btn-idle-topcode; one is a key's release edge, more fails `unclean_idle`); and, over a full watch, every ladder read released at least once (`never_released`). INFO under 30 s with nothing failed. The record carries each ladder's released range and whether it topped out. Repeat with the radio on.

Holding POWER longer than 0.4 s sleeps the board (the reader's long press); a click does nothing else. POWER + DOWN takes a screenshot to `/screenshots/`, as in the reader.

### Display tests (T4-T8)

The display tests run the reader's own calls (`GfxRenderer`, `HalDisplay`) and read what the SDK driver did from its bring-up record (`EpdBus::diagnostics()`, freeink-sdk README "Bring-up diagnostics"). For every refresh they show and log:

- the mode the driver actually ran. The SDK promotes a FAST to HALF on the first paint after boot or a wake, after a gray pass left without its cleanup, and on the first refresh after a polarity change, so a FAST request is not proof that a FAST ran;
- the update-sequence byte (`0x22`). The Silkscreen profile uses the X4 defaults: FULL `0xF7`, HALF `0xD7` (after the `0x5A` temperature write), FAST `0xFC`, and `0xCC` for the gray waveform (`0xCF` with the Fading Fix power-down). A UC8279 panel reports its `TSSET` byte instead: `0x1E` (GC) for FULL and HALF, `0x5A` (DU) for FAST and for the non-flashing base of a gray page, and its CDI (`0x97`) for the gray waveform;
- the BUSY time, from the activation to the end of the completion wait;
- the host time, which is the rest of the call: SPI transfers, the RAM resync after the refresh, and the power-down.

A refresh is in the **BUSY window** when the driver reported it, it did not time out, and it took at least 100 ms and less than 30 s. The floor is 100 ms, not the 20 ms of the first proposal, because the completion wait gives up after 20 ms when BUSY never rises. That happens with no panel (the BUSY pull reads idle) or with BUSY stuck LOW, so a 20 ms floor would pass a missing panel. It also catches a UC8279 panel driven as an SSD1677 after a wrong verdict: its waits either end at once or run to the 30 s timeout. The quickest real waveform, FAST, takes about 500 ms. 30 s is the SDK's timeout, reached when BUSY is stuck HIGH.

The tests change only what the reader's settings change: `SETTINGS.fadingFix` (Sunlight Fading Fix, which the loop hands to the renderer) and `SETTINGS.screenInverted` (night mode, which the render task hands to the display before each render). T8 also changes the panel's SPI clock. All three live in RAM only. They go back to the pinned values at the end of every run and when the screen closes, and a sleep and wake restores the clock anyway. When a test frame is still on the glass as the screen closes, the next screen's first refresh is a HALF. No display test turns the radio on (PWR-02: no Wi-Fi TX during a refresh).

### T4 Refresh modes

**Purpose:** the three B/W waveforms run as asked, in time and cleanly. **Bring-up:** B2.

Left/Right (or Up/Down) pick a refresh; Confirm runs it. A run alternates two frames. Each frame is cut into six bands: a 20 px checkerboard, a 1 px checkerboard, 1 px lines, UI text, solid black and white, and dithered grays. Frame B puts a different pattern in every band, so a ghost of the other frame shows up as a foreign texture.

| Refresh | Run | Expect |
|---|---|---|
| FULL, HALF, FAST | 6 refreshes | FULL: no ghost. HALF: a faint ghost at most. FAST: ghosting builds up, every frame readable |
| FAST, async | 6 refreshes through `displayBufferAsync()`, each waited out and its baseline rebuilt as the reader's overlay chrome does | as FAST |
| FULL, HALF, FAST with turn-off | 6 refreshes with the Sunlight Fading Fix on | the same as without it |
| Cadence | the reader's page turns at its refresh frequency N (the reader's setting: 5, 10, 15 or 30; 1 runs as 2): N-1 FAST, then a HALF | ghosting builds, then the HALF clears it; the frame before the HALF stays up for a look |

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

Both light the boost only after a question names the load, before each lighting session: T9's Confirm, and each T10 run. The answers are "No load: cancel" (preselected), "J3 panel light" or "J6 LED strip". Within a session the light can go dark and relight on the same load without a new question: a T10 run relights twice by itself (the retry at the lowered level, the restore at the profile's), and T9's string switch takes the driver dark for at least 3 ms and pulses its enable again while lit. Nothing else in the test firmware lights it. They read `LED_MONIT` (IO2) through `freeink::adc`, and the SDK monitor's record (`FrontlightManager::monitorDiagnostics()`, freeink-sdk README "Bring-up diagnostics"): the trips since boot with the last trip's `LED_SW` and limit, and the monitor's last reading. The main loop's trip handler switches a tripped light off, which clears `fault()`, before any screen runs. So a trip shows as the light dropping out and the record's count rising, never as `fault()`. `LED_SW` is the pin reading × 9.333.

Every switch-on and every string change while lit also runs the SDK's load probe (`FrontlightConfig::loadProbeMs`): `LED_MONIT` read every 1 ms for 60 ms after the enable pulse. A reading above the trip there is a "no load" result for that string, not an in-use trip: the SDK then tries the other string in the same call, and either records the first one missing (a single-colour light, which then stays on the other string until a cold boot) or reports load absent and leaves the light off. The tests read that record through `FrontlightManager::probeDiagnostics()`: the load (unknown, present, absent), each string (unknown, present, missing), and the probes, their trips and the last probe's peak reading. Probe trips are counted apart from the monitor's.

The pads are read from the GPIO registers (`gpio_get_io_config()` and the output register), never set: IO42 (ADIM) on its LEDC carrier while lit, and held LOW by the GPIO output register while dark; IO40 (COLOR_SEL) at the lit string (HIGH = warm; the string the SDK picked, which differs from the one asked for when a string is missing), and LOW (cool) while dark, except after a trip on the warm string, until `LED_SW` is at 15 V (at most 4 s).

### T9 Front light

**Purpose:** both strings light at every brightness step, `LED_MONIT` reads the LED rail, and the switch-on probe finds the load. **Bring-up:** B8.

Confirm lights the boost after the load question; Confirm again switches it off. The switch-on probe runs inside that Confirm and inside every string switch while lit: when it finds no load on any string, the light stays off and T9 records FAIL `no_light` at once. Up/Down step the brightness through 10, 25, 50, 75 and 100 %. That is the SDK's perceptual curve, so about 2, 9, 32, 62 and 100 % duty, or roughly 0.3, 1.3, 4.2, 8.3 and 13.3 mA of LED current (duty × 13.3 mA). Left switches between the cool and warm strings; while lit, the driver goes dark for at least 3 ms and gets its enable pulse again. Take both strings through every step, then Record: it switches a lit light off first (the question stops the screen's 22 V watch while it is open), takes the after-off reading, then asks what the light did. Record before anything was lit logs INFO with the dark reading.

It shows:

- the dark reading, taken 0.3 s after the screen opens: `LED_SW` one diode below `LDO_IN`, about 0.25-0.55 V at the pin (fl-offstate-reading). Below 100 mV the monitor cannot see `LED_SW` and its trip is blind, so the test does not light and records FAIL;
- lit, a reading once a second after each change has settled for 0.3 s (fl-monit-settle), kept per string and step in a table of pin mV, and the SDK monitor's own last reading from its 100 ms tick, with its read and skipped counts;
- the trips on this screen and since boot, and the trip level;
- IO42 and IO40 as above, and any pull on IO42, IO40 or IO2 (fl-adim-no-pullup, fl-color-drive, fl-monit-pin);
- the enable pulse, the dark dwell and the carrier from the profile, for the optional scope check: IO42 LOW for at least 3 ms, HIGH for about 150 µs, then the 25 kHz carrier; IO40 changes only while IO42 is LOW, at least 3 ms after it fell;
- what the switch-on probe found: the load, each string, the last probe's string, result, peak reading and reading count, and its trips on this screen and since boot. The table files each reading under the string the SDK lit.

The record adds `probe=`, `cool=` and `warm=` (the probe's findings), `probe_trips=` and `probe_peak=`.

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

**Purpose:** the software over-voltage trip switches the boost off and keeps it off, and the switch-on probe takes a reading above the trip for "no load". **Bring-up:** B8.

This replaces the rebuild with a lower `SILKSCREEN_FRONTLIGHT_TRIP_MV` in PORTING.md B8. Confirm, after the load question, runs about 5 s. Back stops it, and a stopped run records nothing.

| Step | What happens | Expect |
|---|---|---|
| 1 | Cool string at 50 % with the profile's trip level (23 V), for 0.6 s | the switch-on probe finds the load; no trip; the monitor's lit reading is kept |
| 2 | The trip level is lowered in RAM to the lit reading less 2 V or a fifth, whichever is more (12 V for a 15 V string) | an in-use trip within 300 ms, not taken for "no load"; IO42 held LOW by the GPIO register; `LED_MONIT` back below 700 mV |
| 3 | 2 s | dark: no relight, no second trip, IO42 held LOW |
| 4 | Switched on again, level still lowered | the switch-on probe reads above the level at once: the load is taken as absent (the other string is tried too, and trips), the light stays off, IO42 LOW, and the monitor counts no trip |
| 5 | The profile's level back, switched on, for 1 s | the probe finds the load again; lit, no trip |

It then asks whether the light did that. The trip time runs from the lowering to the trip in the SDK's record; the probe's time runs from the carrier start to the reading above the level. A failed step ends the run with FAIL. The record carries `probe2=` (time and reading), `probe2_strings=` (strings tried), `probe2_load=`, `restored_load=` and `probe_trips=`. On a one-colour J6 strip, step 4's second try enables the empty string once: an open-load enable, which is safe (HARDWARE.md §13.1).

The level only ever goes down, never above the profile's. It goes back at the end of every run, on Back, when the screen closes (a sleep included) and whenever the menu opens; a restart restores it anyway.

PASS: every step as in the table, the level back at the profile's, and the operator's yes.

### T11 Touch probe, mapping and gestures

**Purpose:** the touch probe, the controller's orientation and raw ranges, and the gestures. **Bring-up:** B9.

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

**Purpose:** the controller takes its sleep command, so it draws 55 µA and not 220 µA through deep sleep. **Bring-up:** B9, B10.

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

**Purpose:** the card, the 4-bit bus at 20 MHz, and a transfer verified end to end. **Bring-up:** B4.

The screen shows the card and the link as the mount left them, from the SDK's bring-up record (`SDCardManager::cardInfo()`, no card I/O):

- the type and CSD capacity, and the CID name, manufacturer, OEM, revision and date;
- the bus width;
- the clock the link runs, the host cap and the card's maximum;
- the power cycles the parked mount needed, and its last error;
- the volume's used and total space.

**Run** (Confirm) writes a scratch file, `/.silkscreen-test/sdcheck.tmp`, of 8, 64 or 200 MB (Left picks). The data is pseudo-random, hashed with SHA-256 as it is written. The test reads the file back, hashes it and compares every byte with the data regenerated, then removes the file. The writes go through `HalStorage` in 32 KB calls, the reader's own path (the SDK passes every 4 KB through its DMA buffer), so the MB/s are the reader's.

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

- about 3.3 V: IO47, IO14, IO48, IO11, IO38, IO39, IO10;
- 0 V: IO12, IO13, IO21, IO40, IO5-7, IO15-17;
- IO42 below 0.385 V; `SD_VDD` (C36) below 0.5 V;
- the un-held RTC pads IO1-4, IO8 and IO9: probe and note them, since their sleep state is undocumented (POWER.md §7);
- IO41: unheld with an FT6336U, 0 V with a GT911.

Wait at least 60 s after the sleep screen, then average at least 60 s. Wake with POWER and open P5. It shows:

- the path that slept, and how the entry came: a timeout or POWER, the time from arming or the uptime, and USB power, a USB host and touch at the entry;
- the reset reason, wake cause and ext1 mask;
- the time asleep, from the RTC.

Enter the average the meter showed (Left/Right 1 µA, Up/Down 10 µA) and Confirm. The ghost and release paths first ask what the meter showed. Back skips the result.

PASS:

- the entry came the path's way, as in the table;
- the wake is `EXT1` on IO18;
- the current is at most 110 µA without touch, or 165 µA with a tail: the POWER.md §1 budget with the U3 ground current (row 4, still to measure) counted. Above 95 or 150 µA, the budget without row 4, the row carries `above_ds_budget=1` but does not fail. Typical is 58-73 and 113-130 µA; above 150 µA work through the A/B list in POWER.md §4;
- within 3 µA of the other paths measured in the same set-up since the last reset ("about 2 µA", plus the meter's last digit; the readings are kept in RTC memory);
- for the ghost and release paths, the operator saw what the table says.

With no current entered, or "Not measured", the result is INFO. The loop and boot cutoffs, three forced panics and the SD-error screen's 60 s timeout are sleep paths too; they need the reader image or a bench supply (below).

### P8 Reader-data integrity

**Purpose:** the test firmware leaves the reader's data alone: it must never write `/.crosspoint`. It is not a bring-up step; it guards the card the reader shares.

The first boot of the test firmware with a card writes `/.silkscreen-test/crosspoint.sha`. It holds the SHA-256 and size of every file directly in `/.crosspoint`, up to 32: settings, state, Wi-Fi, recent books, OPDS, KOReader and the Quick Resume frame. It also holds the names of the folders there, in directory order (one per book cache; their contents are not hashed).

- Every later boot compares the card with it and records a P8 row (`when=boot`): PASS when nothing changed, FAIL with the files that did. The boot that takes the baseline records INFO.
- The screen compares again when it opens, lists each file with its state, and Confirm records a check (`when=on_demand`).
- After running the reader image in between, which changes these files itself, take a new baseline (Right; it asks first).

PASS: every file the same, none missing or new, the same folder names, `/.crosspoint` there or not as before, and no I/O error. Run P8 before and after a session with sleeps, Wi-Fi and light changes. After flashing the reader back, check that it resumes into its book.

## Safety

- **Front light.** Ideally the TPS923610 boost never runs without a load; an unloaded enable is safe on the standard build (U10 latches off at its OVP, HARDWARE.md §13.1), and the SDK's switch-on probe takes the light dark within about 35 ms of one. A front-light test asks before each lighting session (T9's Confirm, one T10 run) which load is connected (the J3 panel light or a J6 strip, with "No load" preselected), and lights nothing without an answer; a T10 run then relights twice on that load by itself (retry, restore). T9 does not light when `LED_MONIT` reads nothing in the dark, switches the light off above 22 V, and switches it off before its Record question. T10 lowers the trip level in RAM only, never raises it, and puts it back on every way out. Nothing else in the test firmware can light it.
- **Touch.** `TP_RST` is pulsed only by the SDK's own reset (the boot probe, T12's reset path); no test holds it LOW or writes a controller configuration register. T11's mapping changes are in RAM only.
- **Pins.** Tests never call `pinMode` or `gpio_reset_pin` on IO1, IO2, IO4, IO8, IO9, IO10, IO18, IO40, IO42 or the SD pins (POWER.md ADC-01, REQUIREMENTS PWR-19), and every ADC read goes through `freeink::adc`. The boot checks are the SDK's, as in the reader: the ladder test turns each ladder pin's pulls on for about 2 ms at a reset other than a deep-sleep wake, and IO18 is read on the ADC with no pull.
- **Power button fault.** A board whose IO18 idles between the logic levels boots into the test menu (P1 and T3 fail `power_idle`), but none of its sleeps has a wake source: press SW11 (reset) to start it again.
- **Battery.** A screen that holds auto-sleep off also holds the CPU at full clock. Live readings on battery run for a bounded time or only on USB. The 15-minute ceiling, the 60-minute input cap and the low-battery cutoff are the reader's and still apply on battery.
- **Sleep and wake.** No new sleep or wake source; `scripts/check_sleep_wake_sources.sh` is unchanged and covers the test code. Timing across a sleep uses a power-button wake and a stopwatch.
- **Card.** Besides `/.silkscreen-test/`, the only writes are the reader's own: `/crash_report.txt` after a panic, `/screenshots/` (POWER + Down), and the host during USB Drive.
- **Display.** The display tests change only Sunlight Fading Fix, night mode (both through `SETTINGS`) and the panel SPI clock, all in RAM, and put them back at the end of every run and on exit. They never turn the radio on during a refresh (PWR-02).
- **RTC.** T13 writes the RTC only through `Rtc::set()` (the time, then OSF cleared) and never the control register, so EOSC stays 0 (rtc-eosc-zero). Its 400 kHz bench puts the bus back at its own clock. T15 sets the RTC through `HalClock::syncFromNTP()`, as the reader does.
- **Wi-Fi.** T15, P3's overlap and T3's idle station bring the radio up only on USB or with P+ at 3.4 V on the cell (bat-wifi-gate; P3's overlap only on USB, checked again every second of the run). T15 and P3 switch it off (`WIFI_MODE_NULL`) before they write or draw a result. T3's station never joins a network or scans, so it sends nothing; its screen keeps the reader's sleep timeout. Leaving any test switches the radio off. Wi-Fi credentials are read, never written.
- **Card.** P3 writes only `/.silkscreen-test/sdcheck.tmp`, needs 3.6 V on the cell, and removes the file at the end, on Back and on exit; a file a crash leaves is removed at the next boot. P4's USB Drive gives the host the whole card, after a warning, and P8 shows what changed.
- **Sleep paths.** P5 lowers the sleep timeout, the awake ceiling or the input cap to 1 min in RAM only (the timeout goes to Never for the ceiling and the cap), and puts them back when disarmed or left. The boot after a "no input after a wake" arming raises a saved timeout under 5 min to 5 min, in RAM, for that boot only. It never sleeps by itself: the reader's loop does, through `enterDeepSleep()`.

## Checks that need the reader image

The pinned settings, the no-write rule and the tests left out (P6 forced panics, P7 the low-battery ramp) take these paths out of the test firmware. Run them on the reader image (`silkscreen`, or `silkscreen-gh_release` for current and wake timing), with the bench equipment named:

- **Wake policy.** A click wake: Short Power Button = Sleep leaves `WAKE_POLICY_CLICK`, and the early wake decision then boots on a click. The test firmware pins Ignore, so every entry leaves the hold rule.
- **Resume.** Quick Resume and the splash-less wake (`sleep_frame.bin`, `APP_STATE`); the test firmware neither reads the state nor writes the frame.
- **Front light across sleep and restarts** (B8):
  - Restore Light on Wake;
  - with no light connected: switching it on shows "No front light detected" with the 10 s advice, and the light control stays for a retry;
  - a restore that finds no light (the light unplugged while asleep) is silent and is not repeated at the next wake; a power cycle tries once;
  - a single-colour J6 strip: the warmth row names the one string there is and ignores input;
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
- **Web server and updates.** The 15-min ceiling with the web server idle, WebDAV polling not counting as traffic, and `IO8` with the web server running (the 100 mV allowance under its load; T15's idle station is lighter). OTA and the SD firmware update refusing below 3.6 V (the test firmware has neither), and the radio released at the OTA, Clock Sync and KOReader result screens, then the restart on exit.
- **Reset loop** (WK-03): 3 forced panics in a row, with the panel refreshed fast before them. The device then sleeps at the floor, with the driver-free panel and touch sleep. With the custom panel, check that the log names UC8279 before the driver-free sleep, and that the floor matches the one after a menu sleep.
- **Reader overlay** (B2): on both panels, open the toolbar menu over a page with anti-aliased text and close it. The page is re-rendered with its gray levels, as on the X4 (`xteinkClassPanel()`).

Bench checks no test firmware screen can make, for a meter, a scope or esptool:

- B1: from EN release, CS (IO14) HIGH before the first SCLK (IO13) edge, IO42/IO40 LOW, IO10 HIGH;
- B3: `esptool chip_id` after a wake and after an SW11 reset (HW-01's flash back-feed symptom), and IO47 on a scope across a ghost wake (no LOW excursion);
- B3, B10: ghost-wake timing on a current logger, with and without a card;
- B4: `SD_VDD` at cold boot, below 0.5 V for at least 20 ms with the bus LOW before power-on (sd-coldboot-powercycle), and below 0.5 V in sleep;
- B9: unplug the touch tail while asleep; the next cold boot finds it absent;
- POWER.md §5: the awake idle current at 80 MHz (about 25-40 mA), and result screens idling at the Wi-Fi-off level.

## Writing a test

- Derive from `testsuite::TestScreen` (`src/activities/testsuite/TestScreen.h`): draw the body in `drawBody()`, name the Confirm/Left/Right actions in `slotLabel()`, act in `onSlot()`, `onStep()` and `onTick()`, and record results with `record(Verdict, "key=%d ...", ...)`. One file per test in `src/activities/testsuite/tests/`; declare its factory in `tests/TestFactories.h` and set it in `TestCatalog.cpp`.
- `drawBody()` runs on the render task. Take readings, I2C traffic and `usbHostAttached()` in `onStart()`, `onTick()` or `onSlot()` (the loop task), and draw from what they stored.
- A live reading repaints at most every few seconds (T1 and T2 use 5 s): every repaint runs at the full clock, and a screen that repaints often keeps the board off its idle clock on battery. Never hold auto-sleep off for a live reading.
- Helpers: `ask()` puts a question to the operator and answers through `onAnswer()`; `msSinceInput()` gives the time since the last key or touch input; `TestPage::setCompact()` switches rows to the small font; `appendf()` and `addFailure()` (`tests/TextBuffer.h`) build a record's text. A test whose input is the keys returns true from `keysAreTestInput()` and reads `HalGPIO` itself; its Confirm/Left/Right actions then run on a 1.5 s hold as well as on the touch buttons.
- A test whose input is the touch panel returns true from `touchIsTestInput()` while it is (T11): the touch buttons are not routed and a touch Back (header button, left-edge swipe) does not leave, so only the keys drive the screen. It can keep the bottom-edge swipe from going home by overriding `handleHomeGesture()`. `showsLastResult()` returning false leaves the whole band to the body.
- UI text is English `constexpr` strings in the test code, not `tr()` keys. This is a deliberate exception to AGENTS.md rule 5: `gen_i18n.py` collects `STR_` keys from every source file whatever its `#if`, so test-only keys would ship in every reader image. Shared screens (popups, Wi-Fi, keyboard) keep `tr()`, with the language pinned to English.
- The env builds the SDK with `-DFREEINK_DIAGNOSTICS=1`, so the bring-up records in the freeink-sdk README ("Bring-up diagnostics") are available.
- The main loop and the render task re-apply `SETTINGS.fadingFix` and `SETTINGS.screenInverted` on every pass: a display test changes those fields in RAM, never the renderer or the HAL directly.
- A display test derives from `PanelRunTest` (`tests/PanelRun.h`). `startRun(steps)` starts a run. `runStep()` draws one frame and refreshes it on the render task. `afterStep()` stores the measurement, and calls `pauseRun()` to hold the frame. `onRunEnd()` asks and records after the closing HALF repaint. `restorePanel()` puts back anything the run changed on the panel. `refreshTimed()`, `refreshAsyncStart()`/`refreshAsyncFinish()` and `grayTimed()` return a `RefreshSample` with what the driver ran. `tests/PanelPatterns.h` has the frames, and `tests/GrayPass.h` has the reader's gray-pass routes.
- `TestScreen` also offers `onBack()` (use Back for a sub-mode, or to stop a run), `edgeBands()` (edge bands the test draws itself, such as T7's rulers) and `requestCleanRepaint()` (the next repaint is a HALF).
- A test that needs an SDK object behind the HAL uses `TestSuiteAccess` (`src/activities/testsuite/TestSuiteAccess.h`). The HAL class declares it a friend under `#if CROSSPOINT_TEST_SUITE`, so the reader gains no API. `HalDisplay` (the panel driver), `HalGPIO` (`InputManager`), `HalFrontlight` (`FrontlightManager`), `HalClock` (its RTC driver and read cache) and `HalStorage` (its mutex) do so today.
- A front-light test calls `confirmLightLoad()` and lights the boost only in `onLightLoadConfirmed(load)`, which says whether J3 or J6 was named. `tests/LightCheck.h` reads `LED_MONIT` and the SDK monitor's record, reads a pad's drive from the registers, and lowers and restores the trip level.
- Scratch files go in `/.silkscreen-test/` and end in `.tmp`, never `.txt` (a book type).
- The RTC: `tests/RtcAccess.h` gives HalClock's own driver (`Rtc::address()`/`type()` say which part `begin()` found), raw register reads, UTC epoch arithmetic, and `readAtTick()`, which times the RTC to its next seconds tick. A test that writes the RTC calls `rtc::noteWritten()`, so HalClock reads the chip again and T14 does not judge a span across the write. `TestSuiteAccess` reaches the driver behind `HalClock`, and HalStorage's mutex for the SD manager's bring-up record.
- A test that needs the sleep entry this boot woke from reads `SleepRecord.h` (`wokeFromSleep()`); P5 arms a path with `armSleepPath()`. `ActivityManager::goToSleep()` and the sleep screen fill the record, never a test.
- `tests/Supply.h` reads what powers the board (P+, USB power, a cell) for a gate or a judgement on the cell; `tests/Sha256.h` wraps mbedTLS's SHA-256.
- A test holds auto-sleep off or skips the loop delay (`preventAutoSleep()`, `skipLoopDelay()`) only while a bounded run is on (P3, T15, T13's 60 s check).
- `TestPage::paragraph(text, maxLines)` ends the last line with an ellipsis when the text needs more lines: give instruction text a cap that fits it, and keep it short on screens with many rows (the full procedure is in this file). `ask()` bodies show up to 12 lines.
