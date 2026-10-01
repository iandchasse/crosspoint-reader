# Silkscreen port of CrossPoint Reader

This branch (`silkscreen`) ports CrossPoint Reader to **Silkscreen**, an open-hardware ESP32-S3 e-paper reader base board and the successor of de-link. The hardware is described in `de-link_pcb/docs/HARDWARE.md` ([iandchasse/silkscreen-pcb](https://github.com/iandchasse/silkscreen-pcb)). The firmware-facing facts taken from it, with verification notes, are in [REQUIREMENTS.md](REQUIREMENTS.md). The sleep-current budget, the pin states in sleep and the battery-drain rules, with where each is implemented, are in [POWER.md](POWER.md).

- **Base:** CrossPoint `develop` at `d1509d07`, with freeink-sdk at its pinned `87c4493`. The SDK work is on the submodule's `silkscreen` branch.
  - Upstream `develop` is merged in up to `707625a4`, which keeps the SDK pin at `87c4493`. The next upstream commit, `2600a79e`, moves the pin to `2339226` and its code needs that SDK (`fetchResumable`'s `sameOrigin` callback). It waits for the SDK sync.
- **Earlier attempt:** the first port (Sep 20-21, 2026) is archived in `EPD_Hub_ESP32/silkscreen-firmware-archive/` as git bundles plus `.patch` files. It is a reference only. Its pin map was right, but several behaviours contradict the hardware spec. They are listed under [What this port does differently](#what-this-port-does-differently).

## Status

Every step of the commit plan has landed, in these local commits. SDK is `freeink-sdk` on its `silkscreen` branch; App is this repo on `silkscreen`. Nothing has run on a board yet, so every hardware claim still waits for the [bring-up checklist](#bring-up-checklist-real-board).

| Area | SDK | App | What landed |
|---|---|---|---|
| Plan and requirements | | `be3d2592` | This file and REQUIREMENTS.md. |
| Board registration | `771de19` | `79c214a4` | Device flag and `SILKSCREEN` profile with the verified pin map. `silkscreen`, `silkscreen-gh_release` and `silkscreen-gh_release_rc` envs, OTA board tag, awake after a USB cold boot. |
| Pins, SD, keys, charging, cutoff, RTC | `abca595` | `7f2450c4` | Parked pins, no internal pull on `IO18`, `BUSY` pull-down, the parked SDMMC power cycle, the ADC key ladders, the `USB_STAT` decoder and USB presence, the low-battery cutoff at boot and in the loop, DS3231 low power, the RV-8263 fallback and the NTP resync. |
| Front light | `82fe8ab` | `ccf474d2` | Enable-gated TPS923610 path, fixed warm/cool on `COLOR_SEL`, `LED_MONIT` over-voltage trip, one ADC lock for every reader. Compiled in by default, restored only after a verified wake, handed over before deep sleep. |
| Touch and I2C | `a26e398` | `88d14b28` | Optional touch probed at boot (FT6336U at 0x38, GT911 fallback), with the result cached across deep sleep. Controller put to sleep with `TP_RST` held HIGH. Key hints, remap and the other key UI kept with touch. One I2C clock owner, stuck-bus recovery, a released bus in sleep. |
| Battery drain | `983f2a7` | `b86e9d49` | Drain rules ZD, PB, WK, BAT and AW (see [POWER.md §3](POWER.md#3-firmware-rules-and-where-they-live)). In sleep: EPD SPI lines and SD bus held, no `BUSY` pull-down, no `RST` glitch on wake. Power button: bounded release wait with wake-on-release. Boot: sleep-or-boot decided before SD, touch and the panel; sleep after 3 abnormal resets. Battery: cutoff also with Wi-Fi on, a floor for firmware writes. Awake: 15-min ceiling on battery, Wi-Fi off when an update check ends, USB Drive ends on cable pull. The sleep/wake guard script. |
| Docs and CI | `097bcfd` | `docs(silkscreen)` commit after `b86e9d49` | SDK README (device list, every defaulted field). POWER.md, the REQUIREMENTS.md corrections, this status and the Next section. The `silkscreen` env in the CI build matrix. |
| Port review | `720da26` | `fix(silkscreen): address port review` | Network result screens drop the radio and stop spinning; driver-free panel and touch sleep after a reset loop; GT911 INT re-held after a wake; one-sample idle key poll; `USB_STAT` unknown-band hold and `P+` cross-check; cutoff during uploads and downloads; WK-02, the SD-error timeout and a 60-min input-only cap; WebDAV polling no longer counts as traffic; the light's trip persisted and the light dark before every restart; a key path to the light panel; compile-time capability flags so C3 builds drop the Silkscreen mechanisms; the sleep guard in CI. |
| Panel controller probe | `0c34a6d` | `feat(silkscreen): probe the panel controller` | SSD1677 (Good Display panels) or UC8279D (the custom panel), probed at every cold boot and kept in RTC memory across deep sleep; any UltraChip answer runs the UC8279 driver (UC8179 not linked); a build override; the BUSY pull by controller; each controller's deep sleep on every path, the driver-free one included; the reader's X4 overlay policy on both panels; `viewableInsets` `{0,0,0,0}`; P1 and T4 show and log the probe. |
| Front-light load probe | `b7b00c8` | `feat(silkscreen): detect the front-light load at switch-on` | Each switch-on the user asks for (and Restore Light on Wake) reads `LED_MONIT` every 1 ms for 60 ms; a string with no load goes dark at once. A single-colour light keeps to its one string until a cold boot. "No front light detected" with the 10 s advice, the control kept for a retry; the result kept in RTC memory, so a wake never restores into nothing. `COLOR_SEL` waits for the rail after a trip. T9 and T10 report the probe. |
| Boot checks and probe identity | `3ee11bc` | `feat(silkscreen): check the keys, power button, touch and RTC at boot` | A key ladder built without its pull-up is found at boot and switched off. `IO18` idling between the logic levels (R36 and R72 both fitted) arms no wake, and the reader says why. 0x38 counts as touch only with a FocalTech chip id, and 0x68 is written only once it shows the DS3231 register pattern, so a J6 device there is left alone. The GT911 fallback is built only for the alternate J4 jumpers, and the reset-loop sleep no longer wakes a GT911. P1, P2, T3, T11 and T13 report the checks. |

`7f2450c4`'s message names its SDK commit by an earlier hash, `ce61aa8`; the pointer it records is `abca595`.

**Not yet:**

- the brown-out level;
- the Wi-Fi TX battery gate;
- staying awake while USB is present (REQUIREMENTS `usb-stay-awake-for-flash`);
- a lower idle clock ([POWER.md §5](POWER.md#5-the-awake-idle-tail));
- the items listed under "Deferred until measured on hardware" in the [commit plan](#commit-plan).

## Architecture: Silkscreen is data

freeink-sdk describes each board as a `BoardProfile` in `libs/hardware/BoardConfig/include/BoardConfig.h`, and its rule is that feature code never keys off a device name. This port follows that rule.

- `FREEINK_DEVICE_SILKSCREEN`, `Board::Silkscreen` and the `SILKSCREEN` profile live only in `BoardConfig.h`.
- Every Silkscreen need that the SDK could not already express becomes a **new, defaulted field** that generic code reads. Existing profiles compile unchanged, and the defaults reproduce their behaviour.
- The mechanisms behind the ladder, charge-ladder, power-policy, I2C-recovery, display-probe, front-light-probe and identity-check fields are compiled in only with `FREEINK_CAP_ADC_LADDERS`, `FREEINK_CAP_CHARGE_LADDER`, `FREEINK_CAP_POWER_POLICY`, `FREEINK_CAP_I2C_RECOVERY`, `FREEINK_CAP_DISPLAY_PROBE_POLICY`, `FREEINK_CAP_FRONTLIGHT_PROBE` and `FREEINK_CAP_PROBE_IDENTITY` (on for Silkscreen, the front-light probe only with the light compiled in; the accessors `BoardConfig::hasAdcLadders()`, `hasChargeStatusLadder()`, `powerPolicy()`, `freeink::i2c::busRecoveryEnabled()`, `displayProbePolicy()`, `displayBusyPullUp()`, `touchIdentityCheck()`, `rtcIdentityCheck()` and `FrontlightManager::load()` / `stringMissing()` / `restoreAllowed()` fold to off without them), so the ESP32-C3 build and the other S3 boards do not carry them.
- **Cross-board changes (intentional fixes).** These apply to every board, with no profile field:
  - the light is begun only after the wake decision on every frontlit board, so a ghost wake or a USB-cold-boot re-sleep no longer flashes Restore Light on Wake;
  - `EpdBus::begin()` presets CS HIGH before SPI starts and RESET HIGH before its output is enabled;
  - `holdRailOff()` presets the level before enabling the output;
  - one ADC lock around every SDK ADC read (`SharedAdc.h`). Arduino's oneshot read only try-locks the unit and returns 0 to the loser, so on X4, X3 and de-link a render-task battery read could turn a concurrent key-ladder read into 0 (a phantom RIGHT/DOWN) or a battery sample into 0 mV;
  - touch drivers on the sensor-bus pins start the bus at `SensorsConfig::i2cHz` (`busHz()`), the clock every existing board already used.
- No `isSilkscreen()` in SDK feature code. The app uses board identity only for policy (OTA board tag, the keep-awake list).

| New generic mechanism | Why Silkscreen needs it (HARDWARE.md) |
|---|---|
| `parkedPins`: per-pin boot and sleep levels | EPD `CS` has no pull-up and must be HIGH before any SCLK edge (§6.1). `ADIM`/`COLOR_SEL` must be LOW at boot and in sleep (§7). `TP_RST` is held HIGH, never LOW, because the controller VDD is unswitched (§8). |
| `InputPins::powerExternalPull` | `IO18` already has `R76` 100 kΩ. An extra internal pull-down drags a press to ~2.5 V, marginal against VIH (§9.2). |
| `DisplayPins::busyPullDown` | `BUSY` has no external pull; a missing panel must not float. |
| `DisplayPins::busyPullUp` | The same for a UC8279's active-low `BUSY_N`, whose wait has no timeout: a missing panel must read idle, not hang the loop awake. |
| `BoardProfile::displayProbe` (`DisplayProbeConfig`: `forced`, `ultraChipController`, `cacheAcrossDeepSleep`, `x4ClassPanel`), `freeink::displayControllerSource()` | The panel is a GDEQ0426T82 (SSD1677) or the custom UC8279D panel, on the same pins (§6). The SDK display probe (`XteinkDetect`) tells them apart by a half-duplex read on SDI (IO12 via R29); any UltraChip answer is the UC8279D, the verdict must never go stale in NVS, and a probe that misbehaves needs a build override. |
| SDMMC: `maxFreqKhz`, `internalPullups`, `parkLowWhenOff`, power-off/settle times | The external 10 kΩ pull-ups go to the *gated* `SD_VDD`. Internal pull-ups to 3V3 back-feed a gated card. A real power cycle needs the bus driven LOW for at least 20 ms before `SD_ACTIVATE` goes HIGH (§5, §13.1). |
| `InputStyle::AdcLadders` + `adcLadders` table | 8 keys on two ADC ladders (`IO1`, `IO4`). Readings that are not near a clean level are ignored; software debounce (§9.1). |
| `chargeLadder`: analog charge-status decoder | `USB_STAT` (`IO9`) is a 1 MΩ-class ladder. Windows: >2.6 V idle, 1.80–2.60 V on battery, 0.80–1.35 V charging, 0.35–0.70 V complete. Read ≤1 Hz after a 0.5 s settle, no pulls. "Complete" only when stable, because of the no-battery blink (§3.7, §13.1). |
| `PowerConfig::lowBatteryCutoffMv` | Firmware must sleep well before ~3.3 V; the DW01A 2.4 V cutoff is a last resort (§3.5). |
| `FrontlightConfig`: `colorSelectGpio`/`colorSelectWarmHigh`, `enablePulseUs`, `offDwellMs`, `monitorAdc`/`monitorScaleX1000`/`monitorTripMv` | `ADIM` is also the enable: LOW for at least 3 ms (clears a latched OVP), then a HIGH of at least 100 µs, then the PWM carrier; LOW for more than 2.5 ms shuts down. Change `COLOR_SEL` only with `ADIM` dark. Watch `LED_MONIT` (`IO2`) for over-voltage (§7, §13.1). |
| `FrontlightConfig`: `loadProbeMs`, `railSafeV` | The light may not be fitted, and nothing sees a load without enabling the boost: sample `LED_MONIT` quickly right after the enable, and turn the light off on a reading above the trip (§13.1). After an open-load enable `C9` stays charged near the OVP level, so nothing may close a string's return until it has fallen (the 10 s wait before plugging a light in, §13.1). |
| `SharedAdc.h`: one recursive lock around every SDK ADC read | The LED monitor reads `IO2` on the esp_timer task while the main loop reads the ladders (`IO1`/`IO4`), `BAT_MONIT` (`IO8`) and `USB_STAT` (`IO9`). Arduino's oneshot read only try-locks the unit and returns 0 to the loser. |
| `TouchConfig`: `optional`, `resetPulseMs`/`resetSettleMs`, `fallbackController`/`fallbackAddr`, `hibernateInSleep` | Touch is fitted only on -T01C/-FT01C panels (§8). The panel drawing names an FT6336U; a GT911 is a fallback only for the alternate J4 jumpers (`SILKSCREEN_TOUCH_ALT_JUMPERS`, below). The controller VDD is on the unswitched 3V3, so it must be put to sleep before deep sleep. |
| `TouchConfig::requireFocalTechId`, `SensorsConfig::rtcIdentityCheck` (`FREEINK_CAP_PROBE_IDENTITY`, rules in `ProbeChecks.h`) | `J6` exposes the I2C bus (§11), so an accessory can sit at the touch (0x38) or RTC (0x68) address. Each is identified before anything is written to it: the FocalTech chip id at `0xA3`, the DS3231 register pattern. Not on Paper Mono, whose FT6336 serves a zero ID window. |
| `SILKSCREEN_TOUCH_ALT_JUMPERS` (build flag, default 0) | A GT911 tail needs the alternate jumpers (R43/R66), and the GT911 probe drives `TP_INT` (`IO41`) push-pull for its address strap. The standard FT6336U tail never needs it, so the fallback is built only on request. |
| `PowerConfig::ladderPresenceProbe`, `InputManager::ladderPresence()` | A ladder may be built without its pull-up when none of its keys is fitted (de-link_pcb README). A floating ladder pin reads phantom keys that keep the board awake or start recovery and screenshots. |
| `PowerConfig::powerPinIdleCheck`, `PowerManager::checkPowerPinIdle()` / `powerPinCheck()` | R36 and R72 fitted together make `IO18` idle at about 1.76 V (§9.2): a wake storm or a dead button. |
| `TouchConfig::buttonsAlongsideTouch`, `BoardConfig::hasFrontButtons()` / `HalGPIO::hasFrontButtons()` | Silkscreen keeps all eight keys when touch is fitted (§9.1), so key hints and the front-button remap stay. Other touch boards still hide them. |
| `SensorsConfig::i2cBusRecovery` and `SharedI2c.h` (sensor-bus clock for every driver on those pins, stuck-bus recovery, release before sleep) | RTC and touch share `IO38`/`IO39` with 2.2 kΩ pull-ups on the always-on 3V3, and `J6` exposes the bus. A line held LOW costs 1.5 mA (§10, §13). |
| `SensorsConfig::rtcLowPower` | DS3231MZ in VBAT-only mode: disable the 32 kHz output (§10). |
| `PowerManager::powerDownRailsForSleep()`: `parkedPins` sleep levels (now also `InputNoPull`), `busyPullDown` taken off before sleep, a `parkLowWhenOff` SDMMC bus held LOW through sleep | The panel is on the always-on 3V3, so `CS`/`SCLK`/`MOSI`/`DC` must not float, and un-held RTC pads are undefined in deep sleep (§6.1). The SSD1677 drives `BUSY` HIGH through its deep sleep, so a latched pull-down would draw ~73 µA. A floating SD bus line back-feeds the gated card through its pull-ups (§5). |
| `PowerConfig::releaseWaitMaxMs`, `PowerManager::takeReleaseWake()` | There is no power latch, so a held or stuck power button (§9.2) kept the chip spinning in the release wait. After 5 s it now sleeps with ext1 armed for the release, and that wake sleeps again at once. |
| `PowerConfig::earlyWakeDecision`, `InputManager::begin(false)` / `startTouch()` | Every ghost wake used to mount SD (up to 3 power cycles), load the settings and probe touch (~0.3 s) before deciding to sleep. |
| `PowerConfig::abnormalResetLimit`, `PowerManager::countAbnormalResets()` | A crash or brown-out loop reboots at 50-150 mA indefinitely. After 3 in a row the board sleeps until the power button. |
| `PowerConfig::cutoffRadioAllowanceMv`, `BatteryMonitor::isBelowCutoff(..., radioOn)` | `BAT_MONIT` reads `P+` behind 0.24-0.35 Ω (§3.6), so it reads low under Wi-Fi load. The cutoff used to be skipped whenever Wi-Fi was on. |
| `PowerConfig::firmwareUpdateMinMv` | A firmware write that browns out near empty (§3.5 headroom) is worse than no update. |
| `PowerConfig::awakeCeilingMin`, `PowerConfig::radioOffWhenIdle` | There is no hardware off (§3.5): a network screen or a Never timeout left open on battery empties the cell overnight at tens of mA. |
| `PowerConfig::wakeNoInputTimeoutS`, `storageErrorSleepS`, `awakeInputCapMin` | A pocket press that passes the hold check, a missing card, or a desktop polling a mounted WebDAV share would otherwise keep the board awake for a whole timeout or longer. |
| `ChargeStatusLadder::unknownHoldSamples`, the `P+` cross-check in `BatteryMonitor::pollChargeStatus()` | The on-battery `USB_STAT` level is unmeasured and could land in a gap, or an unpowered TP4056 could read "charging" (POWER.md HW-08): neither may switch the cutoff off for good (§3.5). |
| `PowerManager::sleepPanelWithoutDriver()` (SSD1677, and with the display-probe policy UC8279/UC8179), `InputManager::resetAndHibernateTouch()`, `holdTouchIntForSleep()` | The panel and touch sit on the always-on 3V3 (§6.1, §8), and nothing resets them after a crash; S3 drops a digital pad's hold at the wake, which a GT911's INT needs. |
| `InputManager::adcLadderContact()` | The idle poll every 10 ms needs only "released or not", not the 16× oversampled decode. |
| `SensorsConfig::rtcFallbackType`/`rtcFallbackAddr` | Either `U13` DS3231MZ (0x68) or `U14` RV-8263-C7 (0x51) is fitted (§10). |
| `SensorsConfig::rtcResyncWhenInvalid` | No backup cell: time is lost with the battery, so fetch it again whenever the RTC reports it invalid (§10). |

## Defaults chosen for the first build

These follow the plan's recommendations and can be changed:

- **Front light: compiled in** (`FREEINK_CAP_FRONTLIGHT=1`, `FREEINK_CAP_WARMLIGHT=1` in `[silkscreen] frontlight_flags`), for a bonded panel light on `J3` or an external string on `J6`. `IO42` (ADIM) and `IO40` (COLOR_SEL) are parked LOW at boot and through sleep either way; the light code drives them only while awake. A board with no light keeps the same image: the boost runs only when the user switches the light on, and the switch-on probe then finds no load, says so and stays dark (below). An unloaded enable is safe on the standard build (U10 latches off at its OVP, §13.1) but avoidable, so a board that will never have a light may still drop it with `[silkscreen]` / `frontlight_flags =` in `platformio.local.ini`.
- **Front-light enable:** `IO42` dark (LOW, plain GPIO) for at least 3 ms, `COLOR_SEL` set while it is LOW, `IO42` HIGH for 150 µs as a plain GPIO, then the 25 kHz / 10-bit LEDC carrier at the target duty. A brightness change only changes the duty. Off: duty 0, `IO42` LOW, `COLOR_SEL` back to LOW once dark for 3 ms. Never `gpio_reset_pin` on either pin.
- **Warm/cool is a fixed choice**, not a blend: warm (`COLOR_SEL` HIGH) at warmth ≥50%, cool below (§7: blending is a qualification item). A change while lit is a full off/on cycle (≥3 ms dark, so the enable pulse repeats).
- **Front-light over-voltage trip at 23 V** on `LED_SW` (`SILKSCREEN_FRONTLIGHT_TRIP_MV`, 23000 mV; must stay below the TPS923610's 24.25 V minimum OVP, checked by a `static_assert`). The same level serves the bonded ~15 V panel light and a `J6` string of up to 22 V. `LED_MONIT` is read every 100 ms only while lit (V = mV × 9.333). A trip takes `ADIM` LOW and latches a fault; the main loop then switches the light off and clears the saved Frontlight preference, so no wake, restart or power cycle retries it. It stays off until the user switches it on again.
- **Front-light load probe** (`loadProbeMs` 60, `railSafeV` 15; `FREEINK_CAP_FRONTLIGHT_PROBE`). Only a user action (switching the light on, a colour change while lit) or Restore Light on Wake enables the boost; nothing probes by itself at a cold boot or a wake. After each enable pulse `LED_MONIT` is read every 1 ms for 60 ms. An open string drives `LED_SW` to U10's OVP within a few ms, and the 10.7 ms `LED_MONIT` RC crosses the 23 V trip 25-30 ms later, usually before U10's first retry. A reading above the trip takes `ADIM` LOW at once (U10 shuts down 2.5 ms later, which clears its latch; the next enable waits the 3 ms dwell) and is a "no load" result for the string `COLOR_SEL` selects, kept apart from an in-use trip.
  - With a colour select the other string is tried in the same switch-on. If it lights, the first is recorded missing and warmth lights the other string until a cold boot; the light panel's warmth row says "Cool light only" or "Warm light only" and ignores input.
  - If nothing lights, the light is off and the SDK's record says no light. The reader says "No front light detected. Wait about 10 s before connecting a light." (a popup; in the light panel, a line under the brightness row), saves the light as off, and keeps the control: switching it on again is the retry.
  - The record sits in RTC memory (8 bytes) and is trusted after a deep-sleep wake or a software restart only. Restore Light on Wake asks it first, so a wake never restores into nothing; a cold boot clears it, so a restore may try once.
  - After any trip, `C9` stays near the OVP level, so `COLOR_SEL` moves only once `LED_SW` reads 15 V or less (the bonded string's forward voltage at 13 mA), waited for at most 4 s: before a second try, on the monitor timer while dark, and in `prepareForSleep()`.
  - A switch-on that finds a load holds the caller for the 60 ms window. One that falls back to the other string adds the rail wait (about 1-3 s) and a second window.
- **The light goes dark before every software restart** (a shutdown handler registered by `HalFrontlight::begin()`): `ADIM` LOW, 3 ms, then `COLOR_SEL` LOW, before the reset releases both pads.
- **Keys reach the light panel:** with keys and a light, Settings > Display > Frontlight opens it. Left/Right set the brightness, Confirm switches the light, Up/Down pick warm or cool, Back closes it.
- **Panel controller: probed.** Good Display GDEQ0426T82 panels carry an SSD1677; the custom 4.26" panel a UC8279D, same 800x480 and orientation. At every cold boot, before `display.begin()`, the SDK probe resets the panel and reads its VER/FLG registers half-duplex on SDI (about 66 ms; an UltraChip part answers, an SSD1677 only by elimination). Any UltraChip answer runs the UC8279 driver (the X4 Pro's `Uc8279X4Driver`, its waveforms and LUTs unchanged); its LUT_VER (VER byte 2) becomes the driver variant, and an id the driver does not know gets its default tables, both logged. The UC8179 driver is not linked. A conclusive verdict is kept in RTC memory and reused on a deep-sleep wake with no reset and no bus traffic; any other reset probes again, and nothing goes to NVS, so a panel swapped with the power off is always found. An inconclusive probe runs as SSD1677 and is not kept. The log shows `Panel controller <name> (<how>): VER .. FLG .. variant ..`. `[silkscreen] display_flags = -DSILKSCREEN_DISPLAY_CONTROLLER=uc8279` (or `ssd1677`) in `platformio.local.ini` forces the controller and skips the probe.
- **BUSY pull by controller:** the internal pull-down for an SSD1677's active-high `BUSY`, the pull-up for a UC8279's active-low `BUSY_N`, awake only; both are off before deep sleep (IO48 is never held).
- **Reader overlay:** Silkscreen's panels are X4-class glass (`x4ClassPanel`), so the reader treats them as the X4 (`xteinkClassPanel()`: closing an overlay re-renders the page to restore its gray levels) whenever the driver for the probed controller runs the grayscale pass.
- **`viewableInsets` `{0,0,0,0}`:** every measured case window is the active area plus 0.5-0.7 mm.
- **SDMMC at 20 MHz**, 4-bit. Try 40 MHz only after the bring-up SD tests pass with Wi-Fi and display refresh overlapping.
- **Low-battery cutoff at 3400 mV** on `P+`: 16× averaged readings once a second, 3 consecutive at or below the cutoff, never while `USB_STAT` reports external power (idle, charging, complete, no-battery blink) and never on a reading below 2.5 V (no cell). While `USB_STAT` is still unknown, a reading above 2.5 V proves a cell. With Wi-Fi on the threshold is 100 mV lower (3300 mV) for the `P+` sag under radio load (0.24-0.35 Ω × 100-300 mA, §3.6), and the sleep entry turns Wi-Fi off. Also checked at boot, before SD and the panel: a normal boot pays for one reading, and only a reading within 100 mV of the cutoff waits for the divider and ladder to settle. A cutoff at boot after anything but a deep-sleep wake first begins the panel without a refresh and sends its deep-sleep command, since no sleep entry did. Web uploads and OPDS and font downloads block the loop, so they check the same (latched) cutoff from their callbacks and abort; the next loop pass sleeps. A `USB_STAT` reading stuck outside every window for 30 s drops the last decoded state (and USB presence reads "not connected"), and Charging/Complete with `P+` at or below the cutoff and not rising for 5 min (read once a minute) is treated as on battery.
- **Firmware writes need 3.6 V on battery** (`firmwareUpdateMinMv`; 3.5 V as read with Wi-Fi on, the same 100 mV allowance). OTA and the SD firmware update refuse below it unless `USB_STAT` shows external power, with "Battery too low, connect USB" (`STR_BATTERY_TOO_LOW_FOR_UPDATE`). The SD flow checks again right before the write.
- **Wake: power button only** (`IO18`, ext1 ANY_HIGH via `PowerManager::armPowerButtonWakeup`; no RTC_PERIPH domain needed). `scripts/check_sleep_wake_sources.sh` fails if another wake source, light sleep, a power-domain override or a second `esp_deep_sleep_start()` appears in `src/`, `lib/` or the SDK.
- **Sleep-current target:** ≈58 µA typical without touch (≈95 µA by datasheet maxima), ≈113 µA with an FT6336U tail hibernated. The itemized budget, the per-pin sleep states and the measurement procedure are in [POWER.md](POWER.md).
- **A held power button at sleep entry:** wait at most 5 s for the release (`releaseWaitMaxMs`), then sleep with ext1 ANY_LOW on `IO18` (wake on release). That boot sleeps again at once (`takeReleaseWake()`, first thing in `setup()`, before any peripheral starts).
- **Sleep-or-boot is decided right after the wake-hold check** (`earlyWakeDecision`), before touch, the RTC, SD, the settings, the light, the panel and Wi-Fi. Short Power Button Press lives on SD, so every sleep entry with the settings loaded leaves the wake rule (a click wakes, or the hold is required) in RTC memory. With no rule there (the first wake after a power-on), the decision waits for the settings as before, and if SD then fails, an unverified wake still sleeps. The touch probe runs after the decision.
- **3 abnormal resets in a row** (panic, watchdog, CPU lockup, brown-out, or an aborted sleep entry; `abnormalResetLimit`) and the next boot sleeps at once, with no SD, radio, display driver or touch driver. Without their drivers, the panel controller is probed, then the panel gets a reset pulse (analog off) and its bit-banged deep sleep (SSD1677 `0x10`/`0x03`, UC8279 `0x07`/`0xA5`), and an FT6336U gets a reset and one hibernate write. A power-button wake starts a new count, and so does 60 s of uptime.
- **Awake ceiling on battery: 15 min** (`awakeCeilingMin`) with no key, touch or tilt input and no web-server request or WebSocket frame. OPTIONS, HEAD, PROPFIND, LOCK and UNLOCK do not count: an OS polls a mounted WebDAV share with them. It ends network screens, the keyboard, OTA and a Never timeout. A sleep timeout the user set still governs every other screen. **60 min with no input** ends any screen on battery, client traffic or not (`awakeInputCapMin`).
- **After a power-button wake with no input: sleep after 3 min on battery** (`wakeNoInputTimeoutS`, WK-02). The wake press's own release does not count as input, and a screen that holds auto-sleep off keeps it off. **The SD-card error screen sleeps after 60 s on battery** (`storageErrorSleepS`).
- **Wi-Fi off when a network screen's work ends** (`radioOffWhenIdle`): OTA, Clock Sync and KOReader Auth switch the radio off at their result screens and stop spinning the loop; KOReader Sync switches it off on a failure and drops to modem sleep on the result screens that can still upload. Font Download keeps the radio for its lists but spins only while loading or downloading, and after a download refreshes the idle timer once instead of holding auto-sleep off. Clear Cache spins only while clearing. Leaving any of them still restarts to clear the heap.
- **USB Drive ends when the cable is pulled on battery:** once `USB_STAT` has shown external power in the session, 3 s of "on battery" ends the session and restarts to Home.
- **Touch: compiled in, probed at boot.** `TP_RST` (`IO11`) LOW for ≥5 ms, then ≥300 ms before the first transaction; then the FT6336U at 0x38 (FT5x06 path: `INT` gated polling, `IO41` with the MCU pull-up while awake). The probe reads the chip id (`0xA3`) before its first write and accepts only a FocalTech one (0x06, 0x36, 0x64; `requireFocalTechId`): a J6 device at 0x38 is left alone, and the refusal is kept across deep sleep like an absent panel. A GT911 at 0x5D/0x14 (its `INT`-strap reset) is tried next only in a build with `[silkscreen] touch_flags = -DSILKSCREEN_TOUCH_ALT_JUMPERS=1` (alternate J4 jumpers), so a default build never drives `IO41`. With nothing found, the board is touchless for the session (`ACTIVE.touch.controller = None`, so `BoardConfig::hasTouch()` and `HalGPIO::hasTouch()` agree). The result is kept in RTC memory: a deep-sleep wake skips the probe of an absent panel and starts with the controller found before; a cold boot probes again. Touch runs at the sensor-bus clock (100 kHz). The orientation (`swapXY`, `flipY`) is a guess until the corners are mapped (B9).
- **Touch in sleep.** `TP_RST` is held HIGH at boot and through sleep: an FT6336U's ~3 kΩ `RSTN` pull-up draws ~1.1 mA from a LOW pin, and a reset leaves hibernate. At every sleep entry after the probe (menu, auto-sleep, the awake ceiling, the battery cutoff), an FT6336U gets hibernate (`0xA5 = 0x03`); if it does not ACK (its Monitor mode closes the serial port), it is reset (≥5 ms, then ≥300 ms) and the write is repeated once. `TP_INT` stays unheld for an FT6336U. A GT911 gets `INT` LOW ≥5 ms, `0x8040 = 0x05`, ≥58 ms, then `INT` held LOW through sleep, released at the next boot before its reset. **When no controller answered the probe**, one hibernate write still goes to 0x38 (about 0.1 ms; a NACK with no tail), in case a tail is fitted but missed the probe; there is no reset retry and no GT911 `INT` hold, since holding `INT` LOW would wake an FT6336U. A tail that misses the probe and is in Monitor mode therefore stays at ~220 µA until the next cold boot. A ghost wake and a release wake sleep before the probe and send an FT6336U nothing: it is still as the last sleep entry left it. A GT911 is not, because S3 drops the `INT` hold at the wake: with the probe cached as GT911, `INT` is driven LOW again as soon as the keys are set up, a ghost wake re-sends its sleep command, and a release wake re-holds `INT` LOW. The abnormal-reset sleep resets an FT6336U and sends one hibernate write once its chip id reads back, since nothing after the crash put it to sleep. An alternate-jumper build never pulses `TP_RST` there (a reset without the `INT` strap restarts a GT911 awake): it tries the FocalTech hibernate without it, then the GT911 sleep command at 0x5D and 0x14, and holds `INT` LOW either way. With `requireFocalTechId`, a sleep write to a controller the probe did not find first reads its chip id, and a device the probe refused gets none. A Silkscreen build with `-DFREEINK_CAP_TOUCH=0` fails a `static_assert`, because it could not send the sleep command to a fitted tail.
- **I2C bus.** `SensorsConfig::i2cHz` (100 kHz) is the one clock: every driver on `IO38`/`IO39` starts the bus at it, whichever starts first. The first start in a boot frees a bus a slave holds LOW (9 `SCL` clocks, then a STOP). Before deep sleep, after the touch sleep command, `Wire.end()` (which waits for a transfer in flight on another task), then `SDA`/`SCL` are left unheld inputs with no pulls, and a stuck bus is clocked free, so the 2.2 kΩ pull-ups keep both HIGH.
- **Keys with touch.** The keys stay the primary input. With touch fitted, the key hints, the front-button remap, "Orient Front Buttons", "Short Back to File Browser", the key help lines and the keyboard cursor stay, alongside the touch UI (`TouchConfig::buttonsAlongsideTouch`). The Sunlight Fading Fix row stays too, whether or not the touch probe found a tail: it depends on the panel, which is the same either way.
- **RTC:** DS3231MZ at 0x68 (`U13`, standard build), falling back to RV-8263-C7 at 0x51 (`U14`). Before its control and status registers are written, 0x68 must show the DS3231 register pattern (`rtcIdentityCheck`: the bits the datasheet fixes at 0, a day of week of 1-7, BCD seconds and minutes; read again after 20 ms on a mismatch). A device without it is logged (`Device at 0x68 is not the RTC`) and left alone, and 0x51 is tried. EN32KHZ is cleared (or the RV-8263 CLKOUT switched off), and the time is fetched from NTP on a Wi-Fi connection whenever the RTC reports it lost.
- **Key ladders checked for their pull-up** (`ladderPresenceProbe`). At every reset but a deep-sleep wake, before the keys are read, each ladder pin is read with the internal pull-down and then the pull-up (about 2 ms each, both off afterwards). A pin that follows both from rail to rail (≤400 mV, ≥2600 mV) has no pull-up fitted and is switched off for the session, so it reports no phantom keys (which would hold the board awake, turn a power hold into a screenshot or start SD recovery at a wake); the log says `Key ladder IO4 has no pull-up ...`. A fitted ladder cannot pass, released or with a key held. The result is kept in RTC memory (5 bytes) for deep-sleep wakes.
- **Power button idle check** (`powerPinIdleCheck`). At every reset but a deep-sleep wake, right after the release-wake check and before anything can arm a wake, `IO18` is read on the ADC (ADC2, with the radio off): four 8-sample readings over 15 ms. If all four sit between the logic levels (R36 and R72 both fitted, about 1.76 V), the pin is switched off for the session: every sleep then arms no wake source and holds the pad with its input off, so the reset button (SW11) starts the board. The reader shows "Power button fault: its input idles at 1.7 V ..." (`STR_POWER_BUTTON_FAULT`), stays on it while `USB_STAT` shows external power (so the board can still be flashed), and sleeps on the cell. The test firmware boots on and reports it in P1 and T3.
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
| Frontlight: COLOR_SEL as a warmth PWM channel, even at brightness 0; no enable pulse | Static COLOR_SEL, switched only with ADIM dark and LOW whenever the light is off; ≥3 ms dark, 150 µs enable pulse, then the carrier; LED_MONIT over-voltage trip with a latched fault; a load probe at each switch-on the user asks for, per string, with its result kept across deep sleep. The light is restored only after a verified wake and handed over (dark, LEDC detached) before sleep. |
| Ladder pins and thresholds hard-coded (`IO1/4/8/9/2`) in InputManager | Data-driven ladder table in the profile; ignores non-clean levels; 2-sample debounce |
| `isSilkscreen()` branches in SDK feature files | None; all board data |
| UC8279 forcing, SDMMC 400 kHz / 1-bit bench knobs, 8 MB single-slot partitions, `default_envs = silkscreen, devkitc` | The controller is probed at every cold boot (SSD1677 or UC8279); forcing it is an opt-in build override. None of the other knobs. |
| No low-battery shutdown | Cutoff at 3.4 V on battery |
| SDK commits on no remote; `.gitmodules` on Free-Ink `main` | `.gitmodules` on the fork's `silkscreen` branch |

## Bring-up checklist (real board)

Do these in order. Each step says what to measure.

The hardware test firmware ([TEST-SUITE.md](TEST-SUITE.md), `[env:silkscreen-test]`) runs the firmware side of each step and logs every result to `/.silkscreen-test/results.csv`:

| Step | Tests |
|---|---|
| B1 | P1 |
| B2 | T4-T8 |
| B3 | P1 (the wake), P5 |
| B4 | P3, P4 (USB Drive) |
| B5 | T3 |
| B6 | T1, T2, P4, T15 (the radio's load) |
| B7 | P2, T13, T14, T15 |
| B8 | T9, T10 |
| B9 | P2, T11, T12 |
| B10 | P5, T12 (the touch current), T14 |

What only the reader image or bench equipment can check is listed under [Checks that need the reader image](#checks-that-need-the-reader-image).

1. **B0 - Power, no flash.** No panel, no card, `J3` unplugged, current-limited USB. 3V3 present; `IO42` ≈ 0 V; `IO10` HIGH, so `SD_VDD` ≈ 0 V. The USB-Serial-JTAG port enumerates.
2. **B1 - Boot safety and console.** Flash. The log shows `Device: silkscreen` and about 8 MB PSRAM, and P1 shows the power button idling low and both key ladders fitted. Scope `IO14`/`IO13` from EN release: CS is HIGH before the first SCLK edge. `IO42`, `IO40` LOW; `IO10` HIGH. A power-button press is logged; pressed `IO18` ≈ 3.0 V.
3. **B2 - Display.** Attach the GDEQ0426T82. FULL/HALF/FAST refreshes and the grayscale anti-aliasing pass are clean at 20 MHz SPI. Check orientation (text upright relative to the bottom keys) and that the `{0,0,0,0}` `viewableInsets` fit the case window. Check night-mode ghosting. Then repeat with the custom UC8279D panel:
    - The log (and P1) show `Panel controller UC8279 (probed)` with its VER, FLG and LUT_VER; a GDEQ0426T82 shows `SSD1677` with no UltraChip answer. After a sleep and a wake, P1 shows the same verdict kept from the probe before the sleep.
    - The UC8279 driver's waveforms were tuned for Xteink panels: record the LUT_VER and the MTP header P1 logs, and judge T4/T5 on this panel before tuning anything. It also checks that the panel's charge pump, sized to the SSD1677 reference, runs this controller cleanly.
    - A UC8279 driven as an SSD1677 (a wrong verdict) shows BUSY waits that end at once or time out, and a blank or garbled panel; the SSD1677 soft reset `0x12` is a UC refresh command. If the probe misbehaves on a unit, force the controller with `display_flags` and report the log.
4. **B3 - Sleep and wake.** Sleep and wake from `IO18` repeatedly. No false wakes over 30 minutes. In sleep: `IO47` ≈ 3.3 V (RST not held LOW), `IO14` 3.3 V, `IO12`/`IO13`/`IO21` 0 V, `IO48` ≈ 3.3 V with an SSD1677 panel (`BUSY`, no pull; with the UC8279D panel note the level, which is not in hand), `IO11` 3.3 V (TP_RST HIGH), `IO38`/`IO39` 3.3 V, `IO42`/`IO40` 0 V (including after sleeping with the light on, warm and cool), `IO5`-`IO7`/`IO15`-`IO17` 0 V and `IO10` 3.3 V. Scope `IO47` across a ghost wake: no LOW excursion. `esptool chip_id` works after wake and after an SW11 reset. On a current logger with a release build (the dev env adds a 250 ms USB-serial wait), a short tap (ghost wake) stays awake for about 0.3 s or less, with and without a card. Tape the power button down and trigger a sleep: the current falls to the floor + ~30 µA within ~5 s, and the release gives one short wake before sleeping again.
5. **B4 - SD card.** Scope `SD_VDD` at cold boot: below 0.5 V for at least 20 ms with the bus LOW before power-on. Mount within 3 attempts. Copy 200 MB and verify SHA-256. With no card, a clean "SD card error" and the bus left parked. In sleep, `SD_VDD` < 0.5 V and no bus pin HIGH. USB Drive mode in and out.
6. **B5 - Buttons.** T3's boot-checks row shows both ladders fitted; P1 shows their readings with each pull (a fitted ladder reads above about 2.2 V pulled down). Build with `-DFREEINK_ADC_LADDER_TRACE=1` and log mV for every key on USB and on battery. Tune the windows, especially BACK, UP(2) and the released threshold, which sit near the ADC's 2.9 V ceiling. 50 presses each give 50 events, including with Wi-Fi on. Chords decode as §9.1 says or are ignored.
7. **B6 - Battery and USB_STAT.** Log `IO9`/`IO8` in each state: on battery (~2.20 V), charging (~1.11 V), complete (~0.55 V), charger idle (~3.3 V), and cell pulled on USB (0.55/0.41 V blink must never report "charged"). The on-battery level is unverified on real parts (DESIGN_REVIEW), so re-window if needed. Ramp a supply on `J5`: the device sleeps at ~3.40 V with Wi-Fi off, and at ~3.30 V as read on `IO8` with the web server running. Log `IO8` with Wi-Fi on and off at one supply voltage to check the 100 mV allowance. Below 3.6 V on battery, OTA and SD flashing refuse. Pull the USB cable during USB Drive on battery: the session ends within ~5 s and the device returns to Home. Leave the web-server screen idle on battery: it sleeps after 15 min.
8. **B7 - RTC.** Probe 0x68: T13 shows the identity check passed and the dump matching the DS3231 pattern. OSF is set after a cell insert and cleared once time is set over NTP. Register `0x0F` bit 3 = 0. Check I2C edges at 400 kHz before raising the bus from 100 kHz.
9. **B8 - Front light** (lit builds only, load connected first). Scope `IO42`: at least 3 ms LOW, then about 150 µs HIGH, then the 25 kHz carrier. COLOR_SEL changes only with ADIM LOW (at least 3 ms after it fell). `LED_MONIT` × 9.33 ≈ 15 V lit. Force a trip with the test firmware's T10, which lowers the trip level in RAM for one run and also exercises the switch-on probe, or by building with a temporarily lower `SILKSCREEN_FRONTLIGHT_TRIP_MV` (for example 12000), rather than by unplugging the load while lit: the light goes off, the log shows the trip, and it stays off until switched on again, including across a sleep and wake and a Wi-Fi screen's restart. With no light connected (`J3` unplugged, `J6` empty: a safe open-load enable), switching the light on logs `load probe: ... no load` for each string, shows "No front light detected", and leaves `IO42` LOW; scope `LED_MONIT` against the enable to see the trip crossing fall inside the 60 ms window. For the restore path, leave the light on with Restore Light on Wake on, sleep, unplug the light, and wake: the restore tries once and finds no light (logged, no message); the next wake does not enable the boost (`Frontlight not restored`), and a power cycle tries once again. Wait 10 s after an open-load enable before connecting a light. A cool-only `J6` strip at the default warmth lights cool after the rail wait, and the warmth row says "Cool light only". A ghost wake (short tap) must not light the LEDs. Scope `IO42`/`IO40` across a silent restart with the light on warm: `ADIM` falls at least 3 ms before `COLOR_SEL`. With no touch tail, Settings > Display > Frontlight sets brightness and warm/cool from the keys.
10. **B9 - Touch** (-T01C/-FT01C only). An I2C scan shows 0x38 (FT6336U) next to the RTC, and T11 shows its chip id (expect 0x64) accepted. A GT911 tail (0x5D/0x14) needs the alternate jumpers and a build with `-DSILKSCREEN_TOUCH_ALT_JUMPERS=1`. Build with `-DTOUCH_PROBE_DEBUG` and log the probe (controller, address, cached result); a cold boot without the tail must boot touchless, with the keys, hints and remap. Map the corners and set `swapXY`/`flipX`/`flipY` and the raw ranges (the test firmware's T11 fits and logs them; T12 checks the sleep command). The keys still work with touch fitted. Sleep with the tail fitted:
    - `TP_RST` (`IO11`) is held HIGH (3.3 V), never LOW.
    - `IO41` is unheld for an FT6336U, and 0 V for a GT911.
    - The controller is hibernated: on a logic analyzer at sleep entry, the `0xA5 = 0x03` write is ACKed (or NACKed, then a reset and an ACK).
    - Current into `J4` pin 2 is about 55 µA (hibernate), against 220 µA in Monitor and ~1.1 mA with `TP_RST` LOW. Log it for a few minutes to catch a wake-up on the floating `IO41`.

    Unplug the tail while asleep and check that a cold boot finds it absent.
11. **B10 - Sleep current.** Follow [POWER.md §4](POWER.md#4-measurement-and-bench-checks) (MEAS-01) with a release build: a µA meter or logger in series with the cell at 3.8 V, USB and UART adapters off, card fitted, light off, panel asleep. Average 60 s, starting 60 s after sleep entry.
    - Expect ≈58-73 µA without touch and ≈113-130 µA with an FT6336U hibernated.
    - Above ≈150 µA, work through the A/B list in POWER.md. It includes the J2-flex / R5 test for GPIO47/48 back-feed and the `espefuse` summary.
    - Repeat after each sleep path: menu, auto-sleep, ghost wake, release wake, the 15-min ceiling, the loop and boot cutoffs, and 3 forced panics in a row (a test build). All should agree within about 2 µA.
    - On battery, also check that `D2` is dark and `USB_VBUS` ≈ 0 V.

### Checks that need the reader image

The test firmware pins its settings in RAM, never writes `/.crosspoint`, and has no forced-panic (P6) or battery-ramp (P7) test, so these checks run on the reader image. The full list, with the bench-only checks (scope, meter, esptool), is in [TEST-SUITE.md](TEST-SUITE.md#checks-that-need-the-reader-image).

- B3: a click wake (Short Power Button = Sleep, the `WAKE_POLICY_CLICK` early decision) and Quick Resume / the splash-less wake (`sleep_frame.bin`).
- B8: Restore Light on Wake; the "No front light detected" message; a restore that found no light (light unplugged while asleep) not repeated at the next wake, and tried once after a power cycle; a single-colour strip's warmth row; a ghost wake must not light the LEDs; a light trip stays off across a sleep, a wake and a Wi-Fi screen's restart; IO42/IO40 at 0 V after sleeping with the light on, warm and cool (ZD-08); ADIM falls at least 3 ms before COLOR_SEL across a silent restart with the light on.
- B4, B10: the SD-card error screen with no card, the bus left parked, and its 60 s sleep on battery.
- B1, B3: the power-button fault screen (R72 fitted next to R36, as a rework only): it stays up on USB and sleeps on the cell with no wake source, and SW11 starts the board again.
- B6: the supply ramp on J5 (sleep at 3.40 V, 3.30 V with the web server; resting P+ at least 3.5 V after 10 min; behaviour in LDO dropout), HW-08's 5-min cross-check with `USB_STAT` held in the charging window, `IO8` under the web server's load, the 15-min ceiling on the web-server screen, and OTA / SD update refusing below 3.6 V.
- B10: 3 forced panics in a row (WK-03) end at the sleep floor, on both panels.
- B2: closing the reader's toolbar overlay re-renders an anti-aliased page with its gray levels, on both panels.

## Commit plan

SDK (`freeink-sdk`, branch `silkscreen`). All eight steps and the battery-drain pass have landed; see [Status](#status) for the commits (steps 2-5 share one):

1. Register Silkscreen: device flag, enum, derived capability lists and the `SILKSCREEN` profile with the verified pin map.
2. Boot/sleep pin safety: `parkedPins`, `powerExternalPull`, `busyPullDown`.
3. SDMMC: configurable clock and pull-ups; parked power-cycle mount; park on shutdown and at boot.
4. Data-driven ADC button ladders, with host tests.
5. Analog charge-status ladder and low-battery cutoff API.
6. Frontlight: colour-select, enable pulse, LED over-voltage monitor, and the shared ADC lock.
7. Touch optional/fallback, touch sleep, keys alongside touch; one owner for the I2C bus clock, bus recovery and release. (DS3231 low-power and the RTC fallback landed earlier.)
8. README: Silkscreen in the device list and every new defaulted field in the new-board recipe.

App (this repo, branch `silkscreen`). All three steps, the front light, touch and the battery-drain pass have landed:

1. `.gitmodules` to the fork; `silkscreen` / `silkscreen-gh_release` / `silkscreen-gh_release_rc` envs; OTA board tag; keep-awake after a USB cold boot.
2. USB presence from the charge ladder in `HalGPIO::isUsbConnected()`; low-battery cutoff in the main loop and at boot.
3. CI: build the `silkscreen` env (`.github/workflows/ci.yml` build matrix, artifact `firmware-silkscreen.bin`). The release workflows and `OtaUpdater` are unchanged. The CI checkout needs the SDK's `silkscreen` branch pushed to the fork.

Deferred until measured on hardware: the touch orientation (B9); wake from ladder keys; brown-out level; 40 MHz SDMMC/SPI.

### Notes for steps 6 and 7 (pinned core: Arduino-ESP32 3.3.11)

- **Front light (as built).** LEDC stays attached while awake; the pad moves between the plain GPIO output register (dark, and the enable pulse) and the LEDC signal through the GPIO matrix (`gpio_set_direction`, then `ledcOutputInvert(pin, false)`, which also clears the stale `GPIO_OUT` bit so a later detach or `pinMode` lands LOW). None of these touch a pull. `ledcDetach()` (sleep handover only) routes the pad back to `GPIO_OUT` and frees the channel and timer: no pin reset, no pull change. Arduino 3.x clocks LEDC from the 40 MHz XTAL, so 25 kHz allows 10 bits (1600 ≥ 1024), not 11; a `static_assert` in the profile checks it. The monitor runs on the esp_timer task (8 KB stack) and skips a tick rather than block while a light transition holds the front-light lock.
- **Touch (as built).** The touch `begin` paths run from `gpio.begin()`, before `halClock.begin()`, and Wire ignores a second master `begin`, so the first driver to start the bus sets its clock. Every touch path and every sensor driver now starts the bus through `freeink::i2c::beginBus()` at `busHz()`: the sensor-bus clock when the pins match, otherwise the path's old rate. Every existing board with touch on its sensor bus (X4 Pro, LilyGo, PaperS3, EEGO A4, Paper Mono) already had equal clocks, so none changes. The stuck-bus check runs only on the first start of an `i2cBusRecovery` bus and never under a running controller (Arduino's pin manager is checked first). The probe result sits in `RTC_DATA_ATTR` with a magic word and is trusted only when the reset reason is a deep-sleep wake. `HalPowerManager::startDeepSleep()` runs `gpio.prepareForSleep()` and `releaseSensorBusForSleep()` first, before the serial teardown, so every sleep path gets them and their logs reach the host. A present FT6336U adds the reset and settle time (~307 ms) to each boot, and to a sleep entry whose hibernate write is NACKed. With `earlyWakeDecision` the probe runs after the wake decision (`gpio.begin(true)`, then `gpio.startTouch()`), so it no longer lengthens the hold a wake needs or a ghost wake's awake time. A sleep entry before the probe sends an FT6336U nothing after a deep-sleep wake (it is as the last sleep entry left it) and one best-effort hibernate write after any other reset; a GT911 cached by the probe gets its sleep command or its `INT` hold again. The abnormal-reset sleep resets an FT6336U and sends hibernate once.

## Next

Queued work, in this order.

1. **Port CrossInk.** [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk) is an MIT-licensed fork of crosspoint-reader, also built on freeink-sdk.
   - Its board support should come from the same SDK `silkscreen` branch; the board stays data, with no device checks.
   - The app side to carry over is what this branch changed against `d1509d07`:
     - `src/main.cpp`: the boot order (release wake, reset loop with the driver-free panel and touch sleep, early wake decision, boot cutoff), the loop cutoff, the awake ceiling and input cap, the wake and SD-error timeouts, the light-trip handling, and `Frontlight.begin()` after the wake decision.
     - `lib/hal`: `HalPowerManager`, `HalGPIO`, `HalFrontlight`, `HalClock`.
     - The `hasFrontButtons()` UI sites.
     - `OtaUpdateActivity`, `ClockSyncActivity`, `FontDownloadActivity`, `KOReaderAuthActivity`, `KOReaderSyncActivity`, `ClearCacheActivity`, `SdFirmwareUpdateActivity`, `UsbDriveActivity`, the cutoff checks in the upload and download callbacks, the Frontlight row in `SettingsActivity`, and the traffic hook in `CrossPointWebServer`.
     - `FirmwareBoardTag`, the `[silkscreen]` envs in `platformio.ini`, `STR_BATTERY_TOO_LOW_FOR_UPDATE`, and `scripts/check_sleep_wake_sources.sh`.
   - Re-check every rule in [POWER.md §3](POWER.md#3-firmware-rules-and-where-they-live) against CrossInk's own sleep paths and network screens.
2. **Hardware test-suite firmware.** A separate build that boots into discrete tests, each logging its raw readings and a result:
   - battery (`BAT_MONIT`) and charge status (`USB_STAT` states);
   - every key, including power (ladder mV and decoded key);
   - the e-ink refresh modes (full, half, fast, grayscale);
   - the front light: warm, cool and brightness steps, with the `LED_MONIT` reading;
   - touch: probe, corners and the sleep command;
   - the RTC: probe, EN32KHZ, and time kept across a sleep;
   - Wi-Fi, checked by an NTP sync.

   It covers the firmware side of the bring-up checklist (B1-B10) and uses the same SDK managers as the reader, so a failure there points at the board rather than the reader.

   Built as `[env:silkscreen-test]`, not yet run on a board: the menu, the result log, the safety rules and every test of the plan: system info, I2C scan, battery, charge status, USB, keys, the display tests T4-T8 (B2), the front light T9-T10 (B8), touch T11-T12 (B9), the RTC T13-T14 and Wi-Fi/NTP T15 (B7), the SD card P3 (B4), the sleep paths and current P5 (B3, B10) and the reader-data check P8. Forced panics (P6) and the low-battery ramp (P7) were left out; they run on the reader image with a bench supply. See [TEST-SUITE.md](TEST-SUITE.md) and the table at the head of the [bring-up checklist](#bring-up-checklist-real-board).
3. **Research: Silkscreen modularity against the freeink-sdk ecosystem.** Which Silkscreen blocks the SDK's board-data model can describe as optional or swappable:
   - the J3 panel light or a J6 strip;
   - the J4 touch tail;
   - U13 or U14 RTC;
   - the UP(2)-as-power variant;
   - J6 accessories.

   Also: where runtime probing (as for touch) beats a build flag, and what a future board revision should change to fit the other freeink-sdk boards ([POWER.md §6](POWER.md#6-hardware-notes-for-a-future-revision)).

   **Partly done 2026-09-30.** The runtime checks it recommended have landed: the panel controller probe, the front-light load probe at switch-on, key-ladder presence, the power button's idle level, the touch and RTC identity checks, and the GT911 fallback as an alternate-jumper build option. Still open: one env and board tag per panel family, the OTA source, and designated initializers for the `SILKSCREEN` profile before the next upstream SDK sync.
