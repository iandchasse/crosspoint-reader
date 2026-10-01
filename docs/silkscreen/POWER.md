# Silkscreen power: sleep budget, pin states and drain rules

Silkscreen has no hardware off (§3.5): 3V3 is live whenever a cell or USB is present, so "off" is ESP32-S3 deep sleep and every awake minute comes out of the cell. This file gathers the port's battery-drain facts:

- the sleep-current budget;
- the safe sleep state of every pin;
- each firmware rule and where it is implemented;
- how to measure;
- what only a board revision can fix.

It comes from the battery-drain audit of 2026-09-30. That audit was a synthesis followed by an independent verification pass, and the verifier's corrections are applied here.

- **Nothing here has been measured on a board.**
- `§` refers to `de-link_pcb/docs/HARDWARE.md` ([iandchasse/silkscreen-pcb](https://github.com/iandchasse/silkscreen-pcb)).
- Tags: **[DS]** datasheet figure, **[calc]** Ohm's law on netlist values, **[est]** engineering estimate.
- Code paths: app paths are relative to this repo, SDK paths to `freeink-sdk/libs/`.
- Commit hashes name the commit that landed a change (app on `silkscreen`, SDK on the submodule's `silkscreen`).
- The boot-time probes and the front light's switch-on probe keep to these rules. What each one costs awake is in [MODULARITY.md](MODULARITY.md#battery-cost-of-the-checks).

## 1. Sleep-current budget

**Conditions**

- Battery side, with USB and any UART adapter on TP1/TP2 unplugged.
- Panel in its controller's deep sleep: SSD1677 `0x10`/`0x03` (Good Display GDEQ0426T82) or UC8279 DSLP `0x07`/`0xA5` (the custom UC8279D panel). Light off. SD gated off with a card fitted.
- U13 DS3231MZ fitted, U14 not fitted. J6 empty. Touch tail not connected (priced separately below).
- **typ**: 3.7 V cell at 25 °C.
- **max**: datasheet maxima, 4.2 V cell. This is **not** a worst case over temperature:
  - several maxima are specified at 25 °C only (panel 6 µA, DW01A 6 µA, DS3231M 3.0 µA);
  - Espressif publishes no deep-sleep maximum.

| # | Source (rail) | typ µA | max µA | Condition | Reference |
|---|---|---|---|---|---|
| 1 | U4 ESP32-S3-WROOM-1-N16R8 deep sleep (3V3) | 7 [DS] | 13, placeholder [est] | ext1 wake on IO18, RTC peripherals off, RTC memory on, VDD_SPI off, no ULP or timer | S3 DS v2.2 Table 5-10; IDF 5.5 sleep_modes.c:1282 |
| 2 | U4 pad leakage, pins held above 0 V | ~0 | 0.4 [DS] | 50 nA × about 8 pins | S3 DS v2.2 Table 5-4 |
| 3 | U3 TLV75533P IQ at no load (LDO_IN) | 25 [DS] | 33 [DS, 85 °C] | EN tied to IN | TI SBVS320D §5.5 |
| 4 | U3 extra ground current at the real 20-40 µA load | 0-15 [est] | unknown | Not characterized below 1 mA (≈145 µA at 1 mA). An adaptive-bias curve could add ≈20 µA at 30 µA. **Measure.** | SBVS320D Fig 5-17/5-18 |
| 5 | R81 + R82, Fix-4 detector (3V3) | 6.6 [calc] | 6.9 | Correct cell fitted | netlist |
| 6 | R79 + R80 across the raw cell | 3.4 [calc] | 3.8 | 3.7 / 4.2 V | netlist |
| 7 | R56 + R57, Q3 gate bias (raw cell) | 0.37 [calc] | 0.42 | Outside the protection FETs | netlist |
| 8 | U5 DW01A (clone) (P+) | 3.0 [DS] | 6.0 [DS] | FETs on | DW01A ICC; clone datasheet not obtained |
| 9 | U2 TPS2116 on VIN2 (P+) | 1.35 [DS] | 3.7 [DS] | VIN1 = 0 V | TI SLVSFG1A §6.5 |
| 10 | U11 TP4056 BAT pin, VCC = 0 (P+) | 1.0 [DS] | 2.0 [DS] | CE held at 3V3 by Fix-4 (no datasheet row for that) | TOPPOWER TP4056 p.5 |
| 11 | BAT_MONIT R12/R10 1M/1M (P+) | 1.85 [calc] | 2.1 | IO8 with no pulls | §3.6 |
| 12 | USB_STAT R70 1M + R17 2M (3V3) | 1.1 [calc] | 2.1 [calc] | On battery; max assumes the unpowered TP4056 status pins clamp | §3.7 |
| 13 | LED_MONIT R39/R41 1M/120k from LED_SW via U10's body diode (LDO_IN) | 2.9 [calc/est] | 3.4 | Light off; in §16 since its 2026-09-30 re-itemization | TI SNVSCN8A §7.3.3 |
| 14 | U10 TPS923610 shutdown (LDO_IN) | 0.13 [DS] | 0.5 [DS, 85 °C] | ADIM LOW for > 2.5 ms (IO42 held LOW) | SNVSCN8A §6.5 |
| 15 | U12 74LVC1G04 (clone) (3V3) | 0.1 [DS] | 4 [DS Nexperia]; 10 if TI's limit applies | Input at GND (IO40 LOW) | Nexperia Rev 18.1; TI SCES214AF |
| 16 | U13 DS3231MZ, VBAT-only (3V3) | 2.0 [DS] | 3.0 [DS] | EN32KHZ = 0, I2C idle; conversions already averaged in | Maxim DS3231M Rev 5 p.3 (Note 4) |
| 17 | GDEQ0426T82 panel in deep sleep (3V3) | 2 [DS] | 6 [DS] | 0x10/0x03 sent, RST HIGH. The custom UC8279D panel (DSLP `0x07`/`0xA5`) has no datasheet figure in hand: measure it (MEAS-01). | GDEQ0426T82 §7.1 |
| 18 | Q7 AO3401A leakage, SD gate off | ~0.01 [est] | 1 [DS, at −30 V] | IO10 HIGH (R77 carries the same current) | AO3401A DS |
| 19 | Q4 IRLML6346 leakage | ~0.01 [est] | 1 [DS, at 24 V] | EINK_SW at 3V3 | IRLML6346 DS |
| 20 | ESD/TVS parts (TPD4E1U06 ×5, CR2/CR3 SD05C, D3, CR1, D8) | 0.05 [est] | 2 [DS bounds] | SD05C clones rated 1 µA max at 5 V each | TI SLVSBQ9D; SD05C-01FTG Rev 1.4 |
| 21 | MLCC insulation leakage, always-on rails | 0.05 [est] | 0.5 [est] | Room temperature | Murata GRM21BR61E226ME44 |
| 22 | Idle pull networks (R4, R28, R5, R7, R13, R40, R47, R48, R62/R76, R38/R51), D2, R49/R50 | 0 | 0 | Nothing driven against them, no key held, USB absent. R49/R50 (1 MΩ LED bleeders) and Q6 draw nothing because each FT01C string is 5 LEDs (≈15 V Vf) against LED_SW ≈3.2 V | netlist |
| **Σ** | **Floor, no touch** | **≈58** (57.9) | **≈95** (94.8) | ≈91 (90.9) with the high-voltage leakage specs of rows 18-20 discounted; 58-73 typ and up to ≈110 max once row 4 is counted | rows re-added by the verifier from the netlist |

- 58 µA ≈ 1.4 mAh/day; 95 µA ≈ 2.3 mAh/day. The cell capacity is still an open question.
- **Not in the budget** (small, unquantified):
  - U10's SW-pin off-state leakage from LDO_IN (SNVSCN8A specifies shutdown current for VIN only);
  - the 3V3 → L1 → D5 → PREVGH/VGH path (C14 4.7 µF/50 V), live through sleep;
  - the rise of every leakage term at 60-85 °C.

### Scenarios on top of the floor

| Scenario | Adder µA | Total µA | In this firmware |
|---|---|---|---|
| FT6336U tail on J4, hibernated (`0xA5 = 0x03`), TP_RST HIGH | +55 typ [DS, at 2.8 V, no max] | ≈113 (≈2.7 mAh/day) | Every normal sleep entry with the tail fitted. The lowest possible while J4 VDD is unswitched (HW-02). |
| Tail in Monitor mode | +220 typ [DS] | ≈278 | A tail that missed the cold-boot probe and did not take the one best-effort write, until the next cold boot. The abnormal-reset sleep resets the controller and sends hibernate, and a Silkscreen build with `FREEINK_CAP_TOUCH=0` no longer compiles. |
| TP_RST held LOW with the tail fitted | ≈+1100 [est: 3.3 V into the ~3 kΩ RSTN pull-up] plus the reset-state current | ≈1150 (≈28 mAh/day) | This port's profile did this until freeink-sdk `a26e398`, which holds IO11 HIGH. |
| GPIO47/48 pads unpowered in sleep, so R5 and panel BUSY back-feed VDD_SPI | +100-300 [est]; the R5 path alone ≤ ≈280 | ≈160-360 | Open hardware risk (HW-01). Measure ([section 4](#4-measurement-and-bench-checks)). |
| Near-empty cell: LDO_IN below ≈3.3-3.35 V, U3 in dropout | IQ 25 → ≈190-225 at 25 °C, ≈260 at 85 °C [DS typ curve] | ≈250-300 | The reason for the 3.4 V cutoff (BAT-01). Unavoidable for a device stored empty. |
| Power button held or stuck | +30 [calc, R62 + R76] | ≈88 | Firmware sleeps after 5 s and wakes on the release (PB-01). |
| After a DW01A over-discharge trip | 2.5-6.5 from the raw cell | — | DW01A IPD ≤ 4 [DS] + R79/R80 + R56/R57 (HW-09). |
| EPD SCLK/MOSI/DC floating | 0 to tens [est] | — | Fixed: held LOW (freeink-sdk `983f2a7`). |
| DS3231 EN32KHZ left set | +0.1 at 25 °C, +0.8 at 85 °C [DS] | — | Does not occur here: `Rtc.cpp` `beginDevice()` clears it (freeink-sdk `abca595`). |

### Against HARDWARE.md §16

§16 was re-itemized on 2026-09-30 from this budget, part by part, and now agrees with it:

- ≈58 µA typical (≈56 µA without the panel) and ≈91-95 µA maximum, with the ESP32 at 7 µA typ for ext1. Every row above is in it, the LED_MONIT divider and the panel included.
- It states the touch-tail case (+55 µA hibernated, ≈220 µA in Monitor, ≈1.1 mA with TP_RST LOW) and the near-empty cell (LDO dropout).
- Like this budget, its maximum leaves out the LDO's load term (row 4) and the GPIO47/48 back-feed (HW-01), and names both.
- It has no row for the custom UC8279D panel, whose deep-sleep figure is not in hand (row 17).
- **Double counts to avoid** (both documents follow them):
  - §3.3's "≈7 µA incl. DW01A" overlaps the Fix-4 figure (R79/R80).
  - DS3231M conversions are already in its IBATT.
  - R77 and Q7 leakage are one current.
  - TPS2116 ISTBY,VIN1 does not apply on battery.

## 2. Pin states in sleep

**How the pads behave in deep sleep.**

- IO0-21 are VDD3P3_RTC pads; `gpio_hold_en` on them goes to the RTC hold.
- IO38-48 are digital pads. At deep-sleep entry ESP-IDF isolates every **non-held** digital pad (input and output off, pulls off). The exceptions are U0TXD/U0RXD and the MSPI pads, and it does this only while `gpio_deep_sleep_hold_en()` is set (IDF 5.5 `sleep_gpio.c:129-184`).
- ESP-IDF never isolates un-held RTC pads, and their sleep state is undocumented. So every RTC pad that matters is held.
- Digital-pad holds are dropped at a deep-sleep wake (IDF `gpio.h:401-403`). IO40, IO42 and IO47 are therefore undriven from reset until `holdPowerRails()`/`EpdBus`. R75, U10's ADIM pull-down and R5 keep their levels meanwhile. IO41 has no board pull: for a GT911 (alternate-jumper builds only) the firmware drives it LOW again as soon as `InputManager::begin(false)` runs, and re-holds it on every sleep entry before the probe.
- Costs below are at 3.3 V with the internal pulls at 45 kΩ typ.

| GPIO | Net | Safe in deep sleep | Forbidden (cost µA) | This firmware |
|---|---|---|---|---|
| IO0 | Boot strap, R13 10k to 3V3 | Untouched | LOW 330; pull-down 60 | Untouched |
| IO1 / IO4 | Key ladders (R4/R28 10k to 3V3) | Input, no pulls, not a wake source | Pull-down 60 each; LOW 330; HIGH ≈33 mA into a pressed 100 Ω key | `INPUT` (`InputManager::begin`), not held. A key held through sleep leaves the input buffer mid-rail (unquantified). The presence test at a reset other than a deep-sleep wake turns each pull on for about 2 ms, then off; a ladder found with no pull-up is switched off and its pad stays as the ADC left it (input and pulls off), so a floating node costs no input current. The awake idle poll takes one conversion per released ladder (`InputManager::adcLadderContact()`); the 16× decode runs only in the debounced poll. |
| IO2 | LED_MONIT (1M/120k) | ADC input, no pulls | Pull-up +17-20; HIGH +25; any pull corrupts the reading | Read awake only, through `SharedAdc.h`: every 100 ms while lit (`FrontlightManager::checkMonitor`), every 20 ms for 500 ms after each enable (the load probe, on the same timer), and every 20-100 ms after a trip until the rail is at 15 V (at most 4 s) |
| IO3 | Spare to J6.9 | J6 empty: optional hold LOW (low priority) | Re-derive if an accessory is fitted | Untouched (floating) |
| IO5-7, IO15-17 | SD bus (10k pull-ups to the **gated** SD_VDD; CLK has none) | LOW, pulls off, held | While gated: HIGH back-feeds SD_VDD (32 without a card, 200-2000 with one); pull-up 30 to ≈440 | Parked LOW and held (`PowerManager::powerDownRailsForSleep` → `parkSdmmcBus` + `gpio_hold_en`, SDK `983f2a7`); released at boot by `holdPowerRails()` |
| IO8 | BAT_MONIT (1M/1M from P+) | ADC input, no pulls | Any pull or drive +1.3-2 and ruins the reading | ADC only |
| IO9 | USB_STAT ladder | ADC input, no pulls | Pull-down +2.1; LOW +2.2; pull-up +0.5 | `chargeLadder` at ≤1 Hz; `batteryChargeStatus` unassigned, so no `INPUT_PULLUP` |
| IO10 | SD_ACTIVATE (active LOW; R40 100k to 3V3) | HIGH + hold | LOW 33 + card 200-2000; a pull-down turns Q7 on (Vgs −2.26 V) | `holdRailOff` HIGH, level preset before the output is enabled |
| IO11 | TP_RST (FT6336U RSTN has ~3 kΩ to IOVCC) | **HIGH + hold** | LOW with an FT6336U ≈1100 [est] and it resets out of hibernate; LOW with a GT911 330 | `parkedPins {11, High, High}` (SDK `a26e398`); LOW only as the ≥5 ms reset pulse (`InputManager::pulseTouchReset`) |
| IO12 / IO13 / IO21 | EPD MOSI / SCLK / DC | LOW + hold (0 µA) | Floating: undefined inputs on a powered SSD1677 | `parkedPins` LOW at boot and in sleep (SDK `983f2a7`) |
| IO14 | EPD CS (no pull-up) | HIGH + hold | Floating or LOW: panel selected | `parkedPins` HIGH at boot and in sleep |
| IO18 | Power button (R62 10k to 3V3 when pressed, R76 100k pull-down) | ext1 ANY_HIGH; input with no IO_MUX or RTC pulls | Pull-up 23 and released level 2.28 V (false wakes); pull-down drops the pressed level to 2.50 V against VIH 2.475 V | `preparePowerPinForSleep` (`powerExternalPull`); ANY_LOW only for a release wake (PB-01). Found idling between the logic levels at boot (R36 and R72 both fitted, ≈1.76 V), it arms nothing and is held with its input off: R28 + R36 + R72 against R76 then draw ≈18 µA, and only reset wakes the chip. |
| IO19 / IO20 | USB D− / D+ | Leave to USB | Repurposing loses the flashing path | USB-Serial-JTAG |
| IO38 / IO39 | I2C SDA / SCL (R48/R47 2.2k to 3V3) | Released, not held; R47/R48 keep them HIGH | Held or stuck LOW 1500 each; pull-down 70 each | `freeink::i2c::releaseSensorBusForSleep()` (Wire end, lines released, a stuck bus clocked free), SDK `a26e398` |
| IO40 | COLOR_SEL (R75 100k pull-down) | LOW + hold | HIGH 33 + U12; pull-up 23 + U12 shoot-through (up to several hundred [est]) | `FrontlightManager::prepareForSleep()`, then `parkedPins` LOW held. After a trip on the warm string it stays HIGH while awake until LED_SW is at 15 V (at most 4 s), so C9's charge cannot reach the cool string; `prepareForSleep()` waits for that too, then rests it LOW |
| IO41 | TP_INT (no board pull) | FT6336U: not held (isolated). GT911 (alternate-jumper builds only): held LOW after its sleep command | FT6336U: a LOW wakes it from hibernate (→ 220); GT911: floating or HIGH for 2-5 ms wakes it (3300) | Awake `INPUT_PULLUP` on the FT path; in sleep unheld for an FT6336U, held LOW for a GT911 (`InputManager::prepareTouchForSleep`). The hold ends at the wake, so a sleep entry before the probe re-sleeps a cached GT911 (ghost wake) or re-holds INT (release wake, `holdTouchIntForSleep()`). |
| IO42 | ADIM / enable (U10 600k pull-down) | LOW + hold | HIGH or pull-up: 260-300 IQ, ≈60 mA lit [est]; `gpio_reset_pin` turns the pull-up on | Duty 0 and LEDC detached (`FrontlightManager::prepareForSleep`), then `parkedPins` LOW held |
| IO43 / IO44 | UART TX / RX (TP2/TP1) | Defaults (not isolated by IDF) | An unpowered adapter left clipped on is back-fed by TX | Default; unplug adapters before measuring |
| IO45 / IO46 | Spare straps to J6 | Defaults, isolated | IO45 HIGH at reset selects 1.8 V VDD_SPI: boot fails, chip stays awake | Untouched |
| IO47 | EPD RST (R5 10k to 3V3); pad likely on VDD_SPI | HIGH + hold | LOW 330 and the panel leaves deep sleep; pull-down 60. If the pad is unpowered in sleep, R5 back-feeds VDD_SPI (HW-01) | `holdRailOff` HIGH; `EpdBus::begin` presets HIGH before releasing the hold (SDK `983f2a7`) |
| IO48 | EPD BUSY (SSD1677 drives it HIGH in deep sleep; a UC8279's BUSY_N level in deep sleep is not in hand); pad likely on VDD_SPI | **Not held**, no pulls | Pull-down held into sleep 73 (SSD1677); a pull-up held against a LOW BUSY_N the same; any drive: contention; unpowered pad back-feeds VDD_SPI (HW-01) | Awake only, by the probed controller: pull-down for an SSD1677, pull-up for a UC8279 (`busyPullUp`). `powerDownRailsForSleep` takes both off and leaves a plain input, never held (SDK `983f2a7`; the pull-up in the display-probe commit) |
| IO26-37 | Flash / octal PSRAM | Never touch | Reconfiguring breaks boot | Untouched |

## 3. Firmware rules and where they live

| Rule | Requirement | Status and location |
|---|---|---|
| ZD-01 | The only deep-sleep wake is ext1 ANY_HIGH on IO18. No timer, ULP, touch-pad, ext0 or GPIO wake, no `esp_sleep_pd_config`, no light sleep. (RTC_PERIPH on costs +1 µA; the ULP 170-190 µA.) | **Done.** SDK `PowerManager.cpp`: `armPowerButtonWakeup()` → `armWakeOnPins()` (the only `esp_sleep_enable_ext1_wakeup`), `deepSleep()` (the only `esp_deep_sleep_start`). Guard: `scripts/check_sleep_wake_sources.sh` (app `b86e9d49`), run by the CI clang-format job. It bans every `esp_sleep_pd_config`, so OPT-01 would need an exception. With IO18 found idling between the logic levels at boot (`powerPinIdleCheck`), the sleep arms no wake source at all: no wake storm, and the reset button starts the board. |
| ZD-02 | Every sleep path ends in `powerDownRailsForSleep` → `deepSleepUntilPowerButton` with `gpio_deep_sleep_hold_en()`, from a task with an internal-RAM stack. | **Done.** `lib/hal/HalPowerManager.cpp` `startDeepSleep()` / `startDeepSleepMinimal()` → `sleepImpl()` → SDK `powerDownRailsForSleep()` → `deepSleepUntilPowerButton()` → `deepSleep()`. Every caller runs on the Arduino loop task (`src/main.cpp`). IDF asserts on a PSRAM stack (`sleep_gpio.c:150`). |
| ZD-03 | Where the panel was begun or reset, send the panel controller's deep-sleep command before sleeping. | **Done** for normal sleep (`src/main.cpp` `enterDeepSleep()` → `display.deepSleep()`: SSD1677 `0x10`/`0x03`; UC8279 power-off, then DSLP `0x07`/`0xA5`) and the boot cutoff after anything but a deep-sleep wake (`sleepIfBatteryEmptyAtBoot()` probes the controller, begins the panel without a refresh and sleeps it; app `b86e9d49`). A ghost wake never begins or probes the panel. The abnormal-reset sleep uses no driver: `sleepIfResetLoop()` resolves the controller first (a probe, since the RTC cache is trusted after a deep-sleep wake only), then SDK `PowerManager::sleepPanelWithoutDriver()` pulses RST (analog and clock off; a fast refresh leaves them on, and a crash does not reset the panel), waits for BUSY (≤100 ms), then bit-bangs that controller's command. **The probe** (SDK `XteinkDetect`) runs at every cold boot before `display.begin()`: two reset pulses and register reads, each pass holding reset 50 ms (the vendor timing; a UC8279d has read blank through the short one), about 115 ms on an SSD1677 and about 170 ms on a UC8279 (then the MTP read). A FLG driven the same on both passes is taken as UltraChip, so a UC8279D is never driven as an SSD1677, whose waits would time out and whose `0x10`/`0x03` is not the UC's deep sleep. A deep-sleep wake reuses the verdict from RTC memory (16 bytes) with no probe; the driver's own reset then reads `BUSY` once (a UC8279 idles HIGH, an SSD1677 LOW), and only a level that stays wrong for 300 ms (a panel swapped while the board slept: 3V3 stays live) drops the verdict and restarts, which probes. A UC `BUSY_N` wait gives up after 60 s and drops the verdict, so no wrong panel holds the CPU awake until the cell is flat. Only a boot that goes on to `display.begin()` (or to one of the two sleeps above, which send the command) runs it, so no path leaves a probed panel out of deep sleep. |
| ZD-04 | EPD RST (IO47) held HIGH through sleep, no LOW glitch on wake. | **Done.** SDK `PowerManager.cpp` `holdRailOff()` (level preset); `EpdBus::begin()` presets HIGH and releases the hold last (SDK `983f2a7`). |
| ZD-05 | CS HIGH, SCLK/MOSI/DC LOW, all held in sleep. | **Done.** SILKSCREEN `parkedPins` (SDK `983f2a7`); `BoardConfig::holdPowerRails()` releases them at boot. |
| ZD-06 | Never pull down or hold IO48 in sleep. | **Done.** `powerDownRailsForSleep()` (busyPullDown and busyPullUp branch); IDF also strips the pulls of every unheld digital pad at sleep entry. |
| ZD-07 | SD off: write back, unmount, deinit, bus LOW with pulls off, IO10 HIGH, ≥20 ms; held in sleep; never a pull-down on IO10. | **Done.** SDK `SDCardManager::shutdown()` (via `HalStorage::prepareForDeepSleep`), `SdmmcParkedMount.cpp` (a failed mount parks), `powerDownRailsForSleep()` holds the bus (SDK `983f2a7`), `holdRailOff` IO10 HIGH. `shutdown()` first writes SdFat's cached directory/data and FAT sectors back (`SDCardManager::syncVolume()`, SDK `65660a6`): `FsVolume::end()` writes nothing, so a sector left dirty would otherwise be lost with the card's power. The USB Drive handoff (`detachFilesystemForRawAccess()`) writes back the same way before the host takes the card. A file still open for writing keeps its own directory entry (its size) until it is closed; activities close their files on exit. |
| ZD-08 | Light dark, LEDC detached, IO42 and IO40 held LOW in sleep; never `gpio_reset_pin` on them; restore the light only after a verified wake. | **Done.** SDK `FrontlightManager::prepareForSleep()` from `HalPowerManager::sleepImpl()` before the parked holds (SDK `82fe8ab`, app `ccf474d2`), and from a shutdown handler before every software restart (`HalFrontlight::begin()`), so COLOR_SEL never changes under current. On the sleep path `enterDeepSleep()` (`src/main.cpp`) calls it first, right after the trip check, so the light is dark before the sleep entry's plugin delivery (popup, Wi-Fi join, handlers); `sleepImpl()`'s call then returns early, and the saved preference (Restore Light on Wake) is unchanged. `Frontlight.begin()` runs after the wake decision in `setup()`. An over-voltage trip switches the light and its saved preference off, so no wake, restart or power cycle drives the boost into the fault again. **Load probe** (`FrontlightConfig::loadProbeWindowMs`/`loadProbePeriodMs`, on the monitor timer since SDK `65660a6`): the boost runs only inside a switch-on the user asks for or Restore Light on Wake, never by itself at a boot or wake; each enable reads LED_MONIT every 20 ms for 500 ms on the monitor's timer, so the caller does not wait, and a string with no load goes dark at the first reading above the trip (U10 sees one OVP event). A switch-on that finds no light waits for the LED rail before trying the second string, on the same timer (about 1-3 s, at most 4 s), while the loop runs on; a Restore Light on Wake that finds no light (once per cold boot, and once after a wake if the light was removed during the sleep) no longer delays the first paint. The reader then shows "No front light detected" with the 10 s connect advice, after a restore, a trip and a second string the rail blocked too, from the second loop pass. After a no-light result the light panel's slider and brightness keys only set the level, so one gesture runs the probe once. A "no light" result is kept in RTC memory (8 bytes, trusted after a deep-sleep wake or a software restart only), so later wakes do not restore into nothing; a user's no-light result also saves the light as off. Nothing new runs in sleep or on a ghost wake: the probe adds no wake source, hold or pull, and `prepareForSleep()` only waits (at most 4 s, and only right after a trip) for the rail before COLOR_SEL rests LOW. |
| TP-01 | TP_RST held HIGH; FT6336U hibernate `0xA5 = 0x03` at every sleep entry; TP_INT unheld. | **Done.** `parkedPins {11, High, High}`; SDK `InputManager::prepareTouchForSleep()` via `HalGPIO::prepareForSleep()` in `sleepImpl()` (SDK `a26e398`, app `88d14b28`). The abnormal-reset sleep resets the controller (≥5 ms, ≥300 ms) and sends hibernate once (`InputManager::resetAndHibernateTouch()`), after reading back its FocalTech chip id. A device at 0x38 with another id (a J6 accessory, `TouchConfig::requireFocalTechId`) gets no write at all, at the probe or at any sleep entry, and the reader logs the refusal: a genuine tail refused by the id check is never hibernated and draws its Monitor current (about 220 µA, about 165 µA over hibernate) in every sleep, so B9's accepted id is a release gate. A Silkscreen build with `FREEINK_CAP_TOUCH=0` fails a `static_assert`. The release-wake sleep sends nothing: the controller is as the last sleep entry left it. |
| TP-02 | If the hibernate write NACKs (Monitor mode closes the port), reset (≥5 ms, ≥300 ms) and retry. | **Done** for a controller found at boot (`pulseTouchReset()` then one retry). With none found: one best-effort write, no reset. |
| TP-03 | GT911: INT LOW ≥5 ms, `0x8040 = 0x05`, ≥58 ms, hold INT LOW; release it before the next reset. | **Done** (contingency: the -FT01C drawing names an FT6336U, and the GT911 fallback is built only with `-DSILKSCREEN_TOUCH_ALT_JUMPERS=1`, so a default build never drives IO41). A GT911 tail on the alternate jumpers running a default image is reset awake by the FT probe (at 0x14) and never gets its sleep command: about 3.3 mA in every deep sleep. P2 reports it as `touch_not_probed`; such a board needs the alternate-jumper build. If the command NACKs, INT is released rather than held. The abnormal-reset sleep in an alternate-jumper build never pulses TP_RST, which would restart a GT911 awake: it sends the sleep command at 0x5D or 0x14 (and caches the GT911 for the next wake), holding INT LOW even with no answer. The INT hold ends at the wake, so with the probe cached as GT911 (its address kept in RTC memory) INT is driven LOW at `begin(false)`, a ghost-wake sleep re-sends the sleep command (and holds INT LOW even on a NACK), and a release-wake sleep re-holds INT LOW. |
| PB-01 | Bound the power-button release wait; if still held, sleep with ext1 ANY_LOW and re-sleep on that wake before any peripheral starts. | **Done.** SDK `waitForPowerButtonRelease()` (5 s, `releaseWaitMaxMs`), `deepSleepUntilPowerButton()`, `takeReleaseWake()`; `setup()` calls it right after the serial setup (SDK `983f2a7`, app `b86e9d49`). `sleepImpl()` drops the clock to 80 MHz before the wait. |
| PB-02 | IO18 plain input, no pulls, awake and asleep. | **Done.** `InputPins::powerExternalPull` (SDK `abca595`). |
| WK-01 | Decide sleep-or-boot right after the wake-hold check, before SD, settings, light, panel, radio; re-sleep an unverified wake even if SD fails. | **Done** (SDK `983f2a7`, app `b86e9d49`). `earlyWakeDecision`, `gpio.begin(true)`, then `startTouch()`. The wake rule comes from RTC memory (`sleepWakePolicy`, written by `enterDeepSleep()`), not NVS. With no rule (the first wake after a power-on), the decision waits for the settings; an SD failure then still re-sleeps. That late decision also comes after upstream's plugin-folder scan and loan-clock restore (`pluginevents::refreshSubscriptions()`, `trustedtime::init()`: a few SD reads and one NVS read), left where upstream puts them; nothing new runs before the early decision. |
| WK-02 | A 30-60 s idle timeout after a verified wake with no input. | **Done with 180 s** (`wakeNoInputTimeoutS`): reading one page takes longer than 60 s with no input, so a shorter window would sleep under a reader. Armed by a power-button wake that boots, disarmed by the first key, touch or tilt input (not the wake press's own release), on battery only, and not while a screen holds auto-sleep off. `enterDeepSleep(true, /*unattended=*/true)`, so Quick Resume after timeout applies, and this sleep emits no `sleep.enter`, joins no Wi-Fi and delivers no plugin event (`deliverSleepPluginEvents()`): nobody is at the device, nothing has changed since the last sleep entry, and queued events wait for the next drain. The SD-card error screen sleeps after 60 s on battery (`storageErrorSleepS`). |
| WK-03 | After 3 abnormal resets in a row, sleep at once without SD, panel or radio. | **Done.** `src/main.cpp` `sleepIfResetLoop()` → SDK `countAbnormalResets()` (panic, watchdogs, CPU lockup, brown-out, aborted sleep via `takeAbortedSleepInfo()`) → panel controller probed, then panel and touch quiesced without their drivers (ZD-03, TP-01) → `startDeepSleepMinimal()`. Any other reset reason (a power-button wake included) starts a new count, and so does 60 s of uptime. |
| BAT-01 | Low-battery cutoff at boot and while awake, with Wi-Fi on too; block firmware writes near empty. | **Done.** `sleepIfBatteryEmptyAtBoot()` and the once-a-second loop check with the 100 mV radio allowance (`HalPowerManager::lowBatteryCutoffTripped()` → `isBelowCutoff(…, radioOn)`). The check latches, and web uploads, the web server's device-side fetch (`/api/fetch`) and relay (`/api/relay`), every `HttpDownloader` transfer (the OPDS feed and books, plugin-catalog, font and font-list downloads, the OTA check and download) and the plugin event drains (all of which block the loop) poll it and abort, so the next loop pass sleeps. `HttpDownloader` polls it in `fetchResumable()`'s abort predicate, which runs in every wait loop, not only from the progress callbacks: those run only when bytes arrive, and a stalled body is retried with Range requests. A sleep entry at the cutoff delivers no plugin events (`deliverSleepPluginEvents()` in `src/main.cpp`). A `USB_STAT` reading stuck outside every window for 30 s drops the last state (`unknownHoldSamples`), and Charging/Complete with `P+` at or below the cutoff and not rising for 5 min is treated as on battery (HW-08 cross-check; above the cutoff a clamped ladder is not caught, see HW-08's contingency). 3.6 V floor in `HalPowerManager::firmwareUpdateBlockedByBattery()` (OTA, SD update). The threshold still needs the bench check in [section 4](#4-measurement-and-bench-checks); the brown-out level is unchanged. |
| I2C-01 | Nothing in flight at sleep; SDA/SCL released HIGH; stuck-bus recovery at boot. | **Done.** SDK `SharedI2c.h` `beginBus()` / `recoverStuckBus()` / `releaseSensorBusForSleep()`, called by `sleepImpl()` on every path, minimal included (SDK `a26e398`). |
| RTC-01 | Clear DS3231 EN32KHZ; RV-8263 CLKOUT off. | **Done.** SDK `Rtc.cpp` `beginDevice()`, re-cleared on every time set (SDK `abca595`). |
| ADC-01 | IO1/2/4/8/9 inputs with no pulls; never `gpio_reset_pin` on them or on IO18/40/42/SD. | **Done.** `InputManager::begin()` (`INPUT`), `batteryChargeStatus` unassigned, every SDK ADC read through `SharedAdc.h`. One exception: the ladder presence test (`PowerConfig::ladderPresenceProbe`) turns IO1's and IO4's pulls on for about 2 ms each at a reset other than a deep-sleep wake, then off. IO18's idle check reads it with both pulls off. No port code calls `gpio_reset_pin`; the IDF SDMMC slot init does, and the mount clears the pull-ups it turns on. |
| AW-01 | Auto-sleep always armed on battery: a hard inactivity ceiling. | **Done** as a 15-min ceiling on battery with no input and no web-server or WebSocket traffic (`src/main.cpp` `loop()`, `HalPowerManager::awakeCeilingPassed()`, `noteClientTraffic()` from `CrossPointWebServer`). It covers the network screens, the keyboard, OTA and Never. OPTIONS, HEAD, PROPFIND, LOCK and UNLOCK (what an OS sends to poll a mounted WebDAV share) do not count as traffic, nor does the plugin-job poll (`GET /api/plugin-jobs/claim`, every 2.5 s from any open page hosting a plugin action, queued job or not; a claimed job counts). 60 min with no input ends any screen on battery whatever the traffic (`awakeInputCapMin`). Never itself is not hidden. **Plugin event drains** (at a station join, when the web server starts, at sleep entry) block the loop, so they stop by themselves at the ceiling, the input cap or the cutoff (`HalPowerManager::backgroundNetworkMustStop()`, polled by `pluginevents::drain()` and by every request it makes); undelivered events stay queued. A sleep entry on battery with either already passed (the ceiling's or the cap's own sleep, or a sleep timeout of 15 min or more with no client traffic) joins no Wi-Fi and delivers no plugin events. |
| AW-02 | Radio off when a network screen's work ends; skip the loop delay only while working. | **Done** (`radioOffWhenIdle`, `HalPowerManager::releaseIdleRadio()`). OTA, Clock Sync and KOReader Auth switch Wi-Fi off at their result screens; KOReader Sync does on a failure and drops to modem sleep on the result screens that can still upload. Font Download keeps the radio (its lists download again) but spins only while loading or downloading and refreshes the idle timer once after a download instead of holding auto-sleep off. Clear Cache spins only while clearing. Each still restarts on exit to clear the heap. Upstream's plugin catalogs keep the radio while a catalog is open (as the OPDS browser does), but leaving one for the plugin picker switches it off, and the next pick joins again (`PluginCatalogActivity::exitCatalog()`; the activity still restarts on exit). The Wi-Fi picker the reader opens to sync a loan's clock switches it off when it is cancelled or fails to join, and so does a failed sync while its Retry popup waits (`EpubReaderActivity::beginLoanTimeSync()`). |
| AW-03 | USB Drive must end when the cable is pulled on battery. | **Done.** `UsbDriveActivity::cablePulledOnBattery()`: after external power was seen, 3 s of "on battery" (about 5 s after the pull) restarts to Home. |
| MEAS-01 | Sleep-current acceptance test. | Procedure in [section 4](#4-measurement-and-bench-checks). **Not yet run.** |
| OPT-01 | Optional: switch RTC slow memory off in sleep. | **Not done.** Saves ≤2 µA [est], and it would lose state that must survive deep sleep: the release-wake flag, the abnormal-reset count, the wake rule, the touch probe cache, the panel-controller cache, the key-ladder presence result (5 bytes), the aborted-sleep record and the front-light load record. (The probe caches fail safe: without them every wake probes again. Without the load record, Restore Light on Wake would try once per wake on a board with no light, one OVP event each.) |

## 4. Measurement and bench checks

**Sleep-current test (MEAS-01)**

1. Use a release build (`silkscreen-gh_release`). The dev env adds a 250 ms USB-serial wait (`CROSSPOINT_WAIT_FOR_USB_SERIAL`) to every boot, ghost wakes and re-sleeps included.
2. Put the cell, or a 3.8 V source, on J5 through a µA meter or a PPK2-class logger.
3. Unplug USB, and take any UART adapter off TP1/TP2. Fit the SD card, connect the panel, light off, J6 empty.
4. Wait at least 60 s after sleep entry, then average over at least 60 s. The DS3231 converts every 10 s (≈575 µA for ≈10 ms), so short readings mislead.
5. Check a 30-min log: the conversion blip should be the only structure, with no boot spikes. After a wake, the log must show wake cause EXT1.

**Expected results**

- Without touch: ≈58-73 µA. With an FT6336U hibernated: ≈113-130 µA.
- Above ≈150 µA, A/B one change at a time: J4 tail out, J2 flex out, R5 lifted, card out.
- **What a high reading points at** (BRING_UP §6; the test firmware's P5 lists these above the budget):
  - +0-15 µA: U3's extra ground current at its real load (row 4; BRING_UP §6 says 0-20);
  - +≈165-235 µA: a cell below ≈3.3-3.35 V, with U3 in dropout (its IQ rises from 25 to ≈190-260 µA; BRING_UP §6 quotes the total);
  - +100-300 µA: the GPIO47/48 back-feed (HW-01; the R5, R34 and flex checks below);
  - +18 µA: R36 and R72 both fitted (IO18 idles at ≈1.76 V, and the boot check then arms no wake);
  - touch: +55 µA hibernated, +220 µA in Monitor mode, ≈1.1 mA with TP_RST LOW;
  - mA: the TP4056 back-feed on battery (HW-08, with D2 lit or glowing), or an unparked SD bus (SD_VDD ≈3.2 V).
- At a 3.30 V supply, expect the LDO dropout step (+≈165-200 µA at 25 °C).
- Repeat the measurement after each sleep path. All should agree within ≈2 µA:
  - menu sleep, auto-sleep and the 15-min ceiling;
  - the loop and boot cutoffs;
  - the ghost-wake and release-wake re-sleeps;
  - the SD-error screen timeout (60 s) and the no-input timeout after a wake (180 s);
  - 3 forced panics (a test build), with the panel refreshed fast before them: the driver-free panel sleep must bring it to the same floor.

**Bench checks**

- **Pin levels in sleep:**
  - ≈3.3 V: IO47, IO14, IO11, IO38, IO39, IO10.
  - IO48 ≈3.3 V with an SSD1677 panel; with the UC8279D panel note the level (not in hand).
  - 0 V: IO12, IO13, IO21, IO40, IO5-7, IO15-17.
  - IO42 below 0.385 V.
  - SD_VDD (C36) below 0.5 V.
  - IO42 and IO40 also after sleeping with the light on, warm and cool, and after sleeping within 4 s of a front-light trip on the warm string.
  - IO47 at 3.3 V does not rule out back-feed.
- **GPIO47/48 supply (HW-01):**
  - Read `espefuse.py --port <COM> summary` and look at `PIN_POWER_SELECTION`. It is read-only, and IDF says it covers GPIO33-37 only, so it may not settle the question.
  - Quick: in sleep, the voltage across R5 should be ≈0 V. Every 0.1 V is 10 µA flowing into IO47 (§6.1).
  - Across R34 (33 Ω in BUSY) it should be ≈0 mV too. Every 1 mV is 30 µA flowing into IO48 (BRING_UP §4). Use the meter's 200 mV range for both.
  - Decisive: sleep current with the J2 flex in and out, then with R5 lifted (flex out). If the flex adds more than ≈6 µA (the GDEQ0426T82's deep-sleep maximum), or lifting R5 changes the current at all, back-feed is confirmed. The custom UC8279D panel's own deep-sleep draw is not in hand. With it, the flex in/out difference mixes that draw with any BUSY back-feed, so rely on the R5 test there and record the difference (row 17).
  - With the GDEQ0426T82, the flex in and out should differ by a few µA (its 2-6 µA). A 100-300 µA gap is back-feed; ≈40-70 µA is a panel that never reached deep sleep (BRING_UP §4).
  - The test firmware's P5 takes these readings after a sleep (the current, the flex in or out, across R5 and across R34) and judges R5 under 20 mV, R34 under 0.2 mV and, with an SSD1677 panel, a flex gap of at most 8 µA in the same set-up.
  - Watch for flash boot failures after wake.
- **Charger on battery (HW-08):** USB_VBUS ≈0 V, D2 dark, USB_STAT (IO9) ≈2.20 V (≈2.06 V on a 10 MΩ meter). D2 lit means the TP4056 pins conduct into VCC (≈500 µA). USB_STAT at 0.7-1.2 V means the unpowered TP4056 clamps CHRG/STDBY, so the firmware cannot tell the cell from USB; 3.3 V means ST is not asserting (BRING_UP §4). The test firmware's T2 stage 1 judges 10 samples since the last change: `hw08_clamp` for any at or below 1350 mV (the charging or complete band), WARN `partial_clamp` for 1350-2100 mV, `above_nominal` for 2300-2600 mV and `st_not_asserting` above 2600 mV.
- **Touch (tail fitted):**
  - An I2C scan shows 0x38 (FT6336U), or 0x5D/0x14 (GT911, alternate-jumper builds).
  - With a device other than a FocalTech controller at 0x38 (an AHT20 on J6) and no tail: the log shows no touch, and a logic analyzer shows only the ID read (`0xA3`) at boot and nothing at sleep entry.
  - On a logic analyzer at sleep entry, `0xA5 = 0x03` is ACKed, or NACKed and followed by a reset and an ACK.
  - Current into J4 pin 2: ≈55 µA in hibernate, ≈220 µA in Monitor, ≈1100 µA with RST LOW.
  - Also check that IO41 does not pick up noise that wakes the controller (see [section 7](#7-open-questions)).
- **Wake:**
  - 30 min asleep without a false wake.
  - A short tap stays awake ≤0.3 s, with and without a card.
  - Scope IO47 across a ghost wake: no LOW excursion.
  - Tape the button down and trigger a sleep: the current falls to floor + ≈30 µA within ≈5 s, and the release gives one short wake.
  - 3 forced panics: the device then stays at the floor.
- **Battery:**
  - Ramp a supply down on J5: the device sleeps at ≈3.40 V, or ≈3.30 V as read with Wi-Fi on.
  - 10 min after the cutoff, resting P+ must be ≥3.5 V; otherwise raise `lowBatteryCutoffMv`.
  - Log IO8 with Wi-Fi on and off at one supply voltage to check the 100 mV allowance.
- **Awake:**
  - The idle current on the OTA, Clock Sync and KOReader result screens equals the normal idle current (Wi-Fi off).
  - Each network screen, left idle on battery, sleeps within 15 min, also with a browser left open on the File Manager with a plugin that registers an action (its job poll every 2.5 s does not count as traffic).
  - The plugin picker, after Back out of a plugin catalog, idles at the normal idle current (Wi-Fi off).
  - With a plugin subscribed to `sleep.enter` and its server unreachable: log the sleep entry's awake time and charge; at the cutoff, after the 15-min ceiling's sleep and on a WK-02 re-sleep (a power-button wake left without input for 3 min), the log shows no Wi-Fi join. With the front light on as well, the LEDs go dark as the sleep starts, before the "Loading" popup or the join, and Restore Light on Wake still restores the light on the next verified wake.
  - Pulling USB during USB Drive on battery returns to Home within ≈5 s.
- **RTC:** register `0x0F` bit 3 reads 0 after init. With a non-DS3231 device at 0x68 on J6 (an MPU-6050), the log says it is not the RTC, and a logic analyzer shows only register reads at 0x68.
- **Key ladders:** with R28 and C28 off (no side keys), the log shows `Key ladder IO4 has no pull-up` and the board sleeps on its timeouts with no phantom keys; the sleep current matches the standard board.
- **Power button idle:** with R72 fitted next to R36 (rework only), the board shows the power-button fault, sleeps, and wakes only from reset (SW11); sleep current is the floor plus ≈18 µA.
- **Guard:** `bash scripts/check_sleep_wake_sources.sh` passes.

## 5. The awake idle tail

After every session the device stays awake until auto-sleep, and this tail outweighs the sleep floor.

- The idle path: 3 s at the normal clock (`IDLE_POWER_SAVING_MS`), then 80 MHz (`HalPowerManager::LOW_POWER_FREQ` on PSRAM boards) in the loop's delay slices. The prebuilt core has no `CONFIG_PM_ENABLE` or tickless idle.
- **Draw:** ≈22-36 mA for the chip at 80 MHz with the CPU waiting [DS, S3 DS v2.2 Table 5-9]. PSRAM standby and a powered, mounted SD card (0.2-2 mA) take the board to ≈25-40 mA [est].
- **Default 10-min auto-sleep:** ≈3.7-6 mAh per session (chip only; ≈4-6.5 mAh for the board [est]). That equals 2.7-4.3 days of the 58 µA floor.
- **15-min awake ceiling** (screens that hold auto-sleep off, or Never):
  - radio off: ≈5.5-9 mAh;
  - Wi-Fi on: power saving is forced off, so full clock plus the radio at tens to over 100 mA gives ≈12-30 mAh [est].
- A wake that passes the hold check but gets no input pays 3 min of the tail on battery (WK-02), not the whole timeout, and its sleep joins no Wi-Fi and delivers no plugin event.
- **Plugin events at sleep entry** (upstream's SD-card plugins). With a plugin installed that subscribes to `sleep.enter`, or a queued event whose handler asks to connect, every sleep entry except a WK-02 re-sleep joins the last Wi-Fi network (at most 10 s) and runs up to 4 HTTPS handlers before the panel sleeps, with the front light already dark (`enterDeepSleep()`), so its boost adds nothing: roughly 0.3-1 mAh per sleep entry, more when a handler's server is slow [est: 10 s at ~100 mA for the join, a few seconds at 100-200 mA per handler].
  - Upstream skips the join below 20 % battery. At the low-battery cutoff, once the awake ceiling or the input cap has passed (so after their own sleeps), and on a WK-02 re-sleep, no plugin event goes over the network at all; a WK-02 re-sleep also queues no `sleep.enter`. An ordinary auto-sleep timeout (the user's setting, shorter than the 15-min ceiling) still delivers: it ends a session that had input, so a plugin may have something new to send. Counting it as unattended too is a policy choice not made yet.
  - Upstream bounds each request (30 s per connect, header and body gap) and the number of events delivered (4), not the drain's length: an unreachable server costs each subscribing plugin about 30 s per sleep entry, and a server that trickles its reply can hold a request longer. The drain stops at the ceiling, counted from the last input or client traffic, so a sleep the user asked for reaches deep sleep at most about 15 min after that input [the bound, not an estimate of the usual case]. Measure a sleep entry with a subscribing plugin and its server unreachable before relying on the estimate above.
  - With no such plugin the cost is one scan of the plugin folders at boot (and when the web server or the plugin list closes) and nothing at sleep.
- **Levers not used yet** (measure first):
  - a lower idle clock (40 MHz: 13.2-18.8 mA [DS]; not checked with octal PSRAM). Upstream keeps 80 MHz on PSRAM boards;
  - a shorter default timeout.

## 6. Hardware notes for a future revision

- **HW-01 (highest risk, unresolved): GPIO47/48 pad supply.**
  - The problem: EPD RST (IO47) and BUSY (IO48) are on the SPICLK_P/N pads. S3 DS v2.2 Table 2-1 gives their supply as VDD_SPI/VDD3P3_CPU, chosen by `EFUSE_PIN_POWER_SELECTION`, and IDF 5.5 always powers VDD_SPI off in deep sleep (keeping it on is not an option there).
  - The cost if these pads are unpowered: R5 and the SSD1677's HIGH BUSY output back-feed flash/PSRAM through the ESD diodes, ≈100-300 µA [est].
  - The fix: move RST and BUSY to VDD3P3_RTC or VDD3P3_CPU pads. IO3 is free for RST. BUSY needs a strap-safe pad: not IO45, and IO46 only after checking the boot-mode strap table.
  - HARDWARE.md §6.1 carries it as a first-article check since 2026-09-30.
- **HW-02: J4 touch VDD is always on (0 Ω R42).** A hibernated FT6336U still draws ≈55 µA and a GT911 70-120 µA. Only a load switch on J4 VDD reaches the ≈58 µA floor with touch. That switch needs bus isolation, or TP_RST/TP_INT held LOW, because R47/R48 and the control lines back-power an unpowered controller.
- **HW-03: the U3 TLV75533P is the largest floor term.** Its 25 µA IQ has an uncharacterized light-load term, and it rises to ≈190-260 µA in dropout. The fix: an LDO with flat dropout IQ and nA shutdown, rated for Wi-Fi peaks, with EN driven by a supervisor or load switch that disconnects the system below ≈3.4 V.
- **HW-04: the LED_MONIT divider (R39/R41) is always fed from LED_SW,** ≈2.9-3.4 µA with the light off. Scale it ×10 or gate it, after checking the ADC and monitor settling.
- **HW-05: RTC choice.** U14 RV-8263-C7 (0.19/0.24 µA) instead of U13 DS3231MZ (2/3 µA) saves ≈1.8-3 µA. The firmware already probes 0x51 as the fallback.
- **HW-06: the Fix-4 detector (≈10 µA) and BAT_MONIT (1.85 µA) are deliberate.** An N-FET gate on BAT_MONIT would save ≈1.9 µA.
- **HW-07: clone parts have unverified leakage** (SD05C-01FTG, TPD4E1U06, DW01A, 74LVC1G04, BSS138). Only a board measurement settles them.
- **HW-08: TP4056 CE is held at 3V3 while its VCC is 0 V on battery.** No datasheet row covers this. If CE, CHRG or STDBY conduct into VCC, D2 lights (≈500 µA) and USB_STAT reads ≈1.2 V ("charging"). Bench check in [section 4](#4-measurement-and-bench-checks). The firmware no longer trusts such a reading near empty: Charging/Complete with `P+` at or below the cutoff and not rising for 5 min is treated as on battery, and logged once. T2 stage 1 checks the level on each board (`hw08_clamp`).
  - **Contingency: a `P+` trend check. Not implemented; implement it only if T2 stage 1 fails `hw08_clamp` on real boards.** With the clamp, a board on its cell decodes "Charging" and is taken for one on USB. The cutoff still protects the cell (the cross-check above, about 5 min late), but until then the battery-only timeouts (the 15-min ceiling, the 60-min input cap, the 3-min wake timeout) do not apply, USB presence reads connected and the UI shows a charge.
  - The check would extend the HW-08 cross-check above the cutoff (`BatteryMonitor::pollChargeStatus()`). While the ladder decodes Charging, a real charge holds `P+` at the charger's output, rising in CC or at the 4.2 V float in CV, so `P+` falling across a window means the cell carries the load, and the state would be published as OnBattery, as the cross-check does today.
  - It would sample once a minute and 16× averaged, as the cross-check does, but (unlike it) only at a quiet moment, since the 0.24-0.35 Ω path reads low under load (§3.6), and with the window restarted on any ladder change (the cross-check restarts it only when the state leaves Charging/Complete).
  - The window and the fall that counts must come from the board: log T1 and T2 on a clamped board, on the cell and on a charger, before choosing them. Complete is harder, since a charged cell relaxes after termination.
  - It adds no wake source, hold or pull, and nothing in sleep.
- **HW-09: 2.5-6.5 µA keep flowing from the raw cell after a DW01A over-discharge trip.** §3.3's "years from 2.4 V to flat" is doubtful; months is more likely.
- **HW-10: held keys.** A held power button draws 30 µA through R62/R76, and a held ladder key 50-330 µA. Higher-value ladder resistors are the only mitigation.
- **HW-11: no GPIO senses VBUS.** A VBUS divider to a spare RTC pad (IO3) would make cable-pull detection deterministic; today USB_STAT stands in for it.
- **HW-12: TP_INT (IO41) is a digital pad and cannot wake the chip** (§8). That is fine for power. A GT911's INT hold works only because `gpio_deep_sleep_hold_en` is set, and it ends at the wake reset, so the firmware re-drives it on every boot and sleep entry before the probe. An RTC pad (or a board pull-down) would keep it LOW through the wake.

**Follow-ups for HARDWARE.md** (not edited from this repo):

- Done in HARDWARE.md on 2026-09-30:
  - §9.2 and §13.1 now say ext1 ANY_HIGH, not `ext0`;
  - §16 is itemized as in [section 1](#1-sleep-current-budget), with ESP32 7 µA typ and the touch and dropout cases;
  - §6.1 has the GPIO47/48 back-feed as a first-article check (HW-01);
  - §9.2 forbids R72 with R36 still fitted, and says a board shortened at the cut line needs the UP(2)-as-power option;
  - §13.1 covers the front light's open-load enable and the 10 s wait before connecting a light.
- Still open:
  - §3.3's "years" claim should change (HW-09).
  - §13.1 should list three parts as sleep/wake-critical: R75 (COLOR_SEL pull-down), U10's ADIM pull-down and R5 (EPD RST pull-up). S3 drops digital-pad holds at a deep-sleep wake, so only these parts hold IO40/IO42/IO47 until firmware drives them.
  - R49/R50: a 1-2 LED string on J3 would conduct in sleep through Q6 and R37. The bonded 5-LED strings do not.

## 7. Open questions

- **GPIO47/48 supply on this N16R8,** and whether it back-feeds in sleep (HW-01, bench check in [section 4](#4-measurement-and-bench-checks)).
- **The custom UC8279D panel in deep sleep:** its current after DSLP (row 17) and the level its `BUSY_N` holds on IO48. A LOW `BUSY_N` would feed the IO48 pad differently from the SSD1677's HIGH `BUSY` (HW-01).
- **FT6336U behaviour:**
  - the hibernate current at 3.3 V (55 µA is at 2.8 V, no max);
  - whether `0xA5 = 0x03` ACKs in Monitor mode; if not, every sleep entry adds the ≈307 ms reset retry;
  - the real RSTN pull-up current;
  - whether the fitted tail has its own pull-ups.
- **TP_INT floats in sleep.** IDF isolates IO41 and the board has no pull. If noise wakes an FT6336U from hibernate (an INT LOW pulse does), sleep current rises toward Monitor (220 µA). If measured, hold IO41 HIGH in sleep.
- **The TLV755P ground current at a 20-40 µA load:** the largest budget uncertainty.
- **The cutoff:**
  - Does 3400 mV keep the resting P+ at or above ≈3.5 V?
  - What P+ offset (12-175 mV, load-dependent) applies at the moment of the check?
  - Is the 100 mV Wi-Fi allowance right (0.24-0.35 Ω × 100-300 mA; ≈175 mV at 500 mA)?
  - Is a brown-out level change (PWR-04) possible on the prebuilt core?
  - Does the chip brown out or loop through resets in LDO dropout, and at what cell voltage?
- **The ext1 ANY_LOW release wake** relies on R76 holding IO18 LOW with RTC_PERIPH off. IDF holds the pad in its ext1 prepare, but this is not bench-verified. A bouncing or stuck button cycles with ≈5 s awake per cycle.
- **What state un-held RTC pads take in S3 deep sleep** with RTC_PERIPH off: IO1-4, IO8, IO9 (ESP-IDF is silent). Probe them in sleep.
- **Temperature:** ESP32 deep-sleep current above 25 °C (13 µA is a placeholder), and the MLCC, FET and TVS leakage terms at 60-85 °C (a device left in a hot car).
- **Gating the SD card between accesses while awake** (0.2-2 mA against a ≈200 ms remount): a product decision.
- **Do the prebuilt Arduino-ESP32 3.3.11 IDF libraries match the local framework-espidf 5.5 source** that these conclusions read (pad isolation, pull routing, VDD_SPI power-down)?
- **The cell capacity (mAh),** needed to turn these figures into storage-life claims.
