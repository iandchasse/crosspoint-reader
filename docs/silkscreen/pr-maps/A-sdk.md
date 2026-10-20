# A-sdk.md - what the Silkscreen SDK fork adds to Free-Ink SDK f9cc1be (hunk-level map for upstream PRs)

Pinned: base `f9cc1be` (= fk-upstream/main) vs ours `bee6382`. `git diff f9cc1be bee6382` = 103 files, +11,062/-182
(source 43 files +7,033/-144; tests 57 files +3,634/-29; docs 3 files +395/-9). Only git/grep/file reads on the pinned
revisions; nothing built or run; no repo file touched. Line numbers are OURS (bee6382) unless prefixed `base:`.
Bucket line counts are by new-side line range; mixed hunks are split by sub-range. Per-file totals reconcile with
`--numstat` (BoardConfig.h rows sum to 1,178 of 1,181); inside InputManager.cpp / FrontlightManager.cpp / PowerManager.cpp the
sub-split is good to about +-15%. "#if-gated" was measured by script (nested-#if tracking over the 12 Silkscreen-only macros).
Anything marked GUESS is unverified.

Headlines (details below):
- Of 7,033 added source lines, a 4.26" device PR needs about 1,580-1,680 (Q1: ADC ladders, charge ladder, pin parking + external-pull power key, parked SDMMC, enable-gated light, I2C/RTC care, SSD1677 config, profile).
  BATLOG (1,441), DIAG (798), PROBES (1,715), POLICY (428) and X368 (303) are separable; GENERIC-FIX (160) are the only hunks with a deliberate behaviour change on other boards (API/compile-level deltas are in Q7).
- Only 42% of the added lines are behind a Silkscreen-only `#if` (27% without BATLOG). The rest is data-gated at runtime or header-only, so "other boards pay nothing" is true for behaviour but NOT for flash/RAM (Q7 R7-R9).
- The seven `FREEINK_CAP_*` macros guard only ~890 lines in total (CHARGE_LADDER guards 2), all default to `FREEINK_DEVICE_SILKSCREEN`, and the profile's static_asserts force the PROBES/POLICY caps on.
- HW-DISPLAY is selected by profile data and does not change the X4 (Q4). Nine PowerConfig fields and `x4ClassPanel` have no SDK reader (consumer policy).
- Strongest stand-alone generic PR: `SDCardManager::syncVolume` (Q6b G4). Weakest: SharedAdc (latent upstream, real only with several ADC tasks).

Buckets: DEV=DEV-PROFILE INP=HW-INPUT PWR=HW-POWER FL=HW-FRONTLIGHT (FL-g gate, FL-b blend) DSP=HW-DISPLAY PRB=PROBES
POL=POLICY LOG=BATLOG DGN=DIAG X36=X368 TD=TESTS-DOCS GEN=GENERIC-FIX UNS=UNSURE.
GATING codes: `N` declaration/constexpr data only (no object code unless referenced); `D` compiled on every board, default-off
by profile data (runtime check); `#cap(X)` = `#if FREEINK_CAP_X`; `#DEV` = `#if FREEINK_DEVICE_SILKSCREEN`; `#DIAG`, `#BLOG`
(SILKSCREEN_BATLOG), `#368`; `C` compile-time assert only; `U` UNGATED behaviour change on other boards.
Paths: BC=libs/hardware/BoardConfig/include/BoardConfig.h; SA=.../BoardConfig/include/SharedAdc.h; SI=.../SharedI2c.h;
PC=.../ProbeChecks.h; BMh/BMc/CLD=libs/hardware/BatteryMonitor/{include/BatteryMonitor.h,src/BatteryMonitor.cpp,include/ChargeLadderDecoder.h};
IMh/IMc/ALD=libs/hardware/InputManager/{include/InputManager.h,src/InputManager.cpp,include/AdcLadderDecode.h};
FMh/FMc/FLL=libs/hardware/FrontlightManager/{include/FrontlightManager.h,src/FrontlightManager.cpp,include/FrontlightLoad.h};
PMh/PMc=libs/hardware/PowerManager/{include/PowerManager.h,src/PowerManager.cpp}; SDh/SDc/SBc/SBh/SPM=libs/hardware/SDCardManager/
{include/SDCardManager.h,src/SDCardManager.cpp,src/SdmmcBlockDevice.cpp,src/SdmmcBlockDevice.h,src/SdmmcParkedMount.cpp};
RTh/RTc=libs/hardware/Rtc/{include/Rtc.h,src/Rtc.cpp}; EBc/EBh/FDc/FDh/SSc/SSh/U9=libs/display/FreeInkDisplay/{src/bus/EpdBus.cpp,
src/bus/EpdBus.h,src/FreeInkDisplay.cpp,include/FreeInkDisplay.h,src/driver/Ssd1677Driver.cpp,src/driver/Ssd1677Driver.h,
src/driver/Uc8279*Driver.*}; XDc/XDh=libs/hardware/XteinkDetect/{src/XteinkDetect.cpp,include/XteinkDetect.h}; OP=libs/network/Opds.

## 1. Bucket summary

### 1a. By bucket (source files only; tests/docs in 1c)
| Bucket | src files | + | - | inside Silkscreen-only #if | not #if-gated (compiled on other boards) | behaviour change on other boards |
|---|---|---|---|---|---|---|
| DEV-PROFILE | 2 | 351 | 12 | ~15 | ~335 (constexpr data, enum/list edits) | none; but BoardProfile grows (R7) |
| HW-INPUT | 4 | 248 | 4 | ~45 | ~205 | none (X4 poll path unchanged except GEN swaps) |
| HW-POWER | 18 | 1,015 | 32 | ~15 | ~1,000 (touch sleep ~230 sits under shared `FREEINK_CAP_TOUCH`; parked SDMMC ~235 under shared `FREEINK_SD_SDMMC`) | ~8 lines (`beginBus` swaps; data-verified equal, R6) |
| HW-FRONTLIGHT | 4 | 465 (FL-g 293, FL-b 172) | 5 | ~130 (blend+probe hooks) | ~335 (whole gated path is compiled into every `FREEINK_CAP_FRONTLIGHT` build) | none at runtime (`gatedPath()` false) |
| HW-DISPLAY | 2 | 54 | 3 | ~30 | ~24 | none (Q4) |
| PROBES | 14 | 1,715 | 38 | ~650 | ~1,065 (struct fields, ProbeChecks.h, FrontlightLoad.h, touch-probe under `CAP_TOUCH`) | none |
| POLICY | 10 | 428 | 16 | ~20 | ~410 | API: `waitForPowerButtonRelease` void->bool, `begin(bool=true)` |
| BATLOG | 4 | 1,441 | 0 | 1,431 | 10 (hook include/call) | none |
| DIAG | 29 | 798 | 0 | ~530 | ~270 (ungated header API: TouchMapping, Rtc::address/type, scanSensorBus, FDc:68-72 static_assert) | none |
| X368 | 7 | 303 | 3 | ~50 | ~250 (constexpr `silkscreen_368` profile + asserts in BC) | none |
| GENERIC-FIX | 11 | 160 | 31 | 0 | 160 | YES - Q7 R1-R4, R14 |
| UNSURE | 2 | 37 | 0 | 0 | 37 | none |
| (rounding/unassigned) | | 18 | | | | |
| TOTAL source | 43 | 7,033 | 144 | 2,934 | 4,099 | |
Measured: 2,934 of 7,033 added source lines (42%) sit inside Silkscreen-only `#if` branches; of those 1,431 are BATLOG, 547 DIAG.
Without BATLOG only 1,503 of 5,602 lines (27%) are preprocessor-gated; the rest is data-gated or header-only. 22% of added source
lines are full-line comments (1,529); BoardConfig.h is 41% comment. Remaining non-comment, non-gated code is mostly struct/profile data.

### 1b. By library (source)
| Library | + | - | #if-gated | notes |
|---|---|---|---|---|
| BoardConfig (BC, SA, SI, PC) | 1,501 | 18 | 38 | BC 1,181 (profile 559 incl. 146 X368) |
| PowerManager | 1,848 | 12 | 1,531 | BATLOG 1,435 of it |
| FrontlightManager | 1,233 | 4 | 523 | FMc 821, FMh 263, FLL 149 |
| InputManager | 916 | 30 | 280 | IMc 660, IMh 201, ALD 55 |
| FreeInkDisplay | 384 | 9 | 235 | DIAG-heavy; HW-DISPLAY only 54 |
| BatteryMonitor | 474 | 6 | 45 | CLD 107, BMc 292, BMh 75 |
| SDCardManager | 282 | 16 | 89 | SPM 104 compiled into every SDMMC board |
| XteinkDetect | 237 | 10 | 160 | |
| Rtc / Imu / EnvironmentSensor | 129 | 32 | 33 | |
| Opds | 29 | 7 | 0 | |

### 1c. Tests and docs (TD): 57 test files +3,634/-29; README +256/-8; docs/testing.md +91/-1; docs/xteink-x3-uc8279-support.md +48
By owner bucket (lines, approx): LOG 1,213 (test_batterylog_core.cpp 1,113 + README share); X368 ~583 (test_silkscreen_368_policy 141,
FreeInkDisplay x3 diagnostics/uc8279/uc8253 tests+epdbus_txn_model 442) + docs 48; PRB ~646 (XteinkDetect policy test+README+stubs 266,
test_probe_checks 173, FrontlightManager test_load_policy+README 207); DEV ~269 (BoardConfig run_profile_asserts.py 207 + README 62);
INP ~285 (test_adc_ladder_decode 175, eego/metalio/picco stubs 105, run.sh 5); PWR ~285 (test_charge_ladder_decoder 214, BatteryMonitor
stubs 29, Rtc stubs 26, Haptic/Imu/SD stubs 16); POL ~333 (PowerManager test_sleep_panel.cpp + run_sleep_panel.py).
Existing upstream host-test stubs had to be edited because of SharedAdc/SharedI2c/syncVolume: HapticManager semphr.h +9, Imu Wire.h +4,
InputManager eego/metalio/picco stubs (+19/+43/+43), Rtc stubs +26, SDCardManager SdFat.h +3, BatteryMonitor stubs +26.

## 2. Hunk table (ours line ranges; `+n/-m` lines)
| ID | where | +/- | purpose | bucket | GATING | REQUIRES / split blocker |
|---|---|---|---|---|---|---|
| A1 | BC:84-86 | +3 | `FREEINK_DEVICE_SILKSCREEN` default 0 | DEV | N | root of everything below |
| A2 | BC:87-102 | +16 | `SILKSCREEN_PANEL_368`, `FREEINK_SILKSCREEN_368` | X36 | N | A1 |
| A3 | BC:116,118,129-130 | +4/-4 | no-device `#error` list+text; `FREEINK_MCU_S3` list | DEV | N | A1 |
| A4 | BC:144-148 | +4/-2 | SSD1677 link list += Silkscreen&&!368 (2 comment lines X36) | DEV | N | A1 |
| A5 | BC:166-167,171-175,180-190 | +12/-4 | `FREEINK_DRIVER_UC8279_X4` split off the UC8179 block (Silkscreen links UC8279_X4 not UC8179); X3 UC8279 for 368 | PRB 7 / X36 5 | N | same truth values for X4/X4PRO/X4CLASSIC (verified); not needed for an SSD1677-only profile |
| A6 | BC:237-244 | +6/-3 | `FREEINK_CAP_TOUCH` default += Silkscreen&&!368 | DEV 4 / X36 2 | N | A1 |
| A7 | BC:345 | +1/-1 | `FREEINK_CAP_RTC` default += Silkscreen | DEV | N | A1 |
| A8 | BC:377-416 | +40 | seven `FREEINK_CAP_*` macros (ADC_LADDERS, CHARGE_LADDER, POWER_POLICY, I2C_RECOVERY, DISPLAY_PROBE_POLICY, FRONTLIGHT_PROBE+#error, PROBE_IDENTITY), ALL default `FREEINK_DEVICE_SILKSCREEN` | INP 4 / PWR 8 / POL 4 / PRB 21 / DEV 3 | N | A1; circular: no mechanism PR can be Silkscreen-gated before A1 lands (Q6-E2) |
| A9 | BC:417-425 | +9 | `FREEINK_DIAGNOSTICS` default 0 | DGN | N | |
| A10 | BC:445 | +1/-1 | `FREEINK_SD_SDMMC` default += Silkscreen | DEV | N | A1 |
| A11 | BC:507,519 | +2 | `Board::Silkscreen` (DEV); `InputStyle::AdcLadders` (INP) | DEV/INP | N | |
| A12 | BC:573-584 | +12 | `DisplayPins::busyPullDown` (POL 4), `busyPullUp` (PRB 8, mentions X368) | POL/PRB | N (default false) | plumbing A112 |
| A13 | BC:616-628 | +13 | `SdmmcPins::maxFreqKhz, internalPullups, parkLowWhenOff, powerOffMs, powerOnSettleMs` | PWR | N | read by A104,A106,A101 |
| A14 | BC:679-682 | +4 | `InputPins::powerExternalPull` | INP (read by PWR) | N | read by A63,A90,A92 |
| A15 | BC:685-717 | +33 | `LadderButton`, `AdcLadderKey/AdcLadder/AdcLadderConfig` | INP | N | |
| A16 | BC:719-750 | +32 | `ChargeState`, `ChargeLadderWindow`, `ChargeStatusLadder` (`unknownHoldSamples` = POL 3) | PWR 29 / POL 3 | N | |
| A17 | BC:752-762 | +11 | `PinPark`, `ParkedPin` | PWR | N | |
| A18 | BC:803-831 | +29 | TouchConfig: `optional,fallback*` (PRB 11), `requireFocalTechId` (PRB 7), `resetPulseMs/SettleMs`+`hibernateInSleep` (PWR 8), `buttonsAlongsideTouch` (INP 3) | mixed | N | one struct, 4 buckets |
| A19 | BC:856-914 | +59 | FrontlightConfig: gate 856-875 (FL-g 20); monitor+probe 876-899 (PRB 24); blend 900-914 (FL-b 15) | mixed | N | blend fields are documented as needing the probe |
| A20 | BC:1001-1019 | +19 | SensorsConfig: `rtcLowPower, i2cBusRecovery` (PWR 8); `rtcFallback*, rtcIdentityCheck` (PRB 8); `rtcResyncWhenInvalid` (POL 3, no SDK reader) | mixed | N | |
| A21 | BC:1046-1107 | +62 | PowerConfig: 12 policy fields (POL 50); `ladderPresenceProbe, powerPinIdleCheck` (PRB 12) | POL/PRB | N | 9 of the 12 policy fields have NO SDK reader (consumer data; Q3) |
| A22 | BC:1121-1147 | +27 | `DisplayProbeConfig` (PRB 22; `x4ClassPanel` POL 5, no SDK reader) | PRB | N | |
| A23 | BC:1233-1245 | +13 | BoardProfile appended: `parkedPins[8]` (PWR), `adcLadders` (INP), `chargeLadder` (PWR), `displayProbe` (PRB) + comment (DEV 4) | mixed | N | appended -> positional initializers of other boards stay valid |
| A24 | BC:2438-2481 | +44 | profile header comment (DEV 23) + knobs `SILKSCREEN_FRONTLIGHT_TRIP_MV` (PRB), `_BLEND_HZ` (FL-b), `_TOUCH_ALT_JUMPERS` (PRB) with asserts | mixed | N | |
| A25 | BC:2482-2516 | +35 | `silkscreen_display` controller override (`-DSILKSCREEN_DISPLAY_CONTROLLER`) + 2 asserts | PRB 27 / X36 8 | N | A22 |
| A26 | BC:2517-2804 | +288 | `constexpr BoardProfile SILKSCREEN` (designated init; 105 comment-only + 182 data lines) | DEV | N (compiled in every build, no codegen) | names fields of A12-A23; designated init lets later-PR fields be omitted |
| A27 | BC:2805-2850 | +46 | profile static_asserts: fb size (DEV 3), PWM/blend resolution (FL 7), power pin RTC GPIO + powerExternalPull + touch hibernate (PWR 12), ADC ladders (INP 2), probe/policy caps required (PRB 20), 368 touch (X36 2) | mixed | C | the asserts `!CAP_FRONTLIGHT\|\|CAP_FRONTLIGHT_PROBE`, `PROBE_IDENTITY&&POWER_POLICY`, `DISPLAY_PROBE_POLICY` FORCE the PRB/POL caps on -> must be deleted in slim PRs |
| A28 | BC:2851-2996 | +146 | `silkscreen_368::derive` + ~20 asserts | X36 | C | |
| A29 | BC:3023-3040 | +14/-2 | `MAX_FRAMEBUFFER_BYTES` Silkscreen term (DEV 4, needed: else fb size 0) + 368 assert (X36 3); `DEFAULT_DEVICE` chain (DEV 3, X36 4) | DEV/X36 | N | A26 |
| A30 | BC:3079-3094 | +16 | pins DEFAULT_DEVICE to the right profile | X36 | C | |
| A31 | BC:3200-3208 | +9 | `selectDevice` case | DEV 4 / X36 5 | #DEV | A26 |
| A32 | BC:3235-3245 | +6/-1 | `isSilkscreen` (DEV); `hasFrontButtons` (INP 3, only with touch); `hasColorTemperatureFrontlight` += colorSelectGpio (FL-g 2) | mixed | N | |
| A33 | BC:3269-3336 | +66 | `driveParkedLevel` (PWR 10); `sdmmcParked`, `parkSdmmcBus`, `unparkSdmmcBus` (PWR 56) | PWR | N (inline; SDMMC part reached only from `FREEINK_SD_SDMMC` code) | dup of PMc:289 `driveLevel` and PMc:173 `holdRailOff` pattern |
| A34 | BC:3337-3368 | +26 | `holdPowerRails()`: parkedPins boot loop (PWR 20) + SDMMC park call (PWR 6) | PWR | D | A17,A33 |
| A35 | BC:3434-3503 | +70 | accessors: `hasChargeStatusLadder` (PWR 7), `hasAdcLadders` (INP 8), `powerPolicy()` (POL 9), `displayProbePolicy/displayBusyPullUp` (PRB 10), `touch/rtcIdentityCheck` (PRB 9), UC8279_X4 assert (PRB 4), `controllerPartName` (PRB 23) | mixed | #cap fold-to-false | |
| A36 | SA:1-60 | +60 | recursive ADC lock `freeink::adc::{Lock,readMilliVolts,readRaw,setAttenuation}` | GEN | N (but its call sites are U: A48,A69,A71) | needed by A47/A52, A69, A87, A97; X4 hot path (R1) |
| A37 | SI:13-114,161-175 | +128 | `sensorWire`, `busHz`, `recoverStuckBus`, `beginBus`, `releaseSensorBusForSleep` (recovery part under `#cap(I2C_RECOVERY)` 8 lines) | PWR | N / #cap(I2C_RECOVERY) | `sensorWire` is a 4th copy (Rtc, Imu, EnvironmentSensor have their own) |
| A38 | SI:116-159 | +44 | `scanSensorBus` (bring-up address scan) | DGN | N (inline, unused) | |
| A39 | PC:1-83 | +83 | `ladderFloats`, `focalTechChipId`, `ds3231Mismatch`, `pinLevel` | PRB | N | 4 unrelated rules for 4 consumers (IMc, Rtc, PMc) |
| A40 | CLD:1-107 | +107 | `ChargeLadderDecoder<Window>` state machine (accessors 87-93 DGN 7) | PWR | N | template; needs A16 |
| A41 | ALD:1-55 | +55 | `decodeAdcLadder<Key>`, `AdcLadderDebounce` | INP | N | |
| A42 | FLL:1-149 | +149 | load record + rules (PRB 123); `colourPlan`/`blendPossible` (FL-b 26) | PRB/FL-b | N | blend needs the record |
| A43 | BMh:2-3,82-86 | +7/-2 | `#include <BoardConfig.h>`; isExternalPowerPresent doc | PWR | N | |
| A44 | BMh:95-154 | +60 | `readMillivoltsAveraged` (PWR 5); `isBelowCutoff`+consts (POL 19); `pollChargeStatus/chargeStatus/chargeStatusLost` (PWR 9, POL 4); `ChargeLadderDiagnostics` (DGN 24) | mixed | #DIAG for last | |
| A45 | BMh:186-193; BMc:1155-1180,1206-1209 | +37 | `percentTenthsFromMillivolts` (Li-ion curve in 0.1 %, reuses base `LIION_NOTCH_MV`) | UNS | N | consumer feature (5 % steps, BATLOG); no SDK reader |
| A46 | BMc:6,8,15,18-19 | +5 | includes: `Logging.h` (NEW lib dependency; base only FrontlightManager/InputManager use it), `SharedAdc.h`, `<atomic>`, decoder | PWR | N | A36,A40 |
| A47 | BMc:703-710,786-788,803-810 | +19/-1 | ladder branches in `readStatus/isCharging/isExternalPowerPresent` | PWR | D (`hasChargeStatusLadder()` folds false) | A16,A35,A52 |
| A48 | BMc:738,753,756 | +3/-3 | `analogRead*` -> `freeink::adc::` | GEN | U | A36 |
| A49 | BMc:846-861 | +16 | `readMillivoltsAveraged` | PWR | D (unreferenced elsewhere) | used by POL/LOG/consumer |
| A50 | BMc:862-930 | +69 | `isBelowCutoff` (cutoff, 3-sample debounce, radio allowance) | POL | D (`cutoffMv==0` returns) | A21 fields |
| A51 | BMc:931-1007 | +77 | sampler state (PWR 14); trend state + `externalPowerDisproved` (POL 61); DIAG vars (2) | mixed | D | fused with A52 |
| A52 | BMc:1008-1059 | +52 | `pollChargeStatus` (PWR 44, calls A51 `externalPowerDisproved` at 1038-1039), `chargeStatus` (PWR 4), `chargeStatusLost` (POL 4) | PWR/POL | D | Q6-E4 |
| A53 | BMc:1060-1081 | +22 | `chargeLadderDiagnostics` | DGN | #DIAG | |
| A54 | IMh:19-20,77-80 | +6 | AdcLadderDecode include; `BTN_*==LadderButton` assert | INP | N | |
| A55 | IMh:24-29 | +6/-1 | `begin(bool startTouch=true)`, `startTouch()` | POL | D (default true) | early-wake feature |
| A56 | IMh:123-135 | +13 | `touchIdRefused/refusedTouchId` | PRB | #cap(PROBE_IDENTITY) | |
| A57 | IMh:136-165 | +30 | `TouchSleepResult`, `prepareTouchForSleep`, `holdTouchIntForSleep`, `resetAndHibernateTouch` | PWR | N | parallel to base `prepareForDeepSleep()` (Q5) |
| A58 | IMh:343-356 | +12 | `readButtonAdc` doc, `adcLadderContact()` | INP | N | |
| A59 | IMh:357-372 | +16 | `LadderPresence` | PRB | #cap(ADC_LADDERS) | |
| A60 | IMh:373-439 | +67 | `TouchMapping/setTouchMapping` (writes `ACTIVE.touch`), `TouchDiagnostics` | DGN | N (TouchMapping) / #DIAG | ungated test API in a production header |
| A61 | IMh:474-490,569-580,628-667 | +53 | private decls/members: ladder (INP 10), touch sleep (PWR 10), identity (PRB 3), diag members (DGN 30) | mixed | mixed | |
| A62 | IMc:3-18,35-40 | +15 | includes: ProbeChecks (PRB), SharedAdc (GEN), gpio/esp_system (PRB), SharedI2c (PWR), trace/cstring (DGN) | mixed | N | host-test stubs needed |
| A63 | IMc:70-75 | +6/-4 | `powerPinMode()` (INP 6; removes 3 inline `activeHigh?PULLDOWN:PULLUP` pinModes) | INP | D (same value when `powerExternalPull=false`) | dup of PMc:30 |
| A64 | IMc:76-113 | +38 | `TouchProbe` enum, RTC cache vars, `cachedTouchProbe/storeTouchProbe` | PRB | #CAP_TOUCH | optional-touch feature |
| A65 | IMc:114-142 | +29 | `cachedGt911Asleep`, `driveGt911IntLow`, FocalTech/GT911 sleep constants | PWR | #CAP_TOUCH | GT911 part needed only with `SILKSCREEN_TOUCH_ALT_JUMPERS` |
| A66 | IMc:143-166 | +24 | `foreignProbe`, `focalTechIdentified` | PRB | #cap(PROBE_IDENTITY) | A39 |
| A67 | IMc:167-232,434-436 | +69 | key-ladder presence (RTC cache, dual-pull test) + accessor | PRB | #cap(ADC_LADDERS) | A39 |
| A68 | IMc:276-362 | +41/-10 | `begin()`: signature (POL), DIAG record (DGN 8), GT911 INT guard under `!startTouch` (POL 5), AdcLadders init 307-325 (INP 17), `applyLadderPresence` call at 316 (PRB 1), X4-branch `adc::setAttenuation`+`powerPinMode` (GEN/INP 6), `startTouch()` (POL 2) | mixed | mixed | Q6-E5 |
| A69 | IMc:363-433 | +47/-2 | `sampleAdcLadder`, `readButtonAdc` ladder branch + `adc::readRaw` swaps, `adcLadderContact` | INP 45 / GEN 2 | D | A15,A41,A36 |
| A70 | IMc:437-471 | +35 | `touchDiagnostics()` | DGN | #DIAG | |
| A71 | IMc:472-590 | +52/-3 | `getState`: ladder branch (INP 8), OnePage/X4 `adc::` swaps (GEN 7), `getAdcLadderState` (INP 29 + trace DGN 8) | mixed | #cap(ADC_LADDERS) for body | |
| A72 | IMc:1655-1735 | +76 | `beginTouch` optional/fallback probe, RTC verdict, foreign-device handling | PRB | #CAP_TOUCH | Q6-E6 |
| A73 | IMc:1736-1887 | +118/-1 | `beginTouchController` split (beginBus swap) + `prepareTouchForSleep` (DIAG wrapper 14) + `holdTouchIntForSleep` + `resetAndHibernateTouch` | PWR 104 / DGN 14 | #CAP_TOUCH | uses `touchStarted`, `touchSleepController/Addr` set by A72 |
| A74 | IMc:2040-2092 | +53 | `pulseTouchReset`, `focaltechHibernate`, `gt911Sleep` | PWR | #CAP_TOUCH | |
| A75 | IMc:2117-2186,2373-2375,2485,2904,3061,3189-3191,3287-3289 | +56/-10 | `beginFt5x06` reset+identity+attempt count (PRB 30); `beginBus` swaps in GSLX680/GT911/FT6336U (PWR 4); `noteTouchRaw` (DGN 9) | mixed | #CAP_TOUCH | |
| A76 | FMh:1-66 | +48 | gate/blend/probe contract comment (8/8/20) + includes (esp_timer, semphr, atomic, FrontlightLoad) | FL-g 12 / FL-b 8 / PRB 28 | #CAP_FRONTLIGHT | 39-line comment (Q8) |
| A77 | FMh:101-138 | +28 | `blendsColour/blending` (FL-b 13), `prepareForSleep` (FL-g 12), fault comment (PRB 3) | mixed | #CAP_FRONTLIGHT; blend body #cap(FRONTLIGHT_PROBE) | A42 for blend |
| A78 | FMh:139-255 | +89 | `fault/clearFault/probing/load/stringMissing/restoreAllowed` (PRB 45); Monitor/ProbeDiagnostics (DGN 44) | PRB/DGN | #cap(FRONTLIGHT_PROBE)/#DIAG | |
| A79 | FMh:256-290 | +16/-1 | `hasColorTemperature` += colorSelectGpio; `warmSelected()` | FL-g | N | |
| A80 | FMh:291-440 | +82 | private: gate decls (FL-g 20), blend decls (FL-b 8), monitor/probe decls+members (PRB 49), diag members (DGN 5) | mixed | #CAP_FRONTLIGHT; probe members #cap(FRONTLIGHT_PROBE) | adds ~28 B to the singleton on every frontlight board |
| A81 | FMc:3-19,141-241 | +104 | include reorder; gate helpers `gatedPath,darkLevel,padToGpio,padToLedc,detachChannel` (FL-g 62); probe consts+RTC record (PRB 17); `monitorConfigured`+`GateGuard` (PRB 20); includes (PRB 5) | mixed | #CAP_FRONTLIGHT | |
| A82 | FMc:78-81,378-392 | +18 | `LEDC_CH_BLEND`, blend carrier attach | FL-b | #cap(FRONTLIGHT_PROBE) (392 part) | needs probe |
| A83 | FMc:299-377 | +47 | `begin()`: gate init (FL-g 20), monitor timer+probe restore (PRB 27) | mixed | D (`gatedPath()`) | |
| A84 | FMc:606-669 | +46 | `apply()` hook (FL-g 4); `applyGated` (FL-g 25, PRB 12, FL-b 5) | mixed | D | `#if PROBE` hooks inside (Q6-E3) |
| A85 | FMc:671-751 | +83 | `confirmsWhileLit, switchLitToConfirm, colourPlan, blendColour` (FL-b 63); `waitDarkDwell, selectWarm` (FL-g 20) | mixed | D (`gatedPath()`); blend arms #cap(FRONTLIGHT_PROBE) | blend needs the load record |
| A86 | FMc:753-825 | +74 | `goDark`, `lightUp` (FL-g 52; rail wait / probe window PRB 22) | mixed | D | |
| A87 | FMc:827-912 | +87 | `startMonitor/stopMonitor/monitorTick/checkMonitor` (LED-rail OVP trip: PRB 71, DGN 16) | PRB | D (needs `monitorConfigured`) | |
| A88 | FMc:914-1206 | +293 | probe window, rail wait, 2nd-string retry, RTC record | PRB (DGN 23) | #cap(FRONTLIGHT_PROBE) | A42 |
| A89 | FMc:1305-1384 | +64 | `prepareForSleep` (FL-g 22, PRB 8); DIAG accessors (DGN 26); `blendsColour` (FL-b 8) | mixed | D (`!_begun`/`gatedPath()` early return); diag #DIAG; blend #cap(FRONTLIGHT_PROBE) | |
| A90 | PMc:6,16-33 | +14 | `driver/rtc_io.h` (PWR 1); ProbeChecks/SharedAdc under POWER_POLICY (PRB 4); BatteryLog include (LOG 3); `powerPinMode` (PWR 6) | mixed | N | |
| A91 | PMc:46-87 | +38 | magics (POL 6), `preparePowerPinForSleep` (PWR 13), RTC_NOINIT vars (POL 3), bootRecord (DGN 4), powerPinCheckResult (PRB 12) | mixed | N; `powerPinCheckResult` #cap(POWER_POLICY); RTC_NOINIT vars unconditional (U-ish, R10) | 12 B RTC_NOINIT on every board |
| A92 | PMc:107-163 | +22/-14 | `armWakeOnPins` skips RTC pulls on a `powerExternalPull` pin (PWR 11); `armPowerButtonWakeup` (PWR 4); bounded `waitForPowerButtonRelease` (POL 7) | mixed | D | A14 |
| A93 | PMc:170-177 | +4 | `holdRailOff`: preset output level before enabling the driver | GEN | U | all rails at every sleep (R2) |
| A94 | PMc:227-276 | +44 | BUSY pull release (POL/PRB 10); SDMMC bus hold (PWR 12); parkedPins sleep loop (PWR 22) | mixed | D | A12,A13,A17,A33 |
| A95 | PMc:277-361 | +83 | `sleepPanelWithoutDriver` + `bitBangByte/driveLevel` | POL 72 / PRB 6 / X36 5 | D | `#if FREEINK_SILKSCREEN_368` at 315-320,332-336 |
| A96 | PMc:363-365 | +3 | BATLOG `tail()` hook in `deepSleep()` | LOG | #BLOG | |
| A97 | PMc:387-455 | +64/-1 | `deepSleepUntilPowerButton` (PRB 8 power-pin-undefined hold; POL 11 release-wake); `checkPowerPinIdle/powerPinCheck` (PRB 45) | PRB/POL | #cap(POWER_POLICY)/D | |
| A98 | PMc:457-491 | +37 | `takeReleaseWake`, `countAbnormalResets`, `clearAbnormalResets` | POL | D (unreferenced) | |
| A99 | PMc:505-528 | +22 | `peekAbnormalResets/peekAbortedSleepInfo/bootResetRecord` | DGN | #DIAG | |
| A100 | PMh:34-160 | +82/-3 | `waitForPowerButtonRelease` void->bool (POL); powerDown comment (PWR 8); sleepPanelWithoutDriver doc (POL 33, X36 3); checkPowerPinIdle/takeReleaseWake/count decls (PRB 17, POL 14); DIAG decls (DGN 21) | mixed | N | API change (R10) |
| A101 | SDc:137-159 | +22 | `shutdown()` parked branch (PWR 19; 3 `syncVolume()` calls GEN) | PWR | D | A33; A102 |
| A102 | SDh:95-153; SDc:92,157-159,315,330-343 | +31/-10 | `syncVolume()` (root.sync) before `FsVolume::end()`; SPI `shutdown()` newly defined | GEN | U | R4; verified `FsVolume::end()` only clears pointers (SdFat 2.3.1) |
| A103 | SDh:99-126; SDc:14-19,101-135,277-291 | +84 | `CardInfo/cardInfo()` | DGN | #DIAG | |
| A104 | SBc:27,54-58,70,96-99,129-131; SBh:53-55,62-64 | +20/-6 | `maxFreqKhz`, `internalPullups`, `sdOn/sdOff` from polarity, `beginParked` dispatch | PWR | D (defaults = old behaviour, R5) | A13 |
| A105 | SBc:18-21,124-127,147-149; SBh:41-49,66-70 | +22 | mount attempts / last error | DGN | #DIAG | |
| A106 | SPM:1-104 | +104 | `SdmmcBlockDevice::beginParked`: power-cycle mount with bus parked LOW | PWR (DGN 4) | compiled in every `FREEINK_SD_SDMMC` board, dead unless `parkLowWhenOff` | A13,A33; repeats host/slot setup of SBc:57-70 (SPM:30-41) |
| A107 | RTc:3-360; RTh:11-75 | +125/-30 | Rtc: SharedI2c (PWR 1), ProbeChecks (PRB 3), PCF85063 CONTROL2/EN32KHZ consts (PWR 5), `beginDevice` low-power writes (PWR 14), restructure (PWR 11), DS3231 identity (PRB 29), fallback RTC in `begin()` (PRB 27), `addr_/type_` plumbing (PRB 11), `set()` low-power (PWR 3); header: `address()/type()` (DGN 6), `rejectedAddress` (PRB 10), doc/include (4) | mixed | D / #cap(PROBE_IDENTITY) | Q6-E14 |
| A108 | Imu.cpp:7,95; EnvironmentSensor.cpp:7,39 | +4/-2 | `beginBus` swap | PWR | U-ish (equal clocks, R6) | A37 |
| A109 | EBc:17-27,86-122,355-357,475-477,488-497 | +66/-1 | cached-controller idle check (ends in `esp_restart()`), predecl of XteinkDetect API, 60 s UC BUSY_N cap | PRB | #cap(DISPLAY_PROBE_POLICY) | A111 |
| A110 | EBc:73-85,308-312 | +18 | `FREEINK_EPD_X3_BUSY_N`, X3TwoPhase pull-up | X36 | #368 | |
| A111 | EBc:271-303 | +16/-3 | CS preset HIGH before `SPI.begin`; RST preset HIGH + hold released last | GEN | U | R3 |
| A112 | EBc:304-318; EBh:69-73; FDc:276 | +21/-2 | BUSY mode chain (busyPullDown POL 5, busyPullUp PRB 4, X36 5, structure 7); `EpdPins::busyPullDown` + plumbing | mixed | D | Q6-E8 |
| A113 | EBc:207-242,501-640; EBh:27-57,249-262; FDc:68-72,1139-1146; FDh:309-323; SSc:476-478,554-556; U9 (Uc8279Driver.cpp/.h, Uc8279X4Driver.cpp/.h) | +~206 | EpdBusDiagnostics, `noteRefresh`, `setSpiClockTrial`, driver refresh reports | DGN | #DIAG (FDc:68-72 static_assert ungated) | |
| A114 | SSc:421,530-536,940-979; SSh:74-81 | +54/-3 | Silkscreen SSD1677 config (`booster[4]=0xC0`, full/half seq 0 = incremental 0x34, `halfUsesFullWaveform`), `case Board::Silkscreen`, opt-in `fullUsesHalfWaveform` fallback | DSP | #DEV (config 38) + D (2 flag checks) | Q4 |
| A115 | FDc:140-148 | +9 | 368 ctor `setDisplayX3()` | X36 | #368 | |
| A116 | XDh:69-73,117-146 | +33/-1 | probe-policy docs (PRB 28), 368 note (X36 5) | PRB | N | |
| A117 | XDc:12-34,35-79,311-340,396-407,501-620,701-706 | +204/-9 | `FREEINK_XTEINK_DISPLAY_PROBE` += Silkscreen (DEV 2); 368 no-probe block (X36 46); probe policy: vendor-reset pass 1, UC8279-by-FLG, RTC verdict cache, forced, promote (PRB 156) | mixed | #cap(DISPLAY_PROBE_POLICY)/#368 | removing X36 turns `#elif` back into `#if` |
| A118 | OP: OpdsImplicitAuth.h:3,15-20; OpdsTransport.h:61-65; OpdsClient.cpp:231; OpdsImplicitAuth.cpp:58-95,162-163 | +29/-9 | implicit-login chain polls the platform stop request (`shouldAbort`), `OpdsTransport::shouldAbort()` | GEN (consumer: low-battery cutoff, POL) | D (null callback) | useful only with a CrossPoint-side override |
| A119 | README.md; docs/testing.md; docs/xteink-x3-uc8279-support.md | +395/-9 | docs | TD (x3 page X36) | | no `docs/<device>-support.md` and no `platformio.sample.ini` env for the 4.26" board (WS397 c2438a8 added a 120-line doc + a 19-line env; Picco efae8dc a 23-line env) |
| A120 | libs/**/test/host/** (57 files) | +3,634/-29 | host tests+stubs | TD | | 1c |

### 2b. A26 profile sub-map (ours lines; comment + data) - what a slim profile keeps
| Lines | Content | Lines | Owner buckets | Slim (MIN-A) |
|---|---|---|---|---|
| 2517-2529 | board/name/inputStyle/controller/size + display comment | 13 | DEV (controller = `silkscreen_display::CONTROLLER`, A25) | keep; fix controller to `SSD1677` |
| 2530-2541 | `.display` (incl. `busyPullDown/busyPullUp` data), `.displaySpiHz` | 12 | DEV; POL/PRB for the two pulls | drop the two pulls |
| 2542-2551 | `.sd` (IO10 gate, active-LOW) | 10 | DEV/PWR | keep |
| 2550-2565 | `.input` (power IO18, `powerExternalPull`), battery ADC IO8, divider 2.0, no usbDetect | ~16 | INP/PWR | keep |
| 2566-2605 | `.touch` FT5x06 @0x38 incl. 12-line narrative | 40 | PRB (optional, reset, requireFocalTechId) + PWR (hibernate) + INP (buttonsAlongside) | drop (MIN-A) or keep ~12 lines with touch PRs |
| 2606-2641 | `.frontlight` (18-line TPS923610 narrative + 18 data lines) | 36 | FL-g 8 data; PRB 6 data (monitor, probe window, railSafeV, period); FL-b 2 data | keep gate fields, drop the rest |
| 2642-2660 | audio/leds/orientation + `.sdmmc` (4-bit, parked) | 19 | PWR/DEV | keep |
| 2661-2689 | gauge/mic + `.sensors` (DS3231 @0x68, PCF85063 fallback, lowPower, recovery, identity) + uiScale | 29 | PWR 8 / PRB 8 / POL 3 / DEV | keep `rtcLowPower`, `i2cBusRecovery` |
| 2690-2727 | `.power` (34-line rationale + 18 data lines) | 38 | POL 50-style data, PRB 2 | reduce to latch/charge pins |
| 2728-2732 | `displayControllerVariant`, `viewableInsets`, `batteryChargeStatusActiveHigh`, `i2cFrontlight` | 5 | DEV | keep |
| 2733-2745 | `.parkedPins` (7 pins: CS, SCLK, MOSI, DC, ADIM, COLOR_SEL, touch RST) | 13 | PWR | keep (drop touch-RST entry in MIN-A) |
| 2746-2767 | `.adcLadders` (2 ladders x 4 keys, releasedMinMv 3000) | 22 | INP | keep |
| 2768-2790 | `.chargeLadder` (4 windows; `unknownHoldSamples` 30) | 23 | PWR (+POL 1) | keep |
| 2791-2803 | `.displayProbe` | 13 | PRB (+POL `x4ClassPanel`) | drop |
Slim profile about 165-170 of the 288 lines (drops ~120).

## 3. Answers

### Q1. Minimum SDK set (SSD1677, keys, draw, charge-detect, sleep/wake, front light, SD; NO probes/policy/batlog/diag)
Not in our diff but required (upstream device-PR shape, cf. WS397 c2438a8 and Picco efae8dc): add Silkscreen to the `FREEINK_CAP_FRONTLIGHT` and
`FREEINK_CAP_WARMLIGHT` default lists (BC base:217-229; today the CrossPoint env passes `-DFREEINK_CAP_FRONTLIGHT=1 -DFREEINK_CAP_WARMLIGHT=1`,
platformio.ini:587-588), a `platformio.sample.ini` env, `docs/silkscreen-support.md`, a profile host test (run_profile_asserts.py is the nearest).
Packs (sizes = added source lines, +-15%; MIN-A has NO touch tail):
- P0 GEN prerequisites ~79: A36, call-site swaps A48, A69(2), A71(7), A68(2 attenuation lines), A93. Needed for correctness on a dual-core S3 with CrossPoint: the key poll and the charge poll share the loop task (lib/hal/HalGPIO.cpp:163-166) but the battery percentage
  is read from the render task (BaseTheme.cpp:122 -> HalPowerManager.cpp:383 `readMillivoltsAveraged`), and the LED monitor (PRB) adds the esp_timer task. Verified that IDF `adc_oneshot_read` returns ESP_ERR_TIMEOUT when the unit lock is
  held (esp_adc/adc_oneshot.c:218-220) and Arduino `__analogRead` ignores the error and returns 0 (esp32-hal-adc.c:316-317); read in the owner's ~/.platformio packages, not matched to upstream's pinned platform 55.03.37.
- P1 device + profile + display ~255: A1, A3, A4, A7, A10, A11, A23 (comment), A24 (trim), A26 (slim to ~170 lines, see 2b), A27 (DEV subset), A29 (DEV), A31 (DEV), A32 (isSilkscreen), A114 (drop `fullUsesHalfWaveform`) + cap defaults above.
- P2 ADC ladders ~230: A8 (ADC_LADDERS), A11b, A15, A23 (adcLadders), A35 (hasAdcLadders), A41, A54, A58, A61 (INP), A68 (307-325 minus 316), A69, A71 (INP part).
- P3 charge ladder ~216: A8 (CHARGE_LADDER), A16 (minus unknownHold), A23 (chargeLadder), A35 (hasChargeStatusLadder), A40, A43, A44 (PWR parts), A46, A47, A52 (minus `externalPowerDisproved` call), A49 optional.
- P4 pin parking + power key ~122: A14, A17, A23 (parkedPins), A33 (`driveParkedLevel`), A34 (loop), A63, A90 (PWR), A91 (`preparePowerPinForSleep`), A92 (PWR parts), A94 (parked loop), A27 (power-pin asserts).
  Needed: IO18 has an external bias, so internal/RTC pulls and `RTC_PERIPH` must stay off (A92; BC:2823-2829 asserts); ADIM/COLOR_SEL/CS/SCLK/MOSI/DC levels through sleep (profile `parkedPins`).
- P5 parked SDMMC ~223: A13, A33 (park/unpark), A34 (SDMMC call), A94 (SDMMC hold), A101, A104, A106.
- P6 front-light gate (fixed warm/cool, no monitor/probe/blend) ~293: A19 (gate), A27 (PWM asserts), A32 (hasColorTemperature), A76-A81 gate parts, A83 (gate init), A84-A86, A89 (prepareForSleep core); drop all `#if FREEINK_CAP_FRONTLIGHT_PROBE` arms and `GateGuard`.
- P7 I2C + RTC low power ~164: A8 (I2C_RECOVERY), A20 (rtcLowPower, i2cBusRecovery), A37, A107 (low-power parts only), A108; (+A73/A75 `beginBus` swaps only with touch). Arguably robustness (stuck-SDA drain); keep only the sleep release (~25 lines) if you want it smaller.
Total about 1,580-1,680 added source lines in ~24 files (~25-30% are comment lines, GUESS), i.e. 22-24% of the 7,033 source lines; about 15% of the whole diff. Upstream precedents: WS397 456 lines/10 files,
Picco 1,718/34 incl. tests, Metalio 2,711/64 incl. tests. MIN-B (+touch tail) adds ~300: A6, A18 (hibernate+reset+buttonsAlongside), A32 (hasFrontButtons), A57, A61, A65, A68 (GT911 guard optional), A73, A74, A75 and ~45 profile lines; FocalTech-only (drop GT911 pieces of A65/A74) saves ~90.
Not needed by the minimum (checked): `SdmmcPins::maxFreqKhz` (profile 20000 = `SDMMC_FREQ_DEFAULT`), `powerOnSettleMs` (20 = default), `busyPullDown`, `unknownHoldSamples`, all of PowerConfig, `percentTenths`, `readMillivoltsAveraged`.

### Q2. Capability macros (all seven default to `FREEINK_DEVICE_SILKSCREEN`; lines = positive-branch lines measured by script)
| Macro | Guards | Generic or Silkscreen-only | Size | Note |
|---|---|---|---|---|
| FREEINK_CAP_ADC_LADDERS | `hasAdcLadders()` (BC:3442), `getAdcLadderState` body (IMc:544-590), ladder-presence block (IMc:167-232), `LadderPresence`+`ladderDebounce` members (IMh) | mechanism generic (data-driven ladder; OnePage could use it), default-on Silkscreen only | 114 lines (BC 2, IMh 17, IMc 95) | `sampleAdcLadder`, `readButtonAdc` branch, `adcLadderContact`, `AdcLadderDecode.h` are NOT behind it (D) |
| FREEINK_CAP_CHARGE_LADDER | only `hasChargeStatusLadder()` (BC:3434-3440) | mechanism generic (any analog /STAT) | 2 lines | the ~290 lines of BatteryMonitor ladder code + `ChargeLadderDecoder.h` are NOT behind it; they fold to dead code through the accessor (GUESS: `--gc-sections` drops them; not measured) |
| FREEINK_CAP_POWER_POLICY | `powerPolicy()` (BC:3454-3456), `checkPowerPinIdle`/`powerPinCheckResult`/release-wake block (PMc:16-19,76-87,388-395,411-455), BatteryLog include | Silkscreen-only (consumer policy) | 65 lines (BC 2, PMc 63) | `PowerConfig` struct fields exist on every profile regardless |
| FREEINK_CAP_I2C_RECOVERY | `busRecoveryEnabled()` and the boot stuck-bus clocking in `beginBus` (SI:89-114) | generic in principle (pull-ups on an always-on rail), default-on Silkscreen only | 9 lines | `releaseSensorBusForSleep`/`recoverStuckBus` are inline and unreferenced when off |
| FREEINK_CAP_DISPLAY_PROBE_POLICY | `displayProbePolicy()/displayBusyPullUp()` (BC:3458-3467); EpdBus cached check, UC wait cap, BUSY_N pull-up (EBc:17-27,86-122,355-357,488-497); XteinkDetect policy/cache/promotion (XDc 123 lines); sleepPanelWithoutDriver UltraChip arm | Silkscreen-only (its SSD1677-or-UC8279D panel; X368 piggybacks) | 180 lines (BC 3, EBc 54, XDc 123) | `static_assert` at BC:2847-2849 forces it on for Silkscreen |
| FREEINK_CAP_FRONTLIGHT_PROBE | switch-on load probe, LED-rail wait, RTC load record, blend carrier + plan, `fault/probing/load/stringMissing/restoreAllowed` | Silkscreen-only (a light that may not be fitted) | 456 lines (BC 2, FMc 399, FMh 55) | the LED-monitor OVP trip (checkMonitor, ~70 lines) is NOT behind it; `static_assert` BC:2813-2814 forces it on whenever the light is on |
| FREEINK_CAP_PROBE_IDENTITY | `touchIdentityCheck()/rtcIdentityCheck()` (BC:3468-3476), FocalTech chip-id read before writes (IMc:143-166,2151), `TouchProbe::Foreign` handling, DS3231 register-pattern check (RTc:117-146) | Silkscreen-only (J6 header may carry another device at 0x38/0x68) | 67 lines | `static_assert` BC:2842-2843 (also forces POWER_POLICY) |
Not capped at all: the front-light gate path (FMc+FMh ~270 lines compiled into every `FREEINK_CAP_FRONTLIGHT` build, gated by `gatedPath(fl)` at runtime), touch sleep (~230, under shared `FREEINK_CAP_TOUCH`), parked-SDMMC mount (~235, under shared `FREEINK_SD_SDMMC`),
BatteryMonitor ladder/cutoff/trend (~380 incl. CLD and BMh), PowerManager release-wake/abnormal-reset/sleepPanelWithoutDriver (~140).

### Q3. New BoardConfig fields (all appended with defaults that keep today's behaviour; MIN = needed by the minimum set)
| Struct.field | bucket | MIN? | SDK reader |
|---|---|---|---|
| DisplayPins.busyPullDown / busyPullUp | POL / PRB | no | EBc:306,310,313; PMc:231,340; FDc:276 |
| SdmmcPins.internalPullups, parkLowWhenOff, powerOffMs | PWR | yes (false / true / 50) | SBc:27,70; SPM:41,62,91; SDc:137; PMc:243; BC:3366 |
| SdmmcPins.maxFreqKhz, powerOnSettleMs | PWR | no (profile restates the defaults 20000 / 20) | SBc:58, SPM:31,63 |
| InputPins.powerExternalPull | INP | yes | IMc:72-73; PMc:31,59,115 |
| AdcLadder*/AdcLadderConfig (oversample 16 differs from default 8) | INP | yes | IMc:307-325,363-433,544-590 |
| ChargeStatusLadder.{pin,count,windows} | PWR | yes | BMc:1008-1051 |
| ChargeStatusLadder.{minIntervalMs,settleAfterBootMs,debounce,completeStable,blinkDelta,oversample} | PWR | yes as fields; profile values equal the defaults | CLD, BMc |
| ChargeStatusLadder.unknownHoldSamples | POL | no | CLD:25,53; BMc:1034,1045 |
| ParkedPin/PinPark/parkedPins[8] | PWR | yes | BC:3349, PMc:255 |
| TouchConfig.optional, fallbackController/Addr | PRB | no | IMc:1655+ |
| TouchConfig.resetPulseMs/resetSettleMs | PWR (PRB use too) | only with touch | IMc:2042 |
| TouchConfig.hibernateInSleep | PWR | only with touch | IMc:1774+ |
| TouchConfig.buttonsAlongsideTouch | INP | only with touch | BC:3239 (consumer reads) |
| TouchConfig.requireFocalTechId | PRB | no | IMc:152,2151 |
| FrontlightConfig.colorSelectGpio, colorSelectWarmHigh, enablePulseUs, offDwellMs | FL-g | yes | FMc:172+ |
| FrontlightConfig.monitorAdc/Scale/Trip | PRB | no | FMc:177 |
| FrontlightConfig.loadProbeWindowMs, railSafeV, loadProbePeriodMs | PRB | no | FMc:815,... |
| FrontlightConfig.colorBlendHz, blendConfirmWindowMs | FL-b (needs PRB) | no | FMc:383,674 |
| SensorsConfig.rtcLowPower | PWR | yes | RTc:148+ |
| SensorsConfig.i2cBusRecovery | PWR | optional | SI:90-114 |
| SensorsConfig.rtcFallbackType/Addr, rtcIdentityCheck | PRB | no | RTc:186+ |
| SensorsConfig.rtcResyncWhenInvalid | POL | no | none in SDK (CrossPoint only) |
| PowerConfig.lowBatteryCutoffMv, cutoffRadioAllowanceMv | POL | no | BMc:872,921,971 |
| PowerConfig.releaseWaitMaxMs | POL | no | PMc:157 |
| PowerConfig.ladderPresenceProbe, powerPinIdleCheck | PRB | no | IMc:316, PMc:83,414 |
| PowerConfig.earlyWakeDecision, abnormalResetLimit, firmwareUpdateMinMv, awakeCeilingMin, radioOffWhenIdle, wakeNoInputTimeoutS, storageErrorSleepS, awakeInputCapMin, noChargeableCellNotice | POL | no | NONE in the SDK (grep); read only by CrossPoint src/lib: 7/4/8/13/27/6/3/12/2 refs |
| DisplayProbeConfig.{forced, ultraChipController, cacheAcrossDeepSleep} / BoardProfile.displayProbe | PRB | no | XDc:327,523,545,604,702 |
| DisplayProbeConfig.x4ClassPanel | POL | no | none in SDK (CrossPoint `xteinkClassPanel()`) |

### Q4. HW-DISPLAY: selected by profile data, X4 unchanged
Evidence: (1) `Ssd1677Config` gains two TRAILING fields `fullUsesHalfWaveform`, `halfUsesFullWaveform`, both `= false` (SSh:74-81); the positional aggregates of the Sticky/Metalio/Picco configs list fewer
initializers, so they stay false. (2) The only code that reads them is `refresh()`: SSc:421 `if (mode==Full && _cfg.fullUsesHalfWaveform) mode=Half;` and SSc:530-536 `if (_cfg.halfUsesFullWaveform) {displayMode|=0x34;} else {<the old two lines + 0xD4>}` - the `else` is the base code. (3) The Silkscreen
values live in `ssd1677SilkscreenConfig()` (SSc:940-973, under `#if FREEINK_DEVICE_SILKSCREEN`): copy of `ssd1677DefaultConfig()` with `booster[4]=0xC0`, `fullSeqOverride=halfSeqOverride=0` (incremental 0x34 path), `halfUsesFullWaveform=true`; it is returned only
by `case BoardConfig::Board::Silkscreen` (SSc:976-979) in `ssd1677ActiveConfig()`, the same board-switch mechanism as Sticky/WS397/Metalio/Picco. (4) The X4 resolves through `default: return ssd1677DefaultConfig()` (or `ssd1677X4Config()` under `FREEINK_X4_FAST_DU_SHORTCUT`), neither sets the new fields (base SSc:929-960).
`fullUsesHalfWaveform` is reachable only through the opt-in `-DSILKSCREEN_SSD1677_X4_SEQUENCES=1` fallback: drop it (4 lines). The values come from the owner's de-link fork (bfe3880, commit 3a42533); whether upstream's DELINK board (default config, base SSc:925-960) has the same under-drive is UNSURE.
The 20 MHz clock is profile data (`.displaySpiHz`, BC:2539) equal to the current default. No EpdBus/FreeInkDisplay change is needed for the SSD1677 panel itself.

### Q5. Does base f9cc1be already contain equivalents? (`git grep` on f9cc1be)
- TPS923610 / enable-gated light: no hits for tps9, adim, colorSelect, enablePulse, enable-gated. Base FrontlightManager = LEDC single/dual (`gpio`+`gpioWarm`), M5PM1 PWM, EEGO LM3630A, Picco I2C. Nothing duplicated; reusable: `apply()`, `physicalDuty`, `perceptualDuty`, `attachChannel`, `hasColorTemperature()` (we extend it).
- Charge-state decoding: base has only the digital `batteryChargeStatus` pin + polarity (BMc base:535-542,675-700,774-784) and I2C chargers/gauges (BQ25896, SGM41562, AXP2101, M5PM1). No analog ladder; reusable: `Status{charging,externalPower*Known}` and the `isExternalPowerPresent(known)` contract (we plug into both).
- Pin parking: base has bespoke holds - `holdPowerRails()` latch pins + `chargeEnable` (BC base:2273-2305) and `powerDownRailsForSleep()`/`holdRailOff` for rst, display/sd/touch/mic enables (PMc base:104-162). `ParkedPin` generalises them; near-duplicate helpers now exist: `holdRailOff` (PMc:173), `driveLevel` (PMc:289), `driveParkedLevel` (BC:3274).
- Li-ion curve: exists (`LIION_NOTCH_MV[11]`, `percentageFromMillivolts` + hysteresis overload); `percentTenthsFromNotches` reuses the table - no duplicate.
- ADC ladders: base `OnePageAdcLadder` hard-codes mV windows (IMc base:182-195) and `XteinkAdcLadder` raw-count ranges (IMc base:48); `AdcLadders` is the data-driven generalisation of OnePage's - a follow-up could express OnePage as data (not for the device PR).
- Touch sleep: base `InputManager::prepareForDeepSleep()` (IMc base:248-300) already has per-controller sleep (EEGO GSLx680, Picco CST); our `prepareTouchForSleep/holdTouchIntForSleep/resetAndHibernateTouch` + `TouchSleepResult` are a second parallel API instead of new branches there.
- SD gating: base `SdmmcBlockDevice::begin` has `powerCycleOnMount` (X4 Pro HIGH->LOW pulse); `parkLowWhenOff` is a different sequence in a new file that copies the host/slot setup. RTC: base already has PCF85063 and DS3231 (WS397).

### Q6. Entanglements and how I would separate them
E1 BoardConfig sub-structs: PowerConfig, FrontlightConfig, TouchConfig, SensorsConfig, DisplayPins each mix 3-4 buckets and the single `SILKSCREEN` designated initializer (BC:2517-2803) names them all. Land each field with the PR that reads it (fields are appended with defaults), land the `SILKSCREEN` profile LAST and trim its lines/asserts (A27: delete the three asserts that force probe/policy caps) to what has landed.
E2 All seven caps default to `FREEINK_DEVICE_SILKSCREEN` (A8) which does not exist upstream until A1: mechanism PRs cannot be Silkscreen-gated first. Options: land A1 (+ empty profile) first; or data-gate (as `gatedPath()` does) and drop the cap; or define the caps default 0 and let the CrossPoint env enable them.
E3 FrontlightManager: `applyGated` (FMc:628-669), `goDark` (754-781), `lightUp` (785-825), `checkMonitor` (845-912) interleave `#if FREEINK_CAP_FRONTLIGHT_PROBE` arms (FMc:633-635,641-651,771-778,789-797,814-823,848-858,889-894,900-910). Gate PR: take them with probe arms removed and `#else` arms kept (`colourPlan()` -> `{false, colourSelect&&warm>=50}` FMc:713-715; `confirmsWhileLit/switchLitToConfirm` stubs); omit `GateGuard`, `_probe`, `_loadRecord`, `FrontlightLoad.h` include. Monitor PR: `checkMonitor` non-probe part + timer + `GateGuard`. Probe PR: A88 + FLL + `restoreAllowed`. Blend CANNOT precede the probe: `colourPlan()` reads `_loadRecord`/`litSinceReset()` and BC:900-914 says "needs FREEINK_CAP_FRONTLIGHT_PROBE".
E4 BatteryMonitor: `pollChargeStatus` (BMc:1008-1051) publishes `disproved ? OnBattery : state` via `externalPowerDisproved()` (BMc:1038-1039, POL, needs `lowBatteryCutoffMv` + `readMillivoltsAveraged`); `isBelowCutoff` consumes `chargeStatus()`. Charge-ladder PR: BMc:1008-1051 with the two lines replaced by `g_chargeState.store(state)`, drop the `unknownHoldSamples` log (BMc:1043-1047) and `chargeStatusLost`. Cutoff/trend PR adds A50-A52.
E5 `InputManager::begin()` (IMc:276-362): POL signature/`startTouch`, PRB `applyLadderPresence()` at IMc:316, INP ladder init 307-325, PWR/POL GT911 INT guard 298-302 (only when `!startTouch`), DGN 290-297. Hand-split by hunk; the INP PR keeps `begin()` signature unchanged.
E6 `beginTouch()` (IMc:1655-1735) is the PRB optional-touch wrapper around the extracted `beginTouchController()` (IMc:1736); the touch-sleep PR needs `touchStarted`, `touchSleepController/Addr`, `cachedGt911Asleep()` which A72 sets. Hand edit: set `touchSleepController/Addr` at `beginTouchController` entry; for a FocalTech-only tail drop GT911 pieces (IMc:114-142 `cachedGt911Asleep/driveGt911IntLow`, `gt911Sleep` IMc:2065-2092, `resetAndHibernateTouch` GT911 loop).
E7 PowerManager: `powerDownRailsForSleep` (PMc:227-276) = BUSY pulls (POL/PRB) + SDMMC hold (PWR) + parked loop (PWR); `deepSleepUntilPowerButton` (PMc:387-409) = PRB `powerPinUndefined` hold + POL release-wake; `deepSleep` BATLOG hook (PMc:363). Adjacent but disjoint lines - split by hunk, no shared lines.
E8 `EpdBus::begin` (EBc:271-318): GEN CS/RST presets + the BUSY chain (POL busyPullDown / PRB busyPullUp / X36). Slim PRs keep base `pinMode(pins.busy, ...)` (base EBc:193); `EpdPins::busyPullDown` + FDc:276 go with POL.
E9 `SDCardManager::shutdown` parked branch (SDc:137-159) calls `syncVolume()` (GEN, SDc:144,159): land A102 first or delete those calls.
E10 `SdmmcBlockDevice::begin` (SBc:27 `if (pins.parkLowWhenOff) return beginParked(pins)` + sdOn/sdOff 96-99,129-131) touches the legacy path every SDMMC board uses; `beginParked` (SPM:30-41) repeats SBc:57-70 host/slot setup - factor one helper before upstreaming.
E11 XteinkDetect.cpp: the 368 block (XDc:35-79) is an `#if 368 ... #elif PROBE` chain head; dropping X36 restores `#if FREEINK_XTEINK_DISPLAY_PROBE` (base XDc:28). PRB hooks sit inside `probeDisplayController` and `applyXteinkDisplayController` behind `#if CAP_DISPLAY_PROBE_POLICY`; the `#else` side is base code.
E12 `sleepPanelWithoutDriver` (PMc:298-360): POL body + PRB `ultraChip` arm + X36 `#if` at 315-320,332-336.
E13 SharedAdc/SharedI2c/ProbeChecks live in the BoardConfig lib and are included by BatteryMonitor, InputManager, FrontlightManager, PowerManager, Rtc, Imu, EnvironmentSensor; each include needs host-test stub edits (1c). Ship SharedAdc as its own generic PR; split ProbeChecks.h per consumer (ladder 13 / FocalTech 7 / DS3231 34 / pin level 10 lines).
E14 Rtc.cpp: `beginDevice()` (RTc:146-185) restructures base `begin()`'s switch (`break`->`return`) to host low-power (PWR), identity (PRB) and fallback (PRB). For an independent low-power PR apply the ~14 lines inside the existing `begin()` switch instead of the restructure.
E15 `holdPowerRails()` (BC:3345-3370) hosts parked pins, SDMMC park and base latch/charger code; order matters (parked first, hold released after the boot level).
Suggested slicing (each with host test + docs, one concern): G1 SharedAdc (+ stubs); G2 `holdRailOff`/EpdBus CS+RST presets; G3 `syncVolume`; G4 Opds abort; M1 ADC ladders; M2 pin parking + external-pull power key; M3 charge-status ladder; M4 parked SDMMC; M5 enable-gated front light; M6 I2C recovery + RTC low-power; M7 touch hibernate (as branches of `prepareForDeepSleep`); then D1 device registration + profile + SSD1677 config; PROBES/POLICY/BATLOG/DIAG as separate, optional, mostly consumer-policy PRs that SCOPE.md likely rejects.
Dependency sketch: G1 (SharedAdc) -> M1, M3 (and FL monitor, BATLOG); M2 -> M4 (`driveParkedLevel`), M5 (gated `begin()` uses `driveParkedLevel`), `holdRailOff` preset (G2) is wanted by M2; G3 -> M4 (`syncVolume` calls) or drop the calls; M6 independent; M7 needs M6 (`beginBus`) and, for the shared bus clock, A37; D1 last (its asserts and profile fields name everything above).

### Q6b. GENERIC-FIX candidates - which bug, how an upstream device would hit it
- G1 SharedAdc (A36 + swaps A48/A68/A69/A71) - WEAK as a stand-alone fix. Bug: IDF `adc_oneshot_read` try-locks the unit and returns ESP_ERR_TIMEOUT when busy; Arduino `__analogRead` ignores it and returns 0; per-pin init and calibration-handle creation in `__analogRead*` are unlocked (esp32-hal-adc.c:295-372). Reach: needs two ADC readers on different tasks/cores. On upstream today the X4/X3 (single-core C3) can only collide in the few instructions between lock acquire and the `rtc_spinlock` critical section inside `adc_oneshot_read` (GUESS), and S3 boards with an ADC (X4 Pro, Murphy M4) read only the battery - so exposure upstream is latent; it is a prerequisite of Silkscreen's ADC users (key + charge poll on the loop task, battery % from the render task, LED monitor on the esp_timer task, BATLOG). Ship it with M1/M3, or gate it so C3 pays nothing.
- G2 `holdRailOff` preset (A93). Bug: `pinMode(OUTPUT)` turns the driver on at whatever GPIO_OUT holds (0 after reset) before `digitalWrite(offLevel)`, so an off level of HIGH (active-LOW enables: X4 Pro SD GPIO5, Murphy M4 SD GPIO10 / touch GPIO45, Picco GPIO0, display RST sleep level HIGH) glitches LOW at each sleep entry. Benign-looking, owner's claim; no measurement.
- G3 EpdBus CS/RST preset (A111). Bug: CS left LOW/floating while SCLK is live; RST driven LOW (output register 0) between `pinMode` and the reset pulse. Hit by boards with no CS pull-up (base profile data does not say which); the comment evidence is the Silkscreen profile ("EPD CS has no pull-up", BC:2735-2738 region).
- G4 `SDCardManager::syncVolume` (A102). Bug: base `shutdown()` comment says `FsVolume::end()` "flushes SdFat's cached FAT/directory sectors" (base SDc, `shutdown()`); in SdFat 2.3.1 it only nulls pointers (FsLib/FsVolume.h:120-126), so a sector dirtied by an op that did not sync, or a file still open for write, is lost at sleep/MSC handoff (clean closes are safe: `close()` syncs). Hit by every SD board at `shutdown()`/USB-MSC `detachFilesystemForRawAccess()` while a writer is open. Best candidate for a stand-alone PR.
- G5 Opds implicit-login abort (A118). Bug: `opdsImplicitAuthenticate` runs up to `MAX_STEPS=10` requests with `AUTH_TIMEOUT_MS=30000` through its own `SecureHttpClient` and never consults any abort, so Back/low-battery/app shutdown cannot interrupt a Cantook/Lirtuel login for minutes (base OpdsImplicitAuth.cpp:15-18,66). Hit by any device using OPDS implicit OAuth; the SDK hook is generic, the consumer must override `OpdsTransport::shouldAbort()` (CrossPoint-side, ~5 lines).
- G6 comment fix (SBc:55-57): base said `SDMMC_FREQ_DEFAULT` is "40 MHz"; it is 20 MHz (20000 kHz). Cosmetic; fold into G3/G4.
- Not generic despite looking like it: `powerExternalPull` ext1 handling (needs a board with an externally biased wake pin), `rtcLowPower`, `i2cBusRecovery`, touch hibernate, parked pins (they are mechanisms only a bench-validated board needs).

### Q7. Risk register - changes that touch OTHER boards (even slightly)
| ID | where | what changes | assessment |
|---|---|---|---|
| R1 | SA:20-57; BMc:738,753,756; IMc:308,314,331,408,411,477,516,523 | every ADC read (battery, X4/X4PRO ladder polls, OnePage ladder) now takes a recursive FreeRTOS mutex; static `StaticSemaphore_t` (about 80 B, GUESS) created on first use; new `semphr.h` include | low; on single-core C3 a collision window is only the few instructions around `adc_oneshot_read`'s critical section (GUESS), so on C3 it is mostly cost without benefit - consider gating by SoC. Owner's commit 720da26 says "The ADC lock stays" in C3 builds |
| R2 | PMc:177 | `holdRailOff` writes the output register before enabling the driver, for rst/display-en/sd-en/touch-en/mic-en at every sleep entry | low; removes a possible LOW glitch; GENERIC-FIX candidate |
| R3 | EBc:274-279,297-303 | EpdBus::begin: CS driven HIGH + hold released before `SPI.begin`; RST driven HIGH (was output at its stale/0 level) and hold released last, on every board | low-medium: pin states at boot change for all panels (OnePage's shared GPIO27 RST becomes HIGH earlier); needs a bench on one non-Silkscreen board |
| R4 | SDc:92,157-159,315,330-343; SDh:128-153 | `shutdown()` and USB-MSC `detachFilesystemForRawAccess()` now sync SdFat's cache (opens root, `sync()`); SPI `shutdown()` goes from empty inline to a function (X3/X4 C3 too) | low; one extra root open + sync before sleep/MSC; the premise holds in SdFat 2.3.1 as vendored in the owner's crossink libdeps: `FsVolume::end()` only nulls pointers (FsLib/FsVolume.h:120-126); upstream's SdFat pin not checked |
| R5 | SBc:96-99,129-131,57,70 | legacy SDMMC mount: gate levels derive from `powerActiveHigh` (was hard-coded HIGH off / LOW on); clock/pull-up from profile | none today: every gated SDMMC board is active-LOW (Murphy M4 base BC:1192, X4 Pro 1812, X4 Classic 1956, Picco 1732 = no cycle); de-link, WS397, Paper Mono, Metalio have no SD gate; `SDMMC_FREQ_DEFAULT` is 20 MHz (the old "40 MHz" comment was wrong) |
| R6 | IMc:1767,2130,2485,2904,3061 (existing touch begin paths); RTc:79; Imu.cpp:95; EnvironmentSensor.cpp:39; SI:17-18 | `Wire.begin` -> `beginBus(..., busHz())` (sensor-bus clock wins when touch and sensors share pins); new includes `esp32-hal-periman.h`, `esp_rom_sys.h` | none by data: every base profile sharing pins has equal rates (EEGO 2/1 @400k, Paper Mono 47/48 @100k, 39/40, 41/42, 39/38 @400k); compile dependency on Arduino-ESP32 3.x for all libs using it |
| R7 | BC:1233-1245 and sub-struct fields | `BoardProfile` grows ~215 B (GUESS, from field layout: parkedPins 24, adcLadders 80, chargeLadder 36, displayProbe 4, sub-struct fields ~70); `ACTIVE` is a RAM copy on every board, each compiled profile adds the same to rodata | measure with `pio run -t size` on x4/x3 (C3) before/after |
| R8 | FMc:141-241,299-377,606-825,1305-1334; FMh:54-65,291-368 | whole enable-gated path, `GateGuard`, esp_timer monitor are compiled into every `FREEINK_CAP_FRONTLIGHT` board (S3 only), dead unless `gatedPath()`; `FrontlightManager` gains ~28 B members | flash GUESS 2-4 KB on X4 Pro/Murphy M4/Picco/de-link; no C3 impact (no frontlight) |
| R9 | BMc:846-1058; BMh:2,95-154; BMc:7 | charge-ladder sampler, cutoff, trend state, `std::atomic`s and `Logging.h` dependency compiled into every ADC-battery board (incl. C3 X4); dead unless referenced | GUESS: unreferenced functions/statics dropped by `--gc-sections`; the `Logging.h` dependency is new for BatteryMonitor (host stub needed) |
| R10 | PMh:34-37 / PMc:150; PMc:69-71,409,6; IMh:27 | `waitForPowerButtonRelease()` void->bool; 3 new `RTC_NOINIT` words; `releaseWakeMagic=0` each sleep; unconditional `<driver/rtc_io.h>`; `InputManager::begin(bool=true)` | API-compatible; confirm `rtc_io.h` compiles on C3 (owner has a `c3check` env in crossink) |
| R11 | IMc:1655-1773 | `beginTouch` restructured for all touch boards: `!optional && fallback==None` goes straight to `beginTouchController()`; `touchStarted`, `touchSleepController/Addr` set; `beginFt5x06` attempts 12 unless a reset ran | none by inspection (Paper Mono has no reset pulse) |
| R12 | SSc:421,530-536 | two runtime checks on config flags | none (Q4) |
| R13 | SPM:1-104 | compiled into every `FREEINK_SD_SDMMC` board | dead unless `parkLowWhenOff`; ~100 lines |
| R14 | OP: OpdsTransport.h:61-65, OpdsImplicitAuth.* | new virtual (vtable +1 slot), signature gains defaulted param, `aborted()` check after each login request | none with a null callback |
| R15 | RTc:146-213 | `begin()` restructured, `now()/set()` use captured `addr_/type_` | same result for Rx8130/Pcf8563/Pcf85063/Ds3231 by inspection; `rtcLowPower` default false |
| R16 | FDc:68-72 | ungated `static_assert` coupling two enums | compile-time only |
| R17 | BC:180-190 | `FREEINK_DRIVER_UC8279_X4` split from UC8179 | same truth values for X4/X4PRO/X4CLASSIC |
| R18 | BC:3034-3040 | `DEFAULT_DEVICE` picks Silkscreen first in a multi-device S3 build | only if Silkscreen is combined with other S3 devices |
| R19 | XDc:308-318; README rows | comment/doc edits ("every benched UC81xx" -> "X4 Pro UC81xx") | none |

### Q8. Hygiene vs coding-standards.md (top 10)
1. Comment style ("usually 1-2 lines", no investigation history): BC:2438-2460 (23-line header), 2566-2577 (touch narrative incl. "UNVERIFIED guess carried over from the first port"), 2606-2623 (front-light narrative), 2690-2709 (power rationale), FMh:15-53 (39 lines), SSc:944-966 (de-link/bfe3880 history), BMh:95-110; 1,529 of 7,033 added source lines (22%) are full-line comments, BoardConfig.h 41%.
2. References the SDK cannot resolve: "silkscreen-pcb docs/HARDWARE.md 13/13.1", "POWER.md ZD-01/PB-02", "scripts/check_sleep_wake_sources.sh", "docs/silkscreen/MODULARITY.md" - BC:2440,2455,2829; PMc:112-114.
3. Raw `Serial.printf` instead of LOG_*: EBc:111,491; SDc:340; XDc:70,564,581,597,607 (base files already mix both); `esp_restart()` for recovery inside the bus layer EBc:116 (rule: restart only for explicit recovery such as OTA).
4. `std::function` in a library API: OP OpdsImplicitAuth.h:3,19-20, OpdsImplicitAuth.cpp:58-59,162-163, lambda at OpdsClient.cpp:231 (base `SecureHttpClient::AbortCallback` is itself a std::function, so at least reuse that typedef).
5. Templates in library headers: CLD:15-95 `ChargeLadderDecoder<Window>`, ALD:21-35 `decodeAdcLadder<Key>` (one instantiation each; rule asks for explicit instantiation or non-template).
6. Style departure: `SILKSCREEN` uses designated initializers (BC:2517-2803) while the 18 base profiles are positional with comments; alias constants `ssd1677`/`SSD1677` etc. (BC:2490-2495).
7. Undefined macros used bare in `#if` (-Wundef): `SILKSCREEN_BATLOG` (PMc:20,363), `FREEINK_ADC_LADDER_TRACE` (IMc:35); mixed `#if FREEINK_DIAGNOSTICS` / `#if defined(FREEINK_DIAGNOSTICS) && FREEINK_DIAGNOSTICS`.
8. Duplicated helpers: `powerPinMode()` IMc:72 and PMc:30; `driveLevel` PMc:289 / `driveParkedLevel` BC:3274 / `holdRailOff` PMc:173; `sensorWire()` SI:25 vs Rtc/Imu/EnvironmentSensor copies; SPM:30-41 vs SBc:57-70.
9. Test/bring-up API in production headers, ungated: IMh:373-401 `TouchMapping/setTouchMapping` (mutates `ACTIVE.touch`), RTh `address()/type()`, SI:116-159 `scanSensorBus`, FDc:68-72 static_assert.
10. Layering/naming: SharedAdc.h/SharedI2c.h/ProbeChecks.h sit in the BoardConfig lib but pull Arduino/FreeRTOS/Wire; `Logging.h` newly included by BatteryMonitor (BMc:7) and SdmmcParkedMount.cpp (SPM:9); `BatteryMonitor.h` now includes `BoardConfig.h` (BMh:2); `SdmmcParkedMount.cpp` defines `SdmmcBlockDevice::beginParked` (file name != class); PowerConfig carries 9 consumer-only policy fields with no SDK reader (SCOPE.md: downstream policy is out of scope).

## 4. NOT COVERED
- Nothing was built, size-measured or run: every flash/RAM figure (R1, R7-R9, `--gc-sections` claims, `BoardProfile` +215 B) is a GUESS from source; run `pio run -t size` on x4/x3/x4pro/murphy-m4 before/after to settle them.
- BatteryLogCore.h (942), BatteryLog.cpp (356), BatteryLog.h (137) were not read in depth (hooks, `#if` envelope, ADC lock use and sizes only); FrontlightManager.cpp 1070-1206 (rail wait, record) and the DIAG bodies were counted, not read line by line.
- Test files (57) were classified and sized, not reviewed; only the stub diffs for Haptic/Imu/SDCard were read. docs/README text was read at heading level only.
- Not checked: whether upstream's DELINK board has the SSD1677 under-drive (Q4); whether other upstream hosts want the ADC lock on C3; the CrossPoint-side consumers beyond a few greps (adcLadderContact in lib/hal/HalGPIO.cpp:195, percentTenths in HalPowerManager.cpp:383, checkPowerPinIdle/sleepPanelWithoutDriver in src/main.cpp).
- Merge-commit conflict resolutions were not examined separately (the net diff was analysed). Per-hunk commit lineage is only given at group level below.
- Line-range splits inside mixed hunks (A18-A21, A44, A51, A61-A62, A68, A71, A75, A76-A89, A91-A100, A107, A112, A117) are approximations (+-15%).

## Appendix: commit lineage of the 77 non-merge commits (bee6382 side)
DEV/PWR/INP/FL port: 771de19 register+profile; abca595 parking+parked SDMMC+ladders; 82fe8ab front-light gate+monitor; a26e398 optional touch+touch sleep+I2C; 983f2a7 power policy; 720da26 port review (sleepPanelWithoutDriver, resetAndHibernate, cap macros, policy fields); 097bcfd/5edb074/c72a825/e2e3403 docs; 923f61d/0362c06/f613450 review fixes.
DIAG: 56305b9 0f93306 26edaee 507a7de 884be30. PROBES: 0c34a6d panel probe; b7b00c8 + 65660a6 front-light load probe; 3ee11bc key/power/touch/RTC boot checks; 1f7f487 probe-policy assert. HW-DISPLAY: 9cb3174 3a42533. Blend: dae5523 468b8e2. Tenths: bf196c8.
BATLOG: eaee143 2c050e5 a5bd897 f21d738 43bf8b1 bb52290 4344fb9 d5dd6a7. ext1 RTC pulls: 8b87038 + 0b60346. OPDS stop request: 6f87cbd.
X368 (38 commits): c875022 757cf9f b1f55e2 01740b4 188d4fa f5e6138 9f84911 f64b0d2 ab1d64b 4fa3c1f 0e53723 b442dab 381fcf8 ba282b0 74362e1 4aea0cb c689ad0 922cd58 d618ab9 9d97f61 a2bde17 48be3d0 4988be4 8e65a89 a4afcf8 6bb55f3 49462bf 664369b 96c996e d2e1d19 5fb944d 2aa010a 9c8726a b460001 37302d6 336ec77 9870b83 92738df.
