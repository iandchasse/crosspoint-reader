# Silkscreen modularity

Silkscreen is a base board meant to be built many ways: with or without a front light, a touch tail, an RTC, some of its keys or a `J6` accessory, cut short, and behind different panels. This file answers the modularity item of the port plan ([PORTING.md, Next](PORTING.md#next)). It covers:

- which of those variations the firmware has to know about;
- how it learns each one;
- how that fits the freeink-sdk board model;
- what is left to do.

It records the conclusion of the modularity research of 2026-09-30, which was a synthesis checked by an independent verification pass, as amended by the owner's decisions. It also gives the state after the work that research led to.

- **Status:** SDK `3ee11bc`, app `16374791`, with the follow-up review's fixes (SDK `923f61d`, app `fix(silkscreen): address follow-up review`) and the hardware audit's (SDK `65660a6`, app `f25a8651`). **Nothing here has run on a board.** Every probe threshold is derived from datasheets and the netlist, and waits for the [bring-up checklist](PORTING.md#bring-up-checklist-real-board).
- **The 3.68" variant** ([below](#the-368-variant-silkscreen-368), 2026-10-11): SDK `c875022` to `48be3d0`, app `9385762a` to `4d9bb804` and the docs commit after it. Built and checked without hardware; its bench list is in [TEST-SUITE.md](TEST-SUITE.md#the-368-variant-silkscreen-368-test).
- `§` refers to `de-link_pcb/docs/HARDWARE.md`. SDK paths are relative to `freeink-sdk/libs/`.
- The battery cost of every check is in [Battery cost of the checks](#battery-cost-of-the-checks); the drain rules they keep to are in [POWER.md](POWER.md).

## Conclusion: four tiers

Every Silkscreen variation falls into one of four tiers. Only the first is a build choice.

| Tier | How the firmware learns it | Variations |
|---|---|---|
| 1. Panel family | A separate build env, `BoardProfile` and board tag | 4.26" (800×480) in `[env:silkscreen]`. The 3.68" x3 panel (792×528 native, 528×792 portrait) in `[env:silkscreen-368]`. Later: the 5.76" panel (SSD2677, 920×680). |
| 2. Probed at runtime | Read at a reset by a check that is harmless to the part: a reset pulse and register reads, or a pin read with a pull for a few ms. The result patches `BoardConfig::ACTIVE` before a driver uses it. Where a wake needs it, it is kept in RTC memory, for deep-sleep wakes only. | The display controller (SSD1677 or UC8279D). The touch tail (FT6336U, identified by its chip id; a GT911 only in an alternate-jumper build). The RTC (U13 DS3231MZ, identified by its register pattern; U14 RV-8263; or none). Whether each key ladder has its pull-up. As a fault check, the power button's idle level. |
| 3. Detected on a user action | Seen only by running the part, so detected inside an enable the user asks for (or Restore Light on Wake, or a silent restart's replay of a lit light), never on its own. The result is kept in RTC memory. | The front-light load (a `J3` light, a `J6` strip, or none) and each of its strings (warm, cool). |
| 4. Invisible | The firmware cannot see it and does not need to. | Individual keys; front-mounted keys; UP(2)-as-power; the `J4` jumper mux; `R37` and the U10 variant; the battery; the SD socket. |

### Why the panel family is a build env

- **The SDK's rule.** freeink-sdk gives a different screen or controller family its own profile, device flag and env. Two devices may share a binary only when they can be told apart at runtime (SDK README, "Adding a new device", step 3).
- **Panel families cannot be told apart reliably.** Geometry is not read back from the panel. UltraChip parts answer the VER read, and a UC81xx MTP dump carries its programmed resolution. But an SSD1677 and a UC8253 both read a floating `0xFF`, and nothing tells an SSD1677 from an SSD2677.
- **The display must work first.** Every screen, a setup screen included, needs the right driver before it can show anything.
- **Firmware updates.** A board tag per panel family lets OTA and the SD firmware update refuse an image built for another panel.

The SILKSCREEN profile's 48,000-byte `static_assert` is not what forces this. The framebuffer is allocated at runtime and sized by `MAX_FRAMEBUFFER_BYTES` (the largest compiled-in profile). A second profile needs a term there, and the way in. The 3.68" variant took the narrow one: a profile derived from `SILKSCREEN` by a `constexpr` function (so the pins, power policy, ladders, light, SD and RTC cannot drift), picked by a build macro, and the same `Board::Silkscreen`, so no enum value, no `selectDevice()` case and no app site that tests the board changes.

### Why probes, kept in RTC memory rather than NVS

A modular board's parts get swapped, panels included, so a stored answer goes stale. For the same reason, the SDK reads the Xteink OEM `hw_calib` value only for its log on every board but the X4 Classic: a reflash from another unit can name the wrong panel.

- **Cheap.** Each probe is cheap (see [Battery cost of the checks](#battery-cost-of-the-checks)).
- **When they run.** The display controller and ladder probes and the `IO18` check run at a reset other than a deep-sleep wake, and a deep-sleep wake reuses the controller and ladder results from RTC memory (the controller's after one `BUSY` read at the panel's own reset). Two exceptions: a touch tail found before is reset and its id read again at a wake (≈307 ms), and the RTC is identified at every boot past the wake decision. The costs are in [Battery cost of the checks](#battery-cost-of-the-checks).
- **Swapped parts.** A part swapped with the power off is found at the next boot. Deep sleep is not "power off": 3V3 stays live through it, so swap parts with the cell and USB disconnected. A panel swapped during a sleep anyway is caught at the wake's first panel reset (`BUSY` at the wrong idle level drops the cached controller and restarts, which probes), and a UC `BUSY_N` wait gives up after 60 s.
- **Undecided results.** A probe that cannot decide is not kept, so the next boot tries again.

The RTC needs no cache: it is found by address at every boot that gets past the wake decision.

### Why the front light waits for the user

No passive reading shows whether a light is on `J3` or `J6`: `LED_MONIT` sees a load only while the boost runs (§7, §13.1). An enable with no load is safe on the standard build. U10 climbs to its OVP and latches off, and `D3` keeps margin to its breakdown (§13.1). It is still avoidable.

- **When the boost runs.** Only when the user switches the light on or changes its colour while lit, or when Restore Light on Wake re-enables it; a silent restart also replays a light that was already on, gated by `restoreAllowed()` like the restore. Every such enable is the probe.
- **No probing on its own.** Nothing probes at a cold boot or a wake.
- **Decision.** The research had proposed declaring the light once in NVS, with "none" as the default. The owner chose detection on use instead: it needs no setup screen and cannot go stale.

## Module table

Status after this round (SDK `3ee11bc`, app `16374791`, the follow-up review and the hardware audit). "B" steps are the [bring-up checklist](PORTING.md#bring-up-checklist-real-board).

| Module | Variants | Tier and mechanism | Status | Left to do |
|---|---|---|---|---|
| Panel family | 4.26" GDEQ0426T82 family and the custom 4.26" panel; 3.68" x3; 5.76" GDEH0576T81 | 1. Env `silkscreen`, profile `SILKSCREEN`, board tag `silkscreen` (`silkscreen-test` for the test firmware). The 3.68": env `silkscreen-368`, `-DSILKSCREEN_PANEL_368=1`, profile `SILKSCREEN_368`, board tag `silkscreen-368` (`silkscreen-368-test`). | 4.26" done; 3.68" built and checked, nothing run on a board | The 3.68"'s [bench list](TEST-SUITE.md#the-368-variant-silkscreen-368-test); the 5.76" env ([Future work](#future-work)) |
| Panel suffix within 4.26" | Plain, -T01C (touch), -FL01C (light), -FT01C (both) | None: the suffix changes only touch and light, which are handled below. | Done | — |
| Display controller | SSD1677 (Good Display panels) or UC8279D (the custom panel), same 800×480 and orientation | 2. A half-duplex VER/FLG read on SDI (`IO12` via `R29`) before `display.begin()`. Any UltraChip answer runs the UC8279 driver, a FLG driven the same on both passes included; UC8179 is not used on this panel size and is not linked. 16 B RTC cache, checked against `BUSY` at the wake's first panel reset. `-DSILKSCREEN_DISPLAY_CONTROLLER` override. | Done (SDK `0c34a6d`, follow-up review) | B2 on the UC8279D panel: verdict and IDs, geometry (TRES, gate offset), `BUSY_N`, deep-sleep current, waveforms |
| Case window | Every measured case (4.26" Rev K/L and P03, 3.68", 5.76"): the active area plus 0.5-0.7 mm | Profile constant `viewableInsets {0,0,0,0}`. | Done | T7 against the case |
| Display controller, 3.68" variant | UC8253 (the first X3 batches) or UC8279d (later ones), same 792×528 | 2. The X3's stock protocol, before `display.begin()`: reset (high 10 ms, low 50 ms, high 50 ms), a bounded `BUSY_N` wait (300 ms), then three VER bytes bit-banged on SDI (`IO12` through `R29`, released with the internal pull-up): byte 2 `0x66` is a UC8279d, `0xFF` a UC8253. No FLG, no MTP, no generic probe (it would take a UC8253 for an UltraChip part, and is not linked). Only those two ids are kept in RTC memory for a deep-sleep wake; any other id runs the UC8253 driver and is probed again at every wake. `-DSILKSCREEN_DISPLAY_CONTROLLER=uc8253` or `=uc8279` (`[silkscreen368] display_flags`) forces it. | Built (SDK `757cf9f`, `4fa3c1f`) | The VER bytes of the real panel (P1's `action=panel` row): see the bench list |
| Touch, 3.68" variant | None: no tail on `J4` (and `U7` unfitted) | 1. `-DFREEINK_CAP_TOUCH=0` and `NO_TOUCH` in the profile: no touch driver is linked and nothing probes `0x38`, `0x5D` or `0x14`, resets `TP_RST` or sends a hibernate. The keys-only path (the X4's) runs everywhere. `scripts/silkscreen/check_no_touch.sh` fails a build that links one. | Built and audited | A logic analyser at boot and at sleep entry |
| Touch tail on `J4` | None; FT6336U (-T01C, -FT01C); GT911 (alternate jumpers) | 2. Reset, then the chip id at `0xA3` (0x06, 0x36 or 0x64) is read before any write. The GT911 fallback is built only with `-DSILKSCREEN_TOUCH_ALT_JUMPERS=1`. RTC cache. | Done (SDK `a26e398`, `3ee11bc`) | B9: the chip id (0x64 expected), orientation, hibernate current |
| `J4` jumper mux | `R42`/`R44`/`R46`/`R52` (default) or `R43`/`R45`/`R58`/`R66` | 4. It only routes connector pins. A GT911 tail, which needs the alternate jumpers, also needs the alternate-jumper build: a default image resets it awake and never sleeps it (about 3.3 mA in sleep). Fit exactly one jumper of each pair (§9.2); firmware cannot see a pair fitted twice. | n/a | — |
| RTC | U13 DS3231MZ at 0x68; U14 RV-8263-C7 at 0x51; none | 2. 0x68 must show the DS3231 register pattern before anything is written to it; then 0x51 is tried; with neither, the board has no RTC. | Done (SDK `abca595`, `3ee11bc`) | B7. 0x51 has no identity check ([J6 accessories](#j6-accessories)) |
| Key ladders | Both fitted; one built without its pull-up (`R4`/`C27` or `R28`/`C28` off, with no keys on that ladder) | 2. Pull-down, then pull-up readings at a reset other than a deep-sleep wake. A pin that follows both pulls switches its ladder off (`ACTIVE` pin unassigned). 5 B RTC cache. | Done (SDK `3ee11bc`) | B5: measure the floating readings (the 400/2600 mV thresholds are derived) |
| Individual keys | Any subset fitted; `SW2`/`SW3`/`SW8`/`SW9` front-mounted (MJTP1243, §9.1.1) | 4. An unfitted key reads released; front-mounted keys use the same ladder values. | n/a | Hints for keys that are not fitted stay (cosmetic) |
| Power key | `SW10`; UP(2)-as-power (`SW7` through `R72`/`R74`) | 4. Both drive `IO18` HIGH through 10 kΩ (about 3.0 V against `R76`). A half-done rework (`R72` with `R36` still fitted) is caught by the `IO18` idle check. | Done (SDK `3ee11bc`) | B1: `IO18` read at boot; B5: T3 with the test firmware's OPT "UP(2) is POWER" |
| Front-light driver group | `U10` and its parts fitted (the standard BOM) or left off | Compiled in by default. `[silkscreen] frontlight_flags =` in `platformio.local.ini` drops it, and `IO42`/`IO40` stay parked LOW. With the group left off, `LED_MONIT` floats and a switch-on reports whatever it reads (cosmetic), so drop it from the build. | Done | — |
| Front-light load | None; a `J3` bonded light (≈15 V); a `J6` strip (6-22 V) | 3. `LED_MONIT` read every 20 ms for 500 ms after each enable, on the monitor timer: the switch-on returns at once and decides within the window. Above 23 V, the string has no load and goes dark at once. If no string lights, the light is off. After a switch-on, a restore that found no light, a trip in use, or a second string the rail blocked, the reader says "No front light detected. Wait about 10 s before connecting a light." and keeps the control for a retry. 8 B RTC record: once a switch-on or a restore has found no light, later deep-sleep wakes do not enable the boost (a cold boot clears the record, so a restore may try once). | Done (SDK `b7b00c8`; the cadence `65660a6`) | B8: where the trip crossing falls in the 500 ms window, the `C9` decay, the `J6` near-trip margin |
| Light strings | Warm and cool; warm only; cool only | 3. A trip on the selected string tries the other one in the same switch-on. If that one lights, the first is recorded missing, a later no-light result included, until a cold boot; the reader saves the warmth on the string there is, so it is never selected again, a cold boot included, unless the user changes the warmth after a cold boot. The warmth row names the string there is. | Done (SDK `b7b00c8`, follow-up review) | B8 with a single-colour strip |
| `R37` / U10 part | `R37` 13.3-22 Ω; TPS923610, or TPS923611 with its SMAJ33A | 4. Brightness is a fraction of the `R37` ceiling. | n/a | — |
| `J6` accessories | None; I2C; GPIO; a light | Partly 2: the identity checks keep accessories at 0x38 and 0x68 from being written. Otherwise not modelled. | Done for 0x38 and 0x68 | [J6 accessories](#j6-accessories) |
| Cut board | Full length, or shortened at the cut line | 4. Needs the UP(2)-as-power rework to wake. The test firmware cannot see it either: the operator sets it in OPT. | Documented | [The cut board](#the-cut-board) |
| Battery, `R6`, SD socket, USB | Various | 4. | n/a | — |
| 5.76" extras | Page keys UP2/DOWN2; a LIS2DH12 (not fitted); a left-handed mount | Part of the 5.76" env. | Not started | [Future work](#future-work) |

## How it maps onto freeink-sdk

The SDK already had most of the model. The work added checks to it, not a new layer.

1. **One profile per panel family.** `BoardConfig.h` holds `SILKSCREEN` (`Board::Silkscreen`, `-DFREEINK_DEVICE_SILKSCREEN`). Each Silkscreen need is a defaulted profile field that generic code reads ([PORTING.md, Architecture](PORTING.md#architecture-silkscreen-is-data)), so every other board compiles and behaves as before.
2. **Capability flags compile the mechanisms in.** They are on for Silkscreen only, and their accessors fold to constants elsewhere, so the C3 build and the other S3 boards do not carry the code:
   - `FREEINK_CAP_DISPLAY_PROBE_POLICY`: the probe policy and `busyPullUp`;
   - `FREEINK_CAP_PROBE_IDENTITY`: the touch and RTC identity checks;
   - `FREEINK_CAP_ADC_LADDERS`: the ladders, and with `FREEINK_CAP_POWER_POLICY` (`ladderPresenceProbe`) their presence test;
   - `FREEINK_CAP_POWER_POLICY`: the power fields, the `IO18` idle check included;
   - `FREEINK_CAP_FRONTLIGHT_PROBE`: the switch-on probe, with `FREEINK_CAP_FRONTLIGHT`;
   - also `FREEINK_CAP_CHARGE_LADDER` and `FREEINK_CAP_I2C_RECOVERY`.
3. **Profile fields switch each check on.** `BoardProfile::displayProbe` (`DisplayProbeConfig`) and `DisplayPins::busyPullUp`; `TouchConfig::requireFocalTechId` and the `fallbackController`/`fallbackAddr` that `SILKSCREEN_TOUCH_ALT_JUMPERS` sets; `SensorsConfig::rtcIdentityCheck` and `rtcFallbackType`/`rtcFallbackAddr`; `PowerConfig::ladderPresenceProbe` and `powerPinIdleCheck`; `FrontlightConfig::loadProbeWindowMs`, `loadProbePeriodMs` and `railSafeV`. Every field and flag is listed in the SDK README ("Optional profile fields").
4. **Probes patch `ACTIVE`.** `BoardConfig::ACTIVE` is the mutable copy of the profile that drivers read, so a probe that rules a part out changes it before first use:

   | Check | Runs | Patches or records | Kept for a deep-sleep wake | Code |
   |---|---|---|---|---|
   | Power button idle level | Right after the release-wake check, at a reset other than a deep-sleep wake | `ACTIVE.input.power` → `PIN_UNASSIGNED` | Not needed: a pin switched off arms no wake, so the next boot is a reset | `hardware/PowerManager` `checkPowerPinIdle()` |
   | Key ladders | `InputManager::begin()` (`gpio.begin()`), at a reset other than a deep-sleep wake | `ACTIVE.adcLadders.ladders[i].pin` → `PIN_UNASSIGNED` | 5 B | `hardware/InputManager`, `ladderPresence()` |
   | Touch tail | `startTouch()`, after the wake decision | `ACTIVE.touch.controller` → `None` (or the GT911 fallback) | The controller found, or absent, or a refused device | `hardware/InputManager` touch probe |
   | RTC | `halClock.begin()`, at every boot past the wake decision | The driver's address (`Rtc::address()`), `Rtc::rejectedAddress()` | Not cached: probed every boot | `hardware/Rtc` |
   | Display controller | Before `display.begin()` at any boot other than a deep-sleep wake (`resolvePanelController()`); also before the boot cutoff's and the reset loop's panel sleep | `ACTIVE.displayController`, `displayControllerVariant` | 16 B, conclusive verdicts only; checked against `BUSY` at the wake's first panel reset | `hardware/XteinkDetect` `applyXteinkDisplayController()` |
   | Front-light load | Each enable the user asks for, Restore Light on Wake, and a silent restart's replay | The load record (`FrontlightManager::load()`, `stringMissing()`, `restoreAllowed()`) | 8 B, also across a software restart | `hardware/FrontlightManager` |

   The pure rules are header-only and host-tested: `hardware/BoardConfig/include/ProbeChecks.h` (ladder, chip id, DS3231 pattern, pin level) and `hardware/FrontlightManager/include/FrontlightLoad.h` (the load record).
5. **A build override for each probe a unit might get wrong.** These go in `platformio.local.ini`:
   - `[silkscreen] display_flags = -DSILKSCREEN_DISPLAY_CONTROLLER=uc8279` (or `ssd1677`) forces the controller and skips the probe;
   - `touch_flags = -DSILKSCREEN_TOUCH_ALT_JUMPERS=1` adds the GT911 fallback;
   - `frontlight_flags =` drops the light.

   The test firmware inherits all three.
6. **Shared by every fork.** All of this lives in the SDK's `silkscreen` branch, so the CrossPoint reader, the test firmware and a later CrossInk port behave the same. An app adds only UI:
   - The reader shows the "No front light detected" popup and the light panel's notes (`STR_FRONTLIGHT_*`).
   - It shows the power-button fault (`STR_POWER_BUTTON_FAULT`) and logs the panel controller.
   - It applies its X4 overlay policy on `displayProbe.x4ClassPanel`.
   - The test firmware reports each result ([TEST-SUITE.md](TEST-SUITE.md#variant-coverage)).

## Battery cost of the checks

Battery drain is the port's first constraint ([POWER.md](POWER.md)). None of these checks adds a wake source, a hold or a pull in sleep, and `scripts/check_sleep_wake_sources.sh` passes. Their awake cost:

| Check | When | Cost |
|---|---|---|
| Display controller | A boot other than a deep-sleep wake that goes on to start the panel (or the boot cutoff's and the reset loop's panel sleep) | ≈115 ms with an SSD1677, ≈170 ms with a UC8279 (both passes hold reset 50 ms). On a deep-sleep wake, one `BUSY` read after the driver's own reset; only a panel swapped during the sleep adds 300 ms and a restart that probes. The 3.68" variant's X3 probe: ≈111 ms (reset 110 ms, one `BUSY_N` read, three VER bytes), 410 ms at most when `BUSY_N` stays LOW through its 300 ms bound; nothing at a deep-sleep wake with a conclusive verdict (`0x66`, `0xFF`), and ≈111 ms at every wake with another id. |
| Power button idle level | A reset other than a deep-sleep wake | 15 ms (four 8-sample ADC2 readings, radio off) |
| Key ladders | A reset other than a deep-sleep wake | About 10 ms for both ladders. Each pull is on for about 2 ms, and both end off. |
| Touch tail | Each boot past the wake decision | ≈307 ms for the controller's reset and settle, plus a chip-id read: at a cold boot whether or not a tail is fitted, and at a deep-sleep wake when a tail was found before. Nothing on a deep-sleep wake when none was. Not in the 3.68" variant, which has no touch code: that is the ≈307 ms and the NACKed hibernate write at each sleep entry it saves. |
| RTC identity | Each boot past the wake decision | One 19-register read; a second read 20 ms later only on a mismatch. A read that gets no answer is repeated, three attempts 10 ms apart (upstream's `readRegsRetry()`), so a board with no RTC pays up to 30 ms per address (0x68, then 0x51) |
| Front-light load | Each switch-on the user asks for, and a restore | The caller does not wait: the monitor timer reads `LED_MONIT` every 20 ms for 500 ms (at most 25 readings, each under the ADC lock), then every 100 ms while lit as before. On a board with no light, one OVP event per string tried, and the rail wait between them on the same timer (≈1-3 s, at most 4 s) with the loop running; a restore no longer delays the first paint, and its message comes a few seconds after boot (once per cold boot, and once after a wake if the light was removed during the sleep). A fallback to the other string adds the rail wait (≈1-3 s) and a second window once: the reader then saves the warmth on the string there is. After a no-light result the light panel's slider and keys only set the level, so a gesture runs the probe once. |

A ghost wake (a short tap) and a release wake run none of the costly ones:

- A release wake sleeps before any of them.
- A ghost wake sleeps at the wake decision. The ladders keep the result from before the sleep, and the `IO18` check is skipped.
- Neither wake begins or probes the panel, probes touch, or begins the light.

## The cut board

The board may be shortened at its cut line (§16, cut-line row). The cut removes:

- `J6`;
- the power button `SW10`;
- `D3`/`D8`, the clamps on the front-light nets (§11);
- the mounting hole `H1`.

**A cut board needs the UP(2)-as-power option to wake** (§9.2): remove `R36` and `R73` first, then fit `R72` and `R74`, in that order.

- **After the rework.** `SW7` takes 3V3 (through `R74`) to `R72` 10 kΩ into `IO18`, against `R76`'s 100 kΩ: about 3.0 V pressed and 0 V released, exactly as `SW10` through `R62`. The firmware needs no change: the same ext1 wake, the same hold check.
- **What changes for the keys.** UP(2) leaves ladder 2. Its window never fires again, and UP(1) still gives Up.
- **Without the rework.** `IO18` sits at 0 V through `R76`. The firmware arms only `IO18`, so once asleep the board wakes only through the `SW11` reset. A wake on `SW1`/`SW2` or on a timer would need firmware support, and the port has neither (REQUIREMENTS `wake-ladder-ext1`).
- **A half-done rework is caught.** With `R72` fitted while `R36` is still in, `IO18` idles at ≈1.76 V. The boot check switches the pin off and the reader shows the power-button fault. Every sleep then arms no wake source, and `SW11` starts the board. The check runs at resets other than a deep-sleep wake. A boot that finds the button held through the 15 ms check (`pwr_idle=high`, "held at boot") cannot tell, and the fault is caught at the next reset with the button released.
- **What firmware cannot see.** `R73` and `R74` fitted together short 3V3 to GND. That is a hardware rule only.
- **The front light.** With `D3`/`D8` gone, an unloaded enable on a cut board relies on U10's own OVP latch alone. Whether that needs its own limit is a hardware question.
- **The test firmware.** The operator sets OPT's "Cut-down board", which turns "UP(2) is POWER" on too (BRING_UP §10). T3 then counts `SW7` as POWER on `IO18` and fails `up2_window` if UP(2)'s window still fires on `IO4`; P2 and T9 offer no `J6`; T10's step 0 says U10's OVP is the only limit and refuses the TPS923611 swap.

## J6 accessories

`J6` exposes the I2C bus, three strap GPIOs, the front-light nets and `P+` (§11). The firmware models none of it as a device. These are the interactions to know about:

- **0x38 (touch).** The address counts as touch only when `0xA3` reads a FocalTech chip id (0x06, 0x36 or 0x64). Any other device there gets no probe writes and no sleep command. Reading the id still writes its register pointer (`0xA3`) first, as any register read must. An accessory that happens to return one of those three bytes at `0xA3` would be taken for touch.
- **0x68 (RTC).** The address counts as the RTC only when registers `0x00`-`0x12` show the DS3231 pattern. A device without it gets no writes beyond the register pointer (`0x00`), and the log says `Device at 0x68 is not the RTC`.
- **0x51 has no identity check.** When nothing passes at 0x68, whatever answers at 0x51 is taken for the RV-8263. That happens with U14 fitted, with no RTC, or after a refused device at 0x68. The device then gets:
  - one read-modify-write of register `0x01` at boot (CLKOUT off);
  - the time writes after each NTP sync.

  On a board without U13, keep accessories off 0x51. A 24C-series EEPROM with A0 high sits there.
- **0x5D and 0x14** are probed only in an alternate-jumper build (the GT911 fallback).
- **Scans.** P2's scan is address-only.
- **A bus held LOW.** It is clocked free at the first bus start of a boot (9 `SCL` clocks, then a STOP). Before sleep the lines are released. An accessory that holds a line LOW through sleep costs ≈1.5 mA per line through `R47`/`R48`, and the firmware cannot fix that.
- **`IO3`, `IO45`, `IO46`.** The firmware never configures them. Their strap rules are the accessory's job (§11 rule 1).
- **A `J6` light.** It is found like a `J3` light, on the user's switch-on, and a single-colour strip keeps to its one string.
  - Its strings must be above 6 V and below 22 V (§11 rule 2). A string below about the battery voltage conducts through U10's body diode with the driver off, and no firmware can stop it.
  - After an open-load enable, wait about 10 s before connecting a light (§13.1).
  - A warm/cool pair with different forward voltages: a colour change between two lit strings moves `COLOR_SEL` 3 ms after dark, while `C9` still holds the first string's voltage, which then discharges into a lower-Vf string. A bench item (B8; [REQUIREMENTS.md](REQUIREMENTS.md), open questions).
  - A cold, top-bin 6+6 strip can sit near 22.3-22.9 V, close to the 23 V trip. It may read as "no load" or as a missing string, which is a bring-up item (B8).
- **`P+` on pin 12** is the raw cell behind a 0.75 A PPTC. That is hardware only.

## The 3.68" variant (`silkscreen-368`)

The owner's "x3 build": Silkscreen Rev 1.01 cut at its cut line, carrying the Xteink X3's 3.68" panel (792×528 native, 528×792 in portrait) on `J2`, a 500 mAh cell, no touch tail and, optionally, a light strip on `J3`. The panel's 24 pads follow `J2`'s order by position (the exact part is not identified in any repo), and its controller is a UC8253 or a UC8279d: reading VER is the first bench step. It is a build choice (tier 1), and the same device in every other respect.

| Part | What it is |
|---|---|
| Identity | `FREEINK_DEVICE_SILKSCREEN` and `Board::Silkscreen`, unchanged. The battery log, the 5% battery step, the front light, the early wake decision, the power policy and the key-access gating therefore apply as they are. |
| Panel | `-DSILKSCREEN_PANEL_368=1` in the SDK: the derived profile `SILKSCREEN_368` (792×528, UC8253 by default, 10 MHz, `BUSY_N` pulled up while awake, the pins of `SILKSCREEN`), the X3 pair `Uc8253X3Driver` and `Uc8279Driver` linked, the SSD1677 and UC8279 X4 drivers not, and the facade in X3 mode from its constructor (`FreeInkDisplay::isX3Mode()`). A build asserts the pins equal `SILKSCREEN`'s, the framebuffer is 52,272 bytes, and the build is touchless. |
| Touch | None, twice: `NO_TOUCH` in the profile and `-DFREEINK_CAP_TOUCH=0`, which compiles the driver out. The SDK refuses the macro with touch, with the alternate-jumper build or with a forced `ssd1677`. |
| App | `HalDisplay::isX3Panel()` is `isX3Mode()` in this build and `gpio.deviceIsX3()` in every other. The five `HalDisplay` sites (HALF resync, the gray base), the splashless-wake rebase, the last-screen sleep and the reader's `absoluteImageGrayscale` ask it, and the web server's `device` string reports `"X3"` from it, so the files page's "Auto" image target is the panel's 528×792. `xteinkClassPanel()` needed nothing: the profile inherits `x4ClassPanel`, which an SDK `static_assert` pins, and both X3 drivers report overlay gray. |
| Left as the board's | `gpio.deviceIsX3()` (the X3 board: C3 only), `isXteinkDevice()` (it also drives the `GPIO13` latch power-off) and `hasEdgeSideButtons()` stay false for Silkscreen. The keyboard's side-hint reserve is the X3 board's. The "Hardware detect" log line is not compiled for Silkscreen (the variant logs `Device: silkscreen_368`). |
| Envs and tags | `silkscreen-368`, `-gh_release`, `-gh_release_rc`, `-batlog` (tag `silkscreen-368`) and `-test` (tag `silkscreen-368-test`), from `[silkscreen368]`, which does not inherit `touch_flags`. OTA and the SD update refuse a 4.26" image, and the reverse. |
| Checks | `scripts/silkscreen/check_no_touch.sh` (no touch driver, none of the SSD1677, UC8279 X4 or generic probe code, the X3 pair linked, the tag), `check_test_firmware.sh silkscreen-368-test`, and `upstream_status.sh`'s "variant survived the merge" lens. The first two run in `build_matrix.sh` and in CI; `upstream_status.sh` is the sync runbook's step 0, and in neither. |

**Verified without hardware** (2026-10-11): the SDK's `static_assert`s and their 37-case driver `run_profile_asserts.py`; the host suites for the probe (the 0x66, 0xFF, unknown-id, cached-verdict and forced cases on the real detector), the X3 drivers' diagnostics and the driver-free sleep, which run under MSVC through the `c++` shim in `scripts/silkscreen/msvc/`; the five existing envs build to the byte (`silkscreen` with an identical symbol table); every 368 env builds and passes the audit; `check_sleep_wake_sources.sh` passes. **What the build cannot say**, and the bench list in [TEST-SUITE.md](TEST-SUITE.md#the-368-variant-silkscreen-368-test) takes in order: which controller the panel really is (read VER first), whether the SSD1677-reference boost (`L1` 47 µH, `R14` 2.2 Ω, `Q4`) drives this glass, the panel's deep-sleep current and its `BUSY_N` level in sleep, the 300 ms `BUSY_N` guard and the 60 ms driver-free settle, orientation (the X3 drivers cannot mirror; the flex is at the bottom), gray on a UC8279d ("pending hardware validation" in its LUT header), UP(2) as the power key on the cut board, and a logic analyser at boot and at sleep entry.

## Future work

- **3.68" x3 env: built 2026-10-11** ([above](#the-368-variant-silkscreen-368)). What is left is its [bench list](TEST-SUITE.md#the-368-variant-silkscreen-368-test), whose first-power gate holds the power-key rework and the panel's contact side, pin 1 and tail thickness against `J2`.
- **5.76" env.**
  - An SSD2677 driver (none exists) and a 920×680 profile (78,200 B per buffer).
  - Its `BUSY` polarity, pixel origin and a conservative SPI clock over the long flex.
  - A left-handed mount as a setting: a 180° rotation plus the key remap.
  - The page keys UP2/DOWN2, and later a LIS2DH12 `ImuType`.
  - Its wake keys: the spec makes DOWN1 and RIGHT wake keys, which needs the same ext0+ext1 decision as a cut board without the rework (the port arms `IO18` only).
- **A board tag per panel family before any release channel exists.** The 4.26" and 3.68" images have theirs (`silkscreen`, `silkscreen-368`, and `-test` for each: `src/network/FirmwareBoardTag.cpp`). The 5.76" needs `silkscreen-576`, so OTA and the SD update refuse a cross-panel image there too.
- **OTA pointed at the fork.** `src/network/OtaUpdater.cpp` queries upstream's releases. A Silkscreen build finds no `-silkscreen` asset there and reports no update. Point it at the fork's releases, or turn OTA off for Silkscreen, before releases exist.
- **An identity check at 0x51**, so an accessory there is not taken for the RV-8263.
- **Measure every threshold on hardware.** The checklist covers each one:
  - the floating ladder readings (B5);
  - `IO18` on ADC2 (B1);
  - the FT6336U chip id (B9);
  - the DS3231MZ pattern after its first power-up (B7);
  - the front light's trip crossing, `C9` decay and the `J6` trip margin (B8);
  - the UC8279D panel's geometry, `BUSY_N` and deep-sleep current (B2, B3, B10).
- **Hardware for a future revision:** [POWER.md §6](POWER.md#6-hardware-notes-for-a-future-revision).

## What the research proposed that was not taken

| Proposal | Outcome |
|---|---|
| An NVS "board declaration" for the light (none, `J3` pair, `J6` pair, warm only, cool only), with a settings screen and an SD-file import | Replaced by detection on use (owner decision). It needs no setup step, and a light added or removed later is found. |
| An NVS cache for the display controller | Refuted: a stored verdict outlives a panel swap or a reflash. The cache is in RTC memory and trusted after a deep-sleep wake only. |
| UC8179 as a 4.26" controller | Not on this panel size (owner decision). The driver is not linked into the Silkscreen build. |
| A passive `LED_MONIT` check of whether the `U10` group is fitted | Deferred: its thresholds need a bench session, and the switch-on probe already finds the load. |
| A `J6` strip short or bridge check (`LED_MONIT` below 1.30 V at full duty, at least 50 ms after the enable) | Deferred: its threshold needs a bench session with real strips. |
| `SILKSCREEN` moved to its own `SilkscreenBoard.h` | Deferred. The profile uses designated initializers, which is what keeps it intact when upstream appends a field. |
| A "no power key" setting with a ladder wake (ext0 plus ext1) for a cut board | Not taken. A cut board needs the UP(2)-as-power rework instead, and the firmware arms only `IO18`. |
