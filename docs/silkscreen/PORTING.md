# Silkscreen port of CrossPoint Reader

This branch (`silkscreen`) ports CrossPoint Reader to **Silkscreen**, an open-hardware ESP32-S3 e-paper reader base board and the successor of de-link. The hardware is described in `de-link_pcb/docs/HARDWARE.md` ([iandchasse/silkscreen-pcb](https://github.com/iandchasse/silkscreen-pcb)). The firmware-facing facts taken from it, with verification notes, are in [REQUIREMENTS.md](REQUIREMENTS.md).

- **Base:** CrossPoint `develop` at `d1509d07`, with freeink-sdk at its pinned `87c4493`. The SDK work is on the submodule's `silkscreen` branch.
- **Earlier attempt:** the first port (Sep 20-21, 2026) is archived in `EPD_Hub_ESP32/silkscreen-firmware-archive/` as git bundles plus `.patch` files. It is a reference only. Its pin map was right, but several behaviours contradict the hardware spec. They are listed under [What this port does differently](#what-this-port-does-differently).

## Architecture: Silkscreen is data

freeink-sdk describes each board as a `BoardProfile` in `libs/hardware/BoardConfig/include/BoardConfig.h`, and its rule is that feature code never keys off a device name. This port follows that rule.

- `FREEINK_DEVICE_SILKSCREEN`, `Board::Silkscreen` and the `SILKSCREEN` profile live only in `BoardConfig.h`.
- Every Silkscreen need that the SDK could not already express becomes a **new, defaulted field** that generic code reads. Existing profiles compile unchanged and behave identically, because the defaults reproduce today's behaviour.
- No `isSilkscreen()` in SDK feature code. The app uses board identity only for policy (OTA board tag, the keep-awake list).

| New generic mechanism | Why Silkscreen needs it (HARDWARE.md) |
|---|---|
| `parkedPins`: per-pin boot and sleep levels | EPD `CS` has no pull-up and must be HIGH before any SCLK edge (§6.1). `ADIM`/`COLOR_SEL` must be LOW at boot and in sleep (§7). Touch is held in reset in sleep (§8). |
| `InputPins::powerExternalPull` | `IO18` already has `R76` 100 kΩ. An extra internal pull-down drags a press to ~2.5 V, marginal against VIH (§9.2). |
| `DisplayPins::busyPullDown` | `BUSY` has no external pull; a missing panel must not float. |
| SDMMC: `maxFreqKhz`, `internalPullups`, `parkLowWhenOff`, power-off/settle times | The external 10 kΩ pull-ups go to the *gated* `SD_VDD`. Internal pull-ups to 3V3 back-feed a gated card. A real power cycle needs the bus driven LOW for at least 20 ms before `SD_ACTIVATE` goes HIGH (§5, §13.1). |
| `InputStyle::AdcLadders` + `adcLadders` table | 8 keys on two ADC ladders (`IO1`, `IO4`). Readings that are not near a clean level are ignored; software debounce (§9.1). |
| `chargeLadder`: analog charge-status decoder | `USB_STAT` (`IO9`) is a 1 MΩ-class ladder. Windows: >2.6 V idle, 1.80–2.60 V on battery, 0.80–1.35 V charging, 0.35–0.70 V complete. Read ≤1 Hz after a 0.5 s settle, no pulls. "Complete" only when stable, because of the no-battery blink (§3.7, §13.1). |
| `PowerConfig::lowBatteryCutoffMv` | Firmware must sleep well before ~3.3 V; the DW01A 2.4 V cutoff is a last resort (§3.5). |
| Frontlight: `colorSelectGpio`, `enablePulseUs`, `offDwellMs`, LED monitor | `ADIM` is also the enable: a HIGH of at least 100 µs before the PWM carrier; LOW for more than 2.5 ms shuts down / clears OVP. Change `COLOR_SEL` only at duty 0. Watch `LED_MONIT` (`IO2`) for over-voltage (§7, §13.1). |
| Touch: `optional`, reset pulse, fallback controller | Touch is fitted only on -T01C/-FT01C panels and the controller part is not fixed (§8). |
| `SensorsConfig::rtcLowPower` | DS3231MZ in VBAT-only mode: disable the 32 kHz output (§10). |

## Defaults chosen for the first build

These follow the plan's recommendations and can be changed:

- **Front light compiled out** (`FREEINK_CAP_FRONTLIGHT` not set). `IO42` stays parked LOW. Enable it per build once the light is connected: bonded panel light on `J3`, or an external string on `J6`, which needs a higher monitor trip level. Never enable the boost with the load unplugged (§13.1).
- **Warm/cool is a fixed choice**, not a blend. The warmth slider snaps to warm at ≥50% (§7: blending is a qualification item).
- **SDMMC at 20 MHz**, 4-bit. Try 40 MHz only after the bring-up SD tests pass with Wi-Fi and display refresh overlapping.
- **Low-battery cutoff at 3400 mV** on `P+`, measured with the radio quiet, 3 consecutive 1 Hz samples, only while `USB_STAT` says on-battery.
- **Wake: power button only** (`IO18`, ext0).
- **Touch:** probed at runtime (FT6336-class at 0x38, then GT911 at 0x5D). If absent, the board is treated as touchless. The result is cached across deep sleep.
- **RTC:** DS3231MZ at 0x68 (`U13`, standard build).
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
| Frontlight: COLOR_SEL as a warmth PWM channel, even at brightness 0; no enable pulse | Static COLOR_SEL switched only at duty 0; ≥100 µs enable pulse; LED_MONIT over-voltage trip |
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
4. **B3 - Sleep and wake.** Sleep and wake from `IO18` repeatedly. No false wakes over 30 minutes. In sleep: `IO47` ≈ 3.3 V (RST not held LOW), `IO14` 3.3 V, `IO42`/`IO11` 0 V. `esptool chip_id` works after wake and after an SW11 reset.
5. **B4 - SD card.** Scope `SD_VDD` at cold boot: below 0.5 V for at least 20 ms with the bus LOW before power-on. Mount within 3 attempts. Copy 200 MB and verify SHA-256. With no card, a clean "SD card error" and the bus left parked. In sleep, `SD_VDD` < 0.5 V and no bus pin HIGH. USB Drive mode in and out.
6. **B5 - Buttons.** Build with `-DFREEINK_ADC_LADDER_TRACE=1` and log mV for every key on USB and on battery. Tune the windows, especially BACK, UP(2) and the released threshold, which sit near the ADC's 2.9 V ceiling. 50 presses each give 50 events, including with Wi-Fi on. Chords decode as §9.1 says or are ignored.
7. **B6 - Battery and USB_STAT.** Log `IO9`/`IO8` in each state: on battery (~2.20 V), charging (~1.11 V), complete (~0.55 V), charger idle (~3.3 V), and cell pulled on USB (0.55/0.41 V blink must never report "charged"). The on-battery level is unverified on real parts (DESIGN_REVIEW), so re-window if needed. Ramp a supply on `J5`: the device sleeps at ~3.40 V with Wi-Fi off.
8. **B7 - RTC.** Probe 0x68. OSF is set after a cell insert and cleared once time is set over NTP. Register `0x0F` bit 3 = 0. Check I2C edges at 400 kHz before raising the bus from 100 kHz.
9. **B8 - Front light** (lit builds only, load connected first). Scope `IO42`: at least 100 µs HIGH, then the carrier. COLOR_SEL changes only with ADIM at 0. `LED_MONIT` × 9.33 ≈ 15 V lit. Force a trip by lowering the trip level temporarily, never by unplugging the load.
10. **B9 - Touch** (-T01C/-FT01C only). Log the controller and address; map the four corners; `IO11` LOW in sleep.
11. **B10 - Sleep current.** A µA meter in series with the cell at 3.8 V, card fitted, light off, panel asleep: about 60 µA typical, ≤ 85 µA (§16).

## Commit plan

SDK (`freeink-sdk`, branch `silkscreen`):

1. Register Silkscreen: device flag, enum, derived capability lists and the `SILKSCREEN` profile with the verified pin map.
2. Boot/sleep pin safety: `parkedPins`, `powerExternalPull`, `busyPullDown`.
3. SDMMC: configurable clock and pull-ups; parked power-cycle mount; park on shutdown and at boot.
4. Data-driven ADC button ladders, with host tests.
5. Analog charge-status ladder and low-battery cutoff API.
6. Frontlight: colour-select, enable pulse, LED over-voltage monitor.
7. Touch optional/fallback; DS3231 low-power; one owner for the I2C bus clock.
8. README: Silkscreen in the device list and the new fields in the new-board recipe.

App (this repo, branch `silkscreen`):

1. `.gitmodules` to the fork; `silkscreen` / `silkscreen-gh_release` / `silkscreen-gh_release_rc` envs; OTA board tag; keep-awake after a USB cold boot.
2. USB presence from the charge ladder in `HalGPIO::isUsbConnected()`; low-battery cutoff in the main loop and at boot.
3. CI: build the `silkscreen` env.

Deferred until measured on hardware: `xteinkClassPanel()` (B2); keeping button hints when touch is fitted; wake from ladder keys; brown-out level; 40 MHz SDMMC/SPI.
