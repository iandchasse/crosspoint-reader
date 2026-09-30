# Silkscreen port of CrossPoint Reader

This branch (`silkscreen`) ports CrossPoint Reader to **Silkscreen**, an open-hardware ESP32-S3 e-paper reader base board and the successor of de-link. The hardware is described in `de-link_pcb/docs/HARDWARE.md` ([iandchasse/silkscreen-pcb](https://github.com/iandchasse/silkscreen-pcb)). The firmware-facing facts taken from it, with verification notes, are in [REQUIREMENTS.md](REQUIREMENTS.md).

- **Base:** CrossPoint `develop` at `d1509d07`, with freeink-sdk at its pinned `87c4493`. The SDK work is on the submodule's `silkscreen` branch.
- **Earlier attempt:** the first port (Sep 20-21, 2026) is archived in `EPD_Hub_ESP32/silkscreen-firmware-archive/` as git bundles plus `.patch` files. It is a reference only. Its pin map was right, but several behaviours contradict the hardware spec. They are listed under [What this port does differently](#what-this-port-does-differently).

## Status

This file describes the finished port. What is in the code today:

- **Landed:** SDK step 1 (`ee3501d`, board and profile) and app step 1 (`4ae622ef`, envs and wiring), then the review-fix commits (`fix(silkscreen): address review findings`), which cover SDK steps 2-5, the DS3231 low-power and RTC-fallback part of step 7, and app step 2: parked pins, no internal pull on `IO18`, `BUSY` pull-down, the parked SDMMC power cycle, the ADC button ladders, the `USB_STAT` decoder, USB presence and the low-battery cutoff.
- **Landed:** SDK step 6, the front light: the enable-gated TPS923610 path, fixed warm/cool on `COLOR_SEL`, the `LED_MONIT` over-voltage trip, one shared ADC lock for every ADC reader, and the sleep handover. The app compiles the light in by default, restores it only after a verified wake, and hands it over before deep sleep.
- **Landed:** SDK step 7, touch: an optional panel probed at boot (FT6336U at 0x38 through the FT5x06 path, GT911 fallback), its absence cached across deep sleep, the controller put to sleep at every sleep entry with `TP_RST` held HIGH, one owner for the shared I2C bus clock, stuck-bus recovery at boot and a released bus in sleep. The app keeps the key hints, the front-button remap and the other key-only UI when touch is fitted.
- **Not yet:** the SDK README (step 8), CI (app step 3), the brown-out level, the Wi-Fi TX battery gate, and staying awake on USB.
- Rows and checklist steps that depend on the missing steps are marked *planned*. Update this section as each step lands.

## Architecture: Silkscreen is data

freeink-sdk describes each board as a `BoardProfile` in `libs/hardware/BoardConfig/include/BoardConfig.h`, and its rule is that feature code never keys off a device name. This port follows that rule.

- `FREEINK_DEVICE_SILKSCREEN`, `Board::Silkscreen` and the `SILKSCREEN` profile live only in `BoardConfig.h`.
- Every Silkscreen need that the SDK could not already express becomes a **new, defaulted field** that generic code reads. Existing profiles compile unchanged and behave identically, because the defaults reproduce today's behaviour.
- No `isSilkscreen()` in SDK feature code. The app uses board identity only for policy (OTA board tag, the keep-awake list).

| New generic mechanism | Why Silkscreen needs it (HARDWARE.md) |
|---|---|
| `parkedPins`: per-pin boot and sleep levels | EPD `CS` has no pull-up and must be HIGH before any SCLK edge (§6.1). `ADIM`/`COLOR_SEL` must be LOW at boot and in sleep (§7). `TP_RST` is held HIGH, never LOW, because the controller VDD is unswitched (§8). |
| `InputPins::powerExternalPull` | `IO18` already has `R76` 100 kΩ. An extra internal pull-down drags a press to ~2.5 V, marginal against VIH (§9.2). |
| `DisplayPins::busyPullDown` | `BUSY` has no external pull; a missing panel must not float. |
| SDMMC: `maxFreqKhz`, `internalPullups`, `parkLowWhenOff`, power-off/settle times | The external 10 kΩ pull-ups go to the *gated* `SD_VDD`. Internal pull-ups to 3V3 back-feed a gated card. A real power cycle needs the bus driven LOW for at least 20 ms before `SD_ACTIVATE` goes HIGH (§5, §13.1). |
| `InputStyle::AdcLadders` + `adcLadders` table | 8 keys on two ADC ladders (`IO1`, `IO4`). Readings that are not near a clean level are ignored; software debounce (§9.1). |
| `chargeLadder`: analog charge-status decoder | `USB_STAT` (`IO9`) is a 1 MΩ-class ladder. Windows: >2.6 V idle, 1.80–2.60 V on battery, 0.80–1.35 V charging, 0.35–0.70 V complete. Read ≤1 Hz after a 0.5 s settle, no pulls. "Complete" only when stable, because of the no-battery blink (§3.7, §13.1). |
| `PowerConfig::lowBatteryCutoffMv` | Firmware must sleep well before ~3.3 V; the DW01A 2.4 V cutoff is a last resort (§3.5). |
| `FrontlightConfig`: `colorSelectGpio`/`colorSelectWarmHigh`, `enablePulseUs`, `offDwellMs`, `monitorAdc`/`monitorScaleX1000`/`monitorTripMv` | `ADIM` is also the enable: LOW for at least 3 ms (clears a latched OVP), then a HIGH of at least 100 µs, then the PWM carrier; LOW for more than 2.5 ms shuts down. Change `COLOR_SEL` only with `ADIM` dark. Watch `LED_MONIT` (`IO2`) for over-voltage (§7, §13.1). |
| `SharedAdc.h`: one recursive lock around every SDK ADC read | The LED monitor reads `IO2` on the esp_timer task while the main loop reads the ladders (`IO1`/`IO4`), `BAT_MONIT` (`IO8`) and `USB_STAT` (`IO9`). Arduino's oneshot read only try-locks the unit and returns 0 to the loser. |
| `TouchConfig`: `optional`, `resetPulseMs`/`resetSettleMs`, `fallbackController`/`fallbackAddr`, `hibernateInSleep` | Touch is fitted only on -T01C/-FT01C panels (§8). The panel drawing names an FT6336U; a GT911 stays a fallback. The controller VDD is on the unswitched 3V3, so it must be put to sleep before deep sleep. |
| `TouchConfig::buttonsAlongsideTouch`, `BoardConfig::hasFrontButtons()` / `HalGPIO::hasFrontButtons()` | Silkscreen keeps all eight keys when touch is fitted (§9.1), so key hints and the front-button remap stay. Other touch boards still hide them. |
| `SensorsConfig::i2cBusRecovery` and `SharedI2c.h` (sensor-bus clock for every driver on those pins, stuck-bus recovery, release before sleep) | RTC and touch share `IO38`/`IO39` with 2.2 kΩ pull-ups on the always-on 3V3, and `J6` exposes the bus. A line held LOW costs 1.5 mA (§10, §13). |
| `SensorsConfig::rtcLowPower` | DS3231MZ in VBAT-only mode: disable the 32 kHz output (§10). |
| `SensorsConfig::rtcFallbackType`/`rtcFallbackAddr` | Either `U13` DS3231MZ (0x68) or `U14` RV-8263-C7 (0x51) is fitted (§10). |
| `SensorsConfig::rtcResyncWhenInvalid` | No backup cell: time is lost with the battery, so fetch it again whenever the RTC reports it invalid (§10). |

## Defaults chosen for the first build

These follow the plan's recommendations and can be changed:

- **Front light: compiled in** (`FREEINK_CAP_FRONTLIGHT=1`, `FREEINK_CAP_WARMLIGHT=1` in `[silkscreen] frontlight_flags`), for a bonded panel light on `J3` or an external string on `J6`. `IO42` (ADIM) and `IO40` (COLOR_SEL) are parked LOW at boot and through sleep either way; the light code drives them only while awake. A board with neither a lit panel nor a `J6` strip must drop the light, because the boost must never run without a load (§13.1): put `[silkscreen]` / `frontlight_flags =` in `platformio.local.ini`.
- **Front-light enable:** `IO42` dark (LOW, plain GPIO) for at least 3 ms, `COLOR_SEL` set while it is LOW, `IO42` HIGH for 150 µs as a plain GPIO, then the 25 kHz / 10-bit LEDC carrier at the target duty. A brightness change only changes the duty. Off: duty 0, `IO42` LOW, `COLOR_SEL` back to LOW once dark for 3 ms. Never `gpio_reset_pin` on either pin.
- **Warm/cool is a fixed choice**, not a blend: warm (`COLOR_SEL` HIGH) at warmth ≥50%, cool below (§7: blending is a qualification item). A change while lit is a full off/on cycle (≥3 ms dark, so the enable pulse repeats).
- **Front-light over-voltage trip at 23 V** on `LED_SW` (`SILKSCREEN_FRONTLIGHT_TRIP_MV`, 23000 mV; must stay below the TPS923610's 24.25 V minimum OVP, checked by a `static_assert`). The same level serves the bonded ~15 V panel light and a `J6` string of up to 22 V. `LED_MONIT` is read every 100 ms only while lit (V = mV × 9.333). A trip takes `ADIM` LOW and latches a fault; the light stays off, with no automatic retry, until the user switches it on again.
- **SDMMC at 20 MHz**, 4-bit. Try 40 MHz only after the bring-up SD tests pass with Wi-Fi and display refresh overlapping.
- **Low-battery cutoff at 3400 mV** on `P+`: 16× averaged readings once a second with the radio off, 3 consecutive at or below the cutoff, never while `USB_STAT` reports external power (idle, charging, complete, no-battery blink) and never on a reading below 2.5 V (no cell). While `USB_STAT` is still unknown, a reading above 2.5 V proves a cell. Also checked at boot, before SD and the panel: a normal boot pays for one reading, and only a reading within 100 mV of the cutoff waits for the divider and ladder to settle.
- **Wake: power button only** (`IO18`, ext1 ANY_HIGH via `PowerManager::armPowerButtonWakeup`; no RTC_PERIPH domain needed).
- **Touch: compiled in, probed at boot.** `TP_RST` (`IO11`) LOW for ≥5 ms, then ≥300 ms before the first transaction; then the FT6336U at 0x38 (FT5x06 path: `INT` gated polling, `IO41` with the MCU pull-up while awake), then a GT911 at 0x5D/0x14 (its `INT`-strap reset). With neither, the board is touchless for the session (`ACTIVE.touch.controller = None`, so `BoardConfig::hasTouch()` and `HalGPIO::hasTouch()` agree). The result is kept in RTC memory: a deep-sleep wake skips the probe of an absent panel and starts with the controller found before; a cold boot probes again. Touch runs at the sensor-bus clock (100 kHz). The orientation (`swapXY`, `flipY`) is a guess until the corners are mapped (B9).
- **Touch in sleep.** `TP_RST` is held HIGH at boot and through sleep: an FT6336U's ~3 kΩ `RSTN` pull-up draws ~1.1 mA from a LOW pin, and a reset leaves hibernate. At every sleep entry (all paths, including the ghost-wake re-sleep and the battery cutoff), an FT6336U gets hibernate (`0xA5 = 0x03`); if it does not ACK (its Monitor mode closes the serial port), it is reset (≥5 ms, then ≥300 ms) and the write is repeated once. `TP_INT` stays unheld for an FT6336U. A GT911 gets `INT` LOW ≥5 ms, `0x8040 = 0x05`, ≥58 ms, then `INT` held LOW through sleep, released at the next boot before its reset. **When no controller answered the probe**, one hibernate write still goes to 0x38 (about 0.1 ms; a NACK with no tail), in case a tail is fitted but missed the probe; there is no reset retry and no GT911 `INT` hold, since holding `INT` LOW would wake an FT6336U. A tail that misses the probe and is in Monitor mode therefore stays at ~220 µA until the next cold boot. Building with `-DFREEINK_CAP_TOUCH=0` drops the sleep command too, so such a build must not have a touch tail fitted.
- **I2C bus.** `SensorsConfig::i2cHz` (100 kHz) is the one clock: every driver on `IO38`/`IO39` starts the bus at it, whichever starts first. The first start in a boot frees a bus a slave holds LOW (9 `SCL` clocks, then a STOP). Before deep sleep, after the touch sleep command, `Wire.end()` (which waits for a transfer in flight on another task), then `SDA`/`SCL` are left unheld inputs with no pulls, and a stuck bus is clocked free, so the 2.2 kΩ pull-ups keep both HIGH.
- **Keys with touch.** The keys stay the primary input. With touch fitted, the key hints, the front-button remap, "Orient Front Buttons", "Short Back to File Browser", the key help lines and the keyboard cursor stay, alongside the touch UI (`TouchConfig::buttonsAlongsideTouch`).
- **RTC:** DS3231MZ at 0x68 (`U13`, standard build), falling back to RV-8263-C7 at 0x51 (`U14`). EN32KHZ is cleared (or the RV-8263 CLKOUT switched off), and the time is fetched from NTP on a Wi-Fi connection whenever the RTC reports it lost.
- **Flash/PSRAM:** `esp32-s3-devkitc1-n16r8`, `dio_opi`. This is the same prebuilt core as X4 Pro, with TinyUSB MSC for USB Drive mode. 16 MB, two OTA slots (`partitions.csv`).
- **No DevKitC bench env.** The first target is the real board.
- **OTA:** unchanged. It looks for a `-silkscreen` asset in upstream releases, finds none, and reports no update.

## Build

```bash
pio run -e silkscreen
```

```bash
pio run -e silkscreen -t upload
```

`default_envs` stays upstream's `default`. For a local default, create `platformio.local.ini` (gitignored) with:

```ini
[platformio]
default_envs = silkscreen
```

## Repository layout and remotes

- The parent repo has one remote, `upstream` (crosspoint-reader). Commits stay local for now.
- The SDK submodule has `fk-upstream` (Free-Ink) and `origin` (`iandchasse/freeink-sdk`). `.gitmodules` points at the fork's `silkscreen` branch, so pushing that branch is all a fresh clone needs. Until it is pushed, `git clone --recursive` of this repo cannot fetch the SDK commits.
- On Windows, both repos need `git config core.longpaths true`.

## What this port does differently

The first port was audited item by item against HARDWARE.md. What changed:

| First port | This port |
|---|---|
| USB detect: `USB_STAT` outside 1700–2250 mV, read every loop | Windows from §13.1 (on battery 1.80–2.60 V). Sampled ≤1 Hz after settle and debounced. `ST` is not treated as proof of USB. |
| `INPUT_PULLDOWN` on the power button, `INPUT_PULLUP` on the charge pin | No internal pulls on externally biased pins |
| Internal SDMMC pull-ups plus explicit `gpio_pullup_en`; 80 ms gate pulse after slot init | No internal pulls. The bus is parked LOW before gating off, and there is a real power cycle before init. |
| Frontlight: COLOR_SEL as a warmth PWM channel, even at brightness 0; no enable pulse | Static COLOR_SEL, switched only with ADIM dark and LOW whenever the light is off; ≥3 ms dark, 150 µs enable pulse, then the carrier; LED_MONIT over-voltage trip with a latched fault. The light is restored only after a verified wake and handed over (dark, LEDC detached) before sleep. |
| Ladder pins and thresholds hard-coded (`IO1/4/8/9/2`) in InputManager | Data-driven ladder table in the profile; ignores non-clean levels; 2-sample debounce |
| `isSilkscreen()` branches in SDK feature files | None; all board data |
| UC8279 forcing, SDMMC 400 kHz / 1-bit bench knobs, 8 MB single-slot partitions, `default_envs = silkscreen, devkitc` | None of these |
| No low-battery shutdown | Cutoff at 3.4 V on battery |
| SDK commits on no remote; `.gitmodules` on Free-Ink `main` | `.gitmodules` on the fork's `silkscreen` branch |

## Bring-up checklist (real board)

Do these in order. Each step says what to measure.

1. **B0 - Power, no flash.** No panel, no card, `J3` unplugged, current-limited USB. 3V3 present; `IO42` ≈ 0 V; `IO10` HIGH, so `SD_VDD` ≈ 0 V. The USB-Serial-JTAG port enumerates.
2. **B1 - Boot safety and console.** Flash. The log shows `Device: silkscreen` and about 8 MB PSRAM. Scope `IO14`/`IO13` from EN release: CS is HIGH before the first SCLK edge. `IO42`, `IO40` LOW; `IO10` HIGH. A power-button press is logged; pressed `IO18` ≈ 3.0 V.
3. **B2 - Display.** Attach the GDEQ0426T82. FULL/HALF/FAST refreshes and the grayscale anti-aliasing pass are clean at 20 MHz SPI. Check orientation (text upright relative to the bottom keys) and measure the bezel for `viewableInsets`. Check night-mode ghosting. Then add Silkscreen to `xteinkClassPanel()` in `EpubReaderActivity.cpp` (same panel as X4).
4. **B3 - Sleep and wake.** Sleep and wake from `IO18` repeatedly. No false wakes over 30 minutes. In sleep: `IO47` ≈ 3.3 V (RST not held LOW), `IO14` 3.3 V, `IO11` 3.3 V (TP_RST HIGH), `IO38`/`IO39` 3.3 V, `IO42`/`IO40` 0 V, including after sleeping with the light on (warm and cool). `esptool chip_id` works after wake and after an SW11 reset.
5. **B4 - SD card.** Scope `SD_VDD` at cold boot: below 0.5 V for at least 20 ms with the bus LOW before power-on. Mount within 3 attempts. Copy 200 MB and verify SHA-256. With no card, a clean "SD card error" and the bus left parked. In sleep, `SD_VDD` < 0.5 V and no bus pin HIGH. USB Drive mode in and out.
6. **B5 - Buttons.** Build with `-DFREEINK_ADC_LADDER_TRACE=1` and log mV for every key on USB and on battery. Tune the windows, especially BACK, UP(2) and the released threshold, which sit near the ADC's 2.9 V ceiling. 50 presses each give 50 events, including with Wi-Fi on. Chords decode as §9.1 says or are ignored.
7. **B6 - Battery and USB_STAT.** Log `IO9`/`IO8` in each state: on battery (~2.20 V), charging (~1.11 V), complete (~0.55 V), charger idle (~3.3 V), and cell pulled on USB (0.55/0.41 V blink must never report "charged"). The on-battery level is unverified on real parts (DESIGN_REVIEW), so re-window if needed. Ramp a supply on `J5`: the device sleeps at ~3.40 V with Wi-Fi off.
8. **B7 - RTC.** Probe 0x68. OSF is set after a cell insert and cleared once time is set over NTP. Register `0x0F` bit 3 = 0. Check I2C edges at 400 kHz before raising the bus from 100 kHz.
9. **B8 - Front light** (lit builds only, load connected first). Scope `IO42`: at least 3 ms LOW, then about 150 µs HIGH, then the 25 kHz carrier. COLOR_SEL changes only with ADIM LOW (at least 3 ms after it fell). `LED_MONIT` × 9.33 ≈ 15 V lit. Force a trip by building with a temporarily lower `SILKSCREEN_FRONTLIGHT_TRIP_MV` (for example 12000), never by unplugging the load: the light goes off, the log shows the trip, and it stays off until switched on again. A ghost wake (short tap) must not light the LEDs.
10. **B9 - Touch** (-T01C/-FT01C only). An I2C scan shows 0x38 (FT6336U) or 0x5D/0x14 (GT911) next to the RTC. Build with `-DTOUCH_PROBE_DEBUG` and log the probe (controller, address, cached result); a cold boot without the tail must boot touchless, with the keys, hints and remap. Map the four corners and set `swapXY`/`flipX`/`flipY`. The keys still work with touch fitted. In sleep: `IO11` 3.3 V; for an FT6336U `IO41` unheld, for a GT911 `IO41` 0 V. On a logic analyzer at sleep entry, the `0xA5 = 0x03` write is ACKed (or NACKed, then a reset and an ACK). Current into `J4` pin 2: about 55 µA in hibernate, against 220 µA in Monitor and ~1.1 mA with `TP_RST` LOW. Unplug the tail while asleep and check that a cold boot finds it absent.
11. **B10 - Sleep current.** A µA meter in series with the cell at 3.8 V, card fitted, light off, panel asleep: about 60 µA typical, ≤ 85 µA (§16).

## Commit plan

SDK (`freeink-sdk`, branch `silkscreen`). Steps 1-7 have landed (2-5 in the review-fix commit, see [Status](#status)):

1. Register Silkscreen: device flag, enum, derived capability lists and the `SILKSCREEN` profile with the verified pin map.
2. Boot/sleep pin safety: `parkedPins`, `powerExternalPull`, `busyPullDown`.
3. SDMMC: configurable clock and pull-ups; parked power-cycle mount; park on shutdown and at boot.
4. Data-driven ADC button ladders, with host tests.
5. Analog charge-status ladder and low-battery cutoff API.
6. Frontlight: colour-select, enable pulse, LED over-voltage monitor, and the shared ADC lock.
7. Touch optional/fallback, touch sleep, keys alongside touch; one owner for the I2C bus clock, bus recovery and release. (DS3231 low-power and the RTC fallback landed earlier.)
8. README: Silkscreen in the device list and the new fields in the new-board recipe.

App (this repo, branch `silkscreen`). Steps 1-2 have landed:

1. `.gitmodules` to the fork; `silkscreen` / `silkscreen-gh_release` / `silkscreen-gh_release_rc` envs; OTA board tag; keep-awake after a USB cold boot.
2. USB presence from the charge ladder in `HalGPIO::isUsbConnected()`; low-battery cutoff in the main loop and at boot.
3. CI: build the `silkscreen` env.

Deferred until measured on hardware: `xteinkClassPanel()` (B2); the touch orientation (B9); wake from ladder keys; brown-out level; 40 MHz SDMMC/SPI.

### Notes for steps 6 and 7 (pinned core: Arduino-ESP32 3.3.11)

- **Front light (as built).** LEDC stays attached while awake; the pad moves between the plain GPIO output register (dark, and the enable pulse) and the LEDC signal through the GPIO matrix (`gpio_set_direction`, then `ledcOutputInvert(pin, false)`, which also clears the stale `GPIO_OUT` bit so a later detach or `pinMode` lands LOW). None of these touch a pull. `ledcDetach()` (sleep handover only) routes the pad back to `GPIO_OUT` and frees the channel and timer: no pin reset, no pull change. Arduino 3.x clocks LEDC from the 40 MHz XTAL, so 25 kHz allows 10 bits (1600 ≥ 1024), not 11; a `static_assert` in the profile checks it. The monitor runs on the esp_timer task (8 KB stack) and skips a tick rather than block while a light transition holds the front-light lock.
- **Touch (as built).** The touch `begin` paths run from `gpio.begin()`, before `halClock.begin()`, and Wire ignores a second master `begin`, so the first driver to start the bus sets its clock. Every touch path and every sensor driver now starts the bus through `freeink::i2c::beginBus()` at `busHz()`: the sensor-bus clock when the pins match, otherwise the path's old rate. Every existing board with touch on its sensor bus (X4 Pro, LilyGo, PaperS3, EEGO A4, Paper Mono) already had equal clocks, so none changes. The stuck-bus check runs only on the first start of an `i2cBusRecovery` bus and never under a running controller (Arduino's pin manager is checked first). The probe result sits in `RTC_DATA_ATTR` with a magic word and is trusted only when the reset reason is a deep-sleep wake. `HalPowerManager::startDeepSleep()` runs `gpio.prepareForSleep()` and `releaseSensorBusForSleep()` first, before the serial teardown, so every sleep path gets them and their logs reach the host. A present FT6336U adds the reset and settle time (~307 ms) to each boot, and to a sleep entry whose hibernate write is NACKed. The probe runs inside `gpio.begin()`, before the wake-hold sample, so with touch fitted a wake needs the power button held about 0.3 s longer, and a ghost wake stays up that much longer; moving the probe after the wake decision belongs with the WK-01 reorder.
