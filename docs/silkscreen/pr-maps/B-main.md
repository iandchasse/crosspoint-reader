# B-main: hunk-level map of the CrossPoint core (main.cpp, platformio.ini, util, small libs) at d076703a vs upstream/develop 0fcdb3ec

Basis: `git diff/show/grep/blame` on d076703a and upstream/develop only; SDK facts by object reads of bee6382 (ours) and f9cc1be (upstream pin).
No build, no tool run, no working-tree file content read (directory listings only). Gating statements are from reading the source, not from disassembly; flash figures are not measured (marked GUESS).
Line numbers are d076703a unless "up:" (upstream). `+a/-b` = added/removed lines of the hunk.

Gating codes: `G(pp)` = `#if FREEINK_CAP_POWER_POLICY` (SDK default = FREEINK_DEVICE_SILKSCREEN, BoardConfig.h:399); `G(fp)` = `#if FREEINK_CAP_FRONTLIGHT_PROBE && !CROSSPOINT_TEST_SUITE`;
`G(dp)` = `#if FREEINK_CAP_DISPLAY_PROBE_POLICY`; `G(bl)` = `#if SILKSCREEN_BATLOG`; `G(ts)` = `#if CROSSPOINT_TEST_SUITE` or `#if !...`; `G(dev)` = inside an `#if ... FREEINK_DEVICE_SILKSCREEN`.
`FOLD` = source is ungated but reads `BoardConfig::powerPolicy().<field>`; without FREEINK_CAP_POWER_POLICY that is `constexpr PowerConfig{}` (BoardConfig.h:3453-3457), so the guard is a compile-time 0 and the compiler drops the body (code is still compiled; not checked in an ELF).
SDK legend for the Requires column: `SDK-PP` = FREEINK_CAP_POWER_POLICY plus the named `PowerConfig` field (Silkscreen profile: cutoff 3400 mV, releaseWait 5 s, abnormalResetLimit 3, awakeCeiling 15 min, wakeNoInput 180 s, storageError 60 s, awakeInputCap 60 min, BoardConfig.h:2714-2724); `SDK-CL` = FREEINK_CAP_CHARGE_LADDER; `SDK-FP` = FREEINK_CAP_FRONTLIGHT_PROBE (needs FREEINK_CAP_FRONTLIGHT); `SDK-DP` = FREEINK_CAP_DISPLAY_PROBE_POLICY. main.cpp keys only on PP (14 `#if`s), FP (2), DP (2), SILKSCREEN_BATLOG (19), CROSSPOINT_TEST_SUITE (17, 4 of them compound with PP/FP) and one FREEINK_DEVICE_SILKSCREEN; `#if` count 16 upstream -> 67.
`RT` = runtime test of mutable `BoardConfig::ACTIVE` (`inline BoardProfile ACTIVE`, BoardConfig.h:3099), code present in every build. `UNG` = ungated, changes every board (what is said in the row).

## 1. Bucket summary

main.cpp is +814/-50 (1110 -> 1874 lines, +69%). "G" = lines inside a board/feature `#if`; "U" = lines compiled on every board (FOLD/RT/UNG; of the 383 U lines, 147 are comments or blanks, 236 code, and most U code folds to nothing off-Silkscreen).

| Bucket | main.cpp +/- (G / U) | platformio.ini | other files in my scope (+/-) | Notes |
|---|---|---|---|---|
| DEV-ENV | 0 | 73 lines (563-588, 606-652) | ci.yml +2; FirmwareBoardTag.cpp +2 (outside list) | the whole minimal device = this + DEV-WIRING; commit 79c214a4 is exactly that |
| DEV-WIRING | +6/-1 (6 G) | - | - | B51: 1 code line, 5 comment lines |
| POWER-SAFETY | +205/-1 (75 G / 130 U) | - | util cutoff poll (PluginEvents +24/-7 .h +3/-1, PluginHttp +19/-16 .h +7/-4) = +53/-28 U; CutoffAbortPrint.h +26 (outside list); english.yaml +2; tests +40 | about 20 cutoff-poll call sites in 13 files, see A4 |
| POWER-BUTTON | +97/-30 (0 G / 97 U) | - | USER_GUIDE +3 | 30 of the 97 are comments; 30 removed lines are renames of upstream X4 Pro code |
| FRONTLIGHT | +107/-8 (67 G / 40 U) | - | english.yaml +3 | B9, B21, B53a, B57-else are ungated and run on every board with a light |
| KEYACCESS | 0 | - | USER_GUIDE +1; tests +54 (selection harness, dictionary) | TOGGLE_FRONTLIGHT short press (B70) is counted PB |
| RADIO-AWAKE | +115/-9 (0 G / 115 U) | - | - | 48 of 115 are comments; ungated with a runtime effect: B14, B17, B22b, B58a, B63 (plus trivial statics B8, B47 and the hoist B60); the rest FOLD |
| BATTERY-DISPLAY | 0 | - | english.yaml +1 (STR_NO_BATTERY); USER_GUIDE +1 | 5% steps live in lib/hal/HalPowerManager.cpp (not mine) |
| WATCHDOG | +88 (G) | - | RenderWatchdog.h +168, .cpp +343 (about 69 of them TS-only P9 hooks); Logging.cpp +16, Logging.h +4 | plus hooks in ActivityManager.cpp, HomeActivity.cpp, EpubReaderActivity.cpp (outside list) |
| BATLOG | +116 (G) | 26 | BatteryLogWriter.h +35, .cpp +161; TrustedTime.cpp +10/-1 | |
| TESTSUITE | +46 (G) | 55 | PersistableStore.cpp +7; ci.yml +8; check_test_firmware.sh +95 (LOCAL-TOOLING); FirmwareBoardTag +3 | |
| X368 | +1/-1 (U) | 146 | ci.yml +10; FirmwareBoardTag +7; webserver doc clause | ignored per brief |
| GENERIC-FIX | (B53a is a candidate: 10 lines U, counted FRONTLIGHT) | - | USER_GUIDE +1 (line 320); webserver-endpoints.md +1/-1 | both docs |
| LOCAL-TOOLING | 0 | - | .gitmodules +2/-2; ci.yml +3 (36-38); check_sleep_wake_sources.sh +101 | never upstream |
| UNSURE | +33 (G(dp)): B33 panel-controller log | 17 (589-605: display_flags/touch_flags override hooks) | - | diagnostic only |

Totals check: main.cpp buckets 205+97+115+107+88+116+46+33+6+1 = 814 added; removed 1+30+9+8+1+1 = 50. platformio.ini 73+17+26+55+146 = 317.

## 2. main.cpp function map (d076703a)

| Region | Lines | Contents by hunk ID |
|---|---|---|
| includes | 1-67 | B1 PS, B2 WD, B3 TS, B4 BL |
| globals, consts | 79-167 | B5 PB(rename), B6 PB, B7 PS, B8 RA |
| front-light trip + notices | 168-251 | B9 FL, B10 FL (G(fp)) |
| armSilentReboot | 275-282 | B11 BL |
| handlePowerDoubleClickLight | 373-392 | B12 PB (renamed upstream handleX4ProFrontlightDoubleClick) |
| sleep-frame save/load | 464-487 | B13 TS wrapper |
| deliverSleepPluginEvents | 489-561 | B13 TS wrapper, B14 RA, B15 PS |
| BATLOG helpers | 599-642 | B16 |
| enterDeepSleep | 643-736 | B17 RA, B18 WD, B19 TS, B20 BL, B21 FL, B22a TS, B22b RA, B23 TS, B24 BL, B25 PB |
| sleep helpers block | 738-997 | B26-B28, B30, B34, B35 PS; B29, B31, B32 WD; B33 UN; B36 BL |
| setupDisplayAndFonts | 996-1023 | B34 (header moved) |
| setup | 1025-1410 | B37-B54 |
| loop | 1412-1874 | B55-B71 |

## 3. main.cpp hunk table

Columns: ID | d076703a lines | +/- | purpose | bucket | gating | requires | entanglement; first commit(s) from `git blame`.
`HAL:` = CrossPoint lib/hal; `SDK:` = freeink-sdk; `APP:` = another app file. Rows marked (m) rewrite or move upstream lines; all others are pure insertions.

| ID | Lines | +/- | Purpose | Bucket | Gating | Requires | Entanglement / commit |
|---|---|---|---|---|---|---|---|
| B1 | 2, 20, 28 | +3 | `#include <BatteryMonitor.h> <PowerManager.h> <esp_system.h>` | PS | UNG (headers; libs already in base lib_deps) | - | used by B27-B35; BL logs `esp_reset_reason`; 7f2450c4, b86e9d49 |
| B2 | 56-58 | +3 | include RenderWatchdog.h | WD | G(pp) | - | with B18,B29,B31,B32,B37,B41,B52,B55; 009130ce |
| B3 | 59-62 | +4 | include TestSuite.h, RenderWatchdogTest.h | TS | G(ts) | src/activities/testsuite/** | d21dde9d |
| B4 | 63-67 | +5 | include BatteryLog.h, BatteryLogWriter.h | BL | G(bl) | SDK BatteryLog.h | b4a2ee87 |
| B5 (m) | 86, 89-90 | +3/-3 | rename `lastX4ProPowerClickAt`->`lastPowerClickAt`, `X4PRO_POWER_*`->`POWER_*` | PB | UNG rename, no behavior change | - | same rename in B12, B69; 36ee5cfc |
| B6 | 149-157 | +9 | `RTC_NOINIT sleepWakePolicy`, WAKE_POLICY_HOLD/CLICK, `settingsLoaded` | PB | UNG: 4 B RTC slow RAM + 1 B .bss on every board | SDK: PowerConfig::earlyWakeDecision | read by B25, B43a, B46a; settingsLoaded also by B16 BL, set by B48; b86e9d49 |
| B7 | 158-161 | +4 | `HEALTHY_UPTIME_MS` (60 s) | PS | UNG constexpr | PowerConfig::abnormalResetLimit | used only by B59; b86e9d49 |
| B8 | 162-167 | +6 | `wakeNoInputArmed`, `storageErrorScreen` | RA | UNG (2 B .bss) | PowerConfig::wakeNoInputTimeoutS, storageErrorSleepS | set B47, B50; cleared B58a; read B64, B65; 892c4f19 |
| B9 | 168-187 | +20 | `handleFrontlightTrip()`: tripped light is switched off and saved off | FL | UNG: runs every loop pass (B57 else arm) and at sleep (B21) on every board; returns at `!present()` / `!fault()` | HAL: Frontlight present/update/fault/switchOnPending; SDK: FrontlightManager::fault (absent upstream f9cc1be); writes SETTINGS.frontlightOn + saveToFile | called by B10, B21, B57; f25a8651, 892c4f19 |
| B10 | 188-251 | +64 | `showFrontlightNotFound`, `handleFrontlightNotices` (trip / no-light / restore notices), `keepWarmthOnPresentString` (saves warmth on the present string) | FL | G(fp) | SDK-FP; HAL takeNoLightNotice, takeRestoreNoLightNotice, stringMissing; APP: Activity::showsFrontlightNotice (+ActivityManager), `GUI.drawWrappedPopup` (BaseTheme.cpp:796, fork-only), STR_FRONTLIGHT_NOT_DETECTED; 2 new `SETTINGS.saveToFile()` sites (3 with B9) | `&& !CROSSPOINT_TEST_SUITE` in the `#if` (188) is a TS token; a2129062, f25a8651, 86e1e6ea |
| B11 | 276-278 | +3 | `batterylog::carry()` in armSilentReboot | BL | G(bl) | SDK BatteryLog | b4a2ee87 |
| B12 (m) | 373-374, 379-380, 384-385, 389 | +7/-7 | `handleX4ProFrontlightDoubleClick` -> `handlePowerDoubleClickLight`; `isX4Pro() && doubleClickPwrLight` -> `SETTINGS.powerDoubleClickLight()` | PB | UNG: X4 Pro identical except the extra `shortPwrBtn != TOGGLE_FRONTLIGHT` term | APP: CrossPointSettings::powerDoubleClickLight/boardHasPowerDoubleClickLight (CrossPointSettings.cpp:67-78) | edits upstream X4 Pro code; 36ee5cfc |
| B13 | 466, 473, 489, 561 | +4 | `#if !CROSSPOINT_TEST_SUITE` around saveSleepFrameBuffer and deliverSleepPluginEvents | TS | G(ts) | - | wraps upstream functions; d21dde9d |
| B14 (m) | 499-502, 507-514 | +12/-2 | `deliverSleepPluginEvents(unattended, allowNetwork)`; unattended sleep emits and delivers nothing | RA | UNG (param is always false off-Silkscreen) | caller B22b / B64 | b605f87d |
| B15 | 530-534 | +5 | no network work on the way down once cutoff / ceiling / cap passed | PS | UNG: one call per sleep entry into HalPowerManager::backgroundNetworkMustStop (false off-Silkscreen) | HAL backgroundNetworkMustStop (HalPowerManager.cpp:237) | 4505ceec, 5d58fdc0 |
| B16 | 599-642 | +44 | `stageBatteryLogEntry`, `tickBatteryLog` | BL | G(bl) | SDK BatteryLog; HAL batteryCachedTenths, atFullClock; BatteryLogWriter | reads `unattended` (B17), `settingsLoaded` (B6), Frontlight state; b4a2ee87 |
| B17 (m) | 643-645 | +3/-2 | `enterDeepSleep(bool fromTimeout, bool unattended)` signature + comment | RA | UNG | - | needed by B14, B16, B64; b605f87d |
| B18 | 646-650 | +5 | `renderwatchdog::beginSleepEntry()` first statement | WD | G(pp) | WD files | 97f042ae |
| B19 | 651-654 | +4 | P9 hook `hangSleepEntryIfArmed` | TS | G(ts) | testsuite | 97f042ae |
| B20 | 655-657 | +3 | `stageBatteryLogEntry(...)` call | BL | G(bl) | B16, B17 | b4a2ee87 |
| B21 | 659-664 | +6 | `handleFrontlightTrip(); Frontlight.prepareForSleep();` | FL | UNG: every board; SDK prepareForSleep returns early unless the light is "gated" (FrontlightManager.cpp:1305-1308) | SDK: FrontlightManager::prepareForSleep (absent upstream); HAL prepareForSleep | B9; b605f87d, 892c4f19 |
| B22a | 667-671, 681 | +6 | `#if TS ... #else` wrapping upstream's trustedtime::note / Bluetooth stop / deliver, `#endif` | TS | G(ts) | - | wraps up:464-472 |
| B22b (m) | 680 | +1/-1 | `deliverSleepPluginEvents(unattended, bluetoothStopped)` | RA | UNG | B14 | 552f0c3d (merge) |
| B23 | 695-697, 704 | +4 | TS arm around the Quick Resume save | TS | G(ts) | - | wraps up:486-491 |
| B24 | 705-708 | +4 | `BatteryLogWriter::flushAtEntry()` | BL | G(bl) | BatteryLogWriter | b4a2ee87 |
| B25 | 731-734 | +4 | store the wake rule (HOLD / CLICK) in RTC before sleeping | PB | FOLD (earlyWakeDecision) | B6; SDK-PP | b86e9d49 |
| B26 | 738-739 | +2 | forward decl `resolvePanelController()` | PS | UNG | - | needed by B29, B30, B35; 6d8fe5ac |
| B27 | 740-764 | +25 | `checkPowerButtonIdle()`: logs the power pin's idle level | PS | G(pp) | SDK: PowerManager::checkPowerPinIdle, PowerPinIdle (absent upstream) | called by B39; 16374791 |
| B28 | 765-805 | +41 | `haltOnPowerButtonFault()`: popup, wait for USB, sleep with no wake source | PS | G(pp) && !TS | SDK: PowerManager::powerPinCheck, BatteryMonitor::pollChargeStatus; STR_POWER_BUTTON_FAULT; GUI.drawWrappedPopup | calls setupDisplayAndFonts (fwd decl 766); called by B46d; 16374791 |
| B29 | 806-821 | +16 | `sleepWithoutDrivers()`: resolve panel, reset+sleep panel and touch without drivers, minimal deep sleep | WD (shared with PS reset loop) | G(pp) | SDK: PowerManager::sleepPanelWithoutDriver; HAL resetTouchForSleep, startDeepSleepMinimal | called by B31, B32; 009130ce |
| B30 | 822-842, 848-861 | +35 | `sleepIfResetLoop()`: after N abnormal resets sleep before SD / touch / panel / radio start; 849-858 is a copy of the pre-watchdog inline sequence in a dead `#else` | PS | FOLD (`limit == 0` returns first); the `#else` arm is compiled only off-Silkscreen, dead | SDK-PP.abnormalResetLimit; PowerManager::takeAbortedSleepInfo (upstream has it), countAbnormalResets, clearAbnormalResets, sleepPanelWithoutDriver; HAL resetTouchForSleep, startDeepSleepMinimal | B31 hooks in; PS-only text = version at 009130ce^ (main.cpp:799-824); b86e9d49, 892c4f19 |
| B31 | 843-847 | +5 | `renderwatchdog::resetLoopSlept(); sleepWithoutDrivers();` | WD | G(pp) | B29 | inside B30 |
| B32 | 862-898 | +37 | `sleepIfRenderWatchdog()`, `logWatchdogReport()` | WD | G(pp) | WD files; Logging getLastLogs | 009130ce, 97f042ae |
| B33 | 899-928, 941-943 | +33 | `logPanelController()` (probe result log) + its call | UNSURE (diagnostic) | G(dp) | SDK: freeink::displayControllerSource, BoardConfig::controllerPartName (absent upstream) | inside B34's function; 6d8fe5ac |
| B34 (m) | 929-930, 946, 996-997 | +5/-1 | extract the panel-controller probe into `resolvePanelController()`; upstream's 12 lines (up:518-529) stay in place as its body; `setupDisplayAndFonts` calls it | PS (enabler) | UNG, behavior-preserving | - | needed by B29, B30, B35; 6d8fe5ac |
| B35 | 948-988, 992-995 | +45 | `sleepIfBatteryEmptyAtBoot()`: quick average, slow path (wait divider/ladder, N readings), re-sleep before SD / panel; non-static | PS | FOLD (`cutoffMv == 0`) | SDK-PP.lowBatteryCutoffMv; BatteryMonitor::readMillivoltsAveraged, CUTOFF_SAMPLES, pollChargeStatus, isBelowCutoff; BoardConfig::hasChargeStatusLadder + chargeLadder.settleAfterBootMs (SDK-CL) | B36 inside; called by B45; 7f2450c4 |
| B36 | 989-991 | +3 | `batterylog::early(Cutoff)` | BL | G(bl) | - | inside B35 |
| B37 | 1027-1033 | +7 | hold the log ring across a watchdog restart | WD | G(pp) | Logging holdLastLogs | `watchdogLogsHeld` read by B52; 97f042ae |
| B38 | 1047-1053, 1057 | +8 | release-wake: a wake from a held-button sleep that asks for nothing sleeps again | PS | FOLD (releaseWaitMaxMs) | SDK: PowerManager::takeReleaseWake; HAL startDeepSleepMinimal | b86e9d49 |
| B39 | 1054-1056 | +3 | `if (powerPinIdleCheck) checkPowerButtonIdle();` | PS | G(pp) | B27 | 16374791 |
| B40 | 1073, 1077 | +2 | `sleepIfResetLoop();` (+blank) | PS | FOLD | B30 | b86e9d49 |
| B41 | 1074-1076 | +3 | `sleepIfRenderWatchdog();` | WD | G(pp) | B32 | 009130ce |
| B42 (m) | 1078-1079 | +2/-1 | `earlyWakeDecision` + `gpio.begin(deferTouch)` (replaces `gpio.begin()`) | PB | UNG: off-Silkscreen deferTouch is false = old call | HAL HalGPIO::begin(bool) / startTouch (HalGPIO.cpp:123, 155); SDK InputManager::begin(startTouch) | B43a calls startTouch; 892c4f19, b86e9d49 |
| B43a | 1101-1108, 1112-1113, 1126-1127 | +12 | early sleep-or-boot decision from the RTC wake rule; `gpio.startTouch()` | PB | FOLD (earlyWakeDecision) | B6, B42 | `unverifiedWake` also read by B46a; b86e9d49 |
| B43b | 1109-1111, 1114-1125 | +15 | `batterylog::early(Ghost)`, `onWake`, one BATLOG line per wake | BL | G(bl) | SDK BatteryLog | nested inside B43a's block; b4a2ee87, af9fb8c8 |
| B44 | 1129-1131, 1139 | +4 | TS: `recoveryFirmwareMode = false` | TS | G(ts) | - | wraps up:611-617 |
| B45 | 1150-1151 | +2 | `sleepIfBatteryEmptyAtBoot();` | PS | FOLD | B35 | 7f2450c4 |
| B46a | 1156-1159, 1163-1165 | +7 | SD-failure path: unverified wake sleeps unless the click-wake rule is known | PB | FOLD | B6 | b86e9d49 |
| B46b | 1160-1162 | +3 | `batterylog::early(Ghost)` | BL | G(bl) | - | nested in B46a |
| B46c | 1166, 1174, 1179, 1181, 1199-1201, 1216, 1234 | +9 | TS guards: no SD-error screen / AppState load / plugins; `testsuite::beginAfterSettings()` | TS | G(ts) | - | wrap upstream lines; a5e8fb00, d21dde9d |
| B46d | 1167-1169, 1213-1215 | +6 | `haltOnPowerButtonFault()` at two call sites | PS | G(pp) && !TS | B28 | 16374791 |
| B47 | 1172 | +1 | `storageErrorScreen = true` | RA | UNG | B8 | 892c4f19 |
| B48 | 1198 | +1 | `settingsLoaded = true` | PB | UNG | B6 | BL also reads it; b86e9d49 |
| B49 | 1244-1246 | +3 | `batterylog::early(Ghost)` in the PowerButton case | BL | G(bl) | - | b4a2ee87 |
| B50 | 1251 | +1 | `wakeNoInputArmed = powerPolicy().wakeNoInputTimeoutS > 0` | RA | FOLD | B8 | 892c4f19 |
| B51 (m) | 1257 (up:710), 1265-1269 | +6/-1 | FREEINK_DEVICE_SILKSCREEN OR-ed into the AfterUSBPower stay-awake `#if` list + comment | DEV-WIRING | G(dev) | - | inert today: on Silkscreen the boot-time USB state is unknown so the wake classifies as Other (HalGPIO.cpp:362-396); 79c214a4 had +3/-1 |
| B52 | 1283-1291 | +9 | `takeReport` -> `logWatchdogReport`; release the log hold | WD | G(pp) | B32, B37 | 97f042ae |
| B53a (m) | 1292-1299, 1306, 1308 (up:684-691) | +10/-8 | `Frontlight.begin(...)` moved from before the wake switch to after it (+2 comment lines) | FL; GENERIC-FIX candidate | UNG: every board with a light. A wake that goes straight back to sleep (the ghost-wake exit in the PowerButton case; the USB-power sleep applies only to boards outside the stay-awake list) no longer runs `Frontlight.begin` / restore first | - | independent of the rest; ccf474d2 |
| B53b | 1300-1305, 1307 | +7 | TS: light never restored in the test firmware | TS | G(ts) | - | wraps B53a's call |
| B54 (m) | 1337 (up:759) | +1/-1 | `display.isX3Panel()` for `gpio.deviceIsX3()` | X368 | UNG, identical off the 3.68" variant (HalDisplay.h isX3Panel) | HAL HalDisplay::isX3Panel | 9385762a |
| B55 | 1419-1421 | +3 | `renderwatchdog::noteLoopPass()` | WD | G(pp) | WD files | 009130ce |
| B56a | 1423-1426 | +4 | `tickBatteryLog()` | BL | G(bl) | B16 | b4a2ee87 |
| B56b | 1427-1429, 1444 | +4 | TS: `remoteActed = false` replaces the BLE tick block | TS | G(ts) | - | wraps up:~842-855; a5e8fb00 |
| B57 | 1475-1481 | +7 | per pass: `handleFrontlightNotices(); keepWarmthOnPresentString();` / else `handleFrontlightTrip();` | FL | G(fp) arm; the `#else` arm is UNG: every board calls handleFrontlightTrip() each pass | B9, B10 | 86e1e6ea, a2129062 |
| B58a (m) | 1514-1521, 1525-1538 (up:918-919) | +22/-2 | `userInput` split from `remoteActed`; `powerManager.noteInput()` on first pass and input; `timeoutLiftedByRemote`; disarm `wakeNoInputArmed`; rewrites upstream's 2-line activity condition | RA | UNG: noteInput() + 2 function statics on every board | HAL noteInput inline (HalPowerManager.h) | `userInput` also feeds BL B58b; `remoteActed` is a TS substitution (B56b); 892c4f19, 13e662f7 |
| B58b | 1522-1524 | +3 | `batterylog::noteInput()` | BL | G(bl) | - | uses B58a's `userInput` |
| B59 | 1543-1550 | +8 | `clearAbnormalResets()` after 60 s up | PS | FOLD | B7 | b86e9d49 |
| B60 (m) | 1551-1558 (up:995 deleted) | +8/-1 | `sleepTimeoutMs` hoisted above the awake limits | RA | UNG pure hoist | - | deletes upstream's decl at up:995, so a patch without B60 must keep it; 8127e3e8 |
| B61a | 1559-1565, 1569-1572 | +11 | low-battery cutoff: `lowBatteryCutoffTripped()` -> `enterDeepSleep()` | PS | FOLD (`lowBatteryCutoffMv > 0 &&` first) | SDK-PP; HAL lowBatteryCutoffTripped | independent of RA; 8127e3e8 |
| B61b | 1566-1568, 1591-1593, 1604-1606, 1625-1627 | +12 | `batterylog::tagPath(...)` x4 | BL | G(bl) | - | one triplet per enterDeepSleep call in B61a, B62, B63, B65 |
| B62 | 1573-1590, 1594-1598 | +23 | awake ceiling (15 min, never shorter than the user's timeout), key-held scan BTN_BACK..BTN_POWER | RA | FOLD (`awakeCeilingMin > 0`) | SDK-PP; HAL awakeCeilingPassed, awakeCeilingMs | needs B58a `timeoutLiftedByRemote`, B60; 8127e3e8, 13e662f7, 5d58fdc0 |
| B63 | 1599-1603, 1607-1610 | +9 | input-only cap (60 min) | RA | UNG: `powerManager.awakeInputCapPassed()` is called every pass on every board (out-of-line, returns false); the LOG_INF string is in every image | SDK-PP.awakeInputCapMin; HAL awakeInputCapPassed | 8127e3e8 |
| B64 | 1611-1620 | +10 | wake with no input (180 s) -> `enterDeepSleep(true, true)` | RA | FOLD | B8, B17, B50, B58a | 892c4f19 |
| B65 | 1621-1624, 1628-1631 | +8 | SD-error screen idle sleep (60 s) | RA | FOLD | B8, B47 | 892c4f19 |
| B66 (m) | 1640-1645, 1657, 1661-1664 (up:946) | +11/-1 | `pauseWhileChordHeld`: `delay(10)` while the screenshot chord is held, keyed on `awakeCeilingMin > 0` | RA | FOLD | - | edits up:946; 8127e3e8 |
| B67 (m) | 1674-1682 (up:956-958) | +9/-3 | capture `armedClickAt` / `pendingClickExpired` before the double-click handler | PB | UNG: runs on X4 Pro | - | used by B68; 04dd14ea |
| B68 (m) | 1686-1711 (up:962) | +26/-1 | `deferClickActions`: every short-click action waits out the 500 ms double-click window on Silkscreen; `setPowerClickDeferral`; else-arm expires a stale click | PB | RT (`BoardConfig::isSilkscreen()`); only the else-arm (`lastPowerClickAt = 0`) can run on X4 Pro | APP: MappedInputManager::setPowerClickDeferral (MappedInputManager.h:53); gpio.hasTouch | B12, B67; 04dd14ea |
| B69 (m) | 1714, 1717-1719, 1723, 1726, 1736-1738, 1757, 1760-1762, 1764 | +14/-14 | renames `x4Pro*` -> generic in the Confirm / Sleep deferrals | PB | UNG renames | - | B5, B12; 36ee5cfc |
| B70 (m) | 1789-1791 (up:1041) | +3/-1 | Short Power = Toggle Frontlight toggles on release | PB (also a key route) | UNG; the value is offered only on Silkscreen (SettingsList.h:316) | CrossPointSettings SHORT_PWRBTN::TOGGLE_FRONTLIGHT=10 (CrossPointSettings.h:169) | 36ee5cfc |
| B71 | 1827-1831, 1835-1837, 1845-1850 | +14 | `sawRenderBusy`, `BatteryLogWriter::flushIfDue()` | BL | G(bl) | B16 | b5353c4e, b4a2ee87 |

Sums: added 814, removed 50 (PB 30, FL 8, RA 9, PS 1, DEV 1, X368 1) as in section 1.

Deriving a bucket's patch: take upstream main.cpp, delete the upstream lines listed below for that bucket's (m) rows, and insert the d076703a text of the bucket's rows (`git show d076703a:src/main.cpp | sed -n 'a,bp'`) at the same relative place. Every added line of the diff is covered by exactly one row (checked by a line-range tally: 814 added, 0 unclassified).
Removed upstream lines by ID (up: numbering, 50 in all): B5 71, 74-75; B12 252-253, 258-259, 263-264, 268; B14 375-376; B17 459-460; B22b 472; B34 517 (the old `setupDisplayAndFonts` header, re-added at 996); B42 588; B51 710; B53a 684-691; B54 759; B58a 918-919; B60 995; B66 946; B67 956-958; B68 962; B69 965, 968-970, 974, 977, 987-989, 1009, 1012-1014, 1016; B70 1041.

## 4. platformio.ini (+317, one hunk `@@ -562,0 +563,317`, no existing env changed)

| Lines | Section | Bucket | Notes |
|---|---|---|---|
| 563-571 | header comment | DEV-ENV | mentions docs/silkscreen/PORTING.md and MODULARITY.md |
| 572-588 | `[silkscreen]` + `frontlight_flags = -DFREEINK_CAP_FRONTLIGHT=1 -DFREEINK_CAP_WARMLIGHT=1` | DEV-ENV | SDK does not derive FRONTLIGHT/WARMLIGHT for Silkscreen (BoardConfig.h:246-261), so the env passes them; FRONTLIGHT also turns on FREEINK_CAP_FRONTLIGHT_PROBE (BoardConfig.h:408-410), which ties this to the FL bucket |
| 589-605 | `display_flags`, `touch_flags` (empty override hooks, 17 lines mostly comment) | UNSURE | for platformio.local.ini (`-DSILKSCREEN_DISPLAY_CONTROLLER`, `-DSILKSCREEN_TOUCH_ALT_JUMPERS`) |
| 606-615 | `device_flags`: FREEINK_DEVICE_SILKSCREEN=1, FREEINK_CAP_USB_MSC=1, BOARD_HAS_PSRAM, USE_BLOCK_DEVICE_INTERFACE=1, USB_PRODUCT="Silkscreen_Reader", USB_MANUFACTURER="Silkscreen", + the three hook lists | DEV-ENV | upstream names are `CrossPoint_<Device>` / `CrossPoint` |
| 617-652 | `[env:silkscreen]`, `-gh_release`, `-gh_release_rc` | DEV-ENV | extends base; esp32-s3-devkitc1-n16r8; mcu esp32s3; memory_type dio_opi; same shape as x4pro (up:391-411, 529-561) |
| 654-678 | `[env:silkscreen-batlog]` | BATLOG | release flags + `-DSILKSCREEN_BATLOG=1`, version `-batlog`, same board tag |
| 680-733 | `[env:silkscreen-test]` | TESTSUITE | `-DCROSSPOINT_TEST_SUITE=1 -DFREEINK_DIAGNOSTICS=1 -DOMIT_FONTS=1 -UFREEINK_CAP_BLE_HID_HOST`, `custom_i18n_builtin_langs = en`, `lib_ignore NimBLE-Arduino`, 16-entry `build_src_filter` (717-733) |
| 735-879 | `[silkscreen368]`, `silkscreen-368`, `-gh_release`, `-gh_release_rc`, `-368-batlog`, `-368-test` | X368 | 145 lines (141 + 4 blanks); with the blank at 734 the X368 total is 146 |

Minimal `[env:silkscreen]` needs: `extends = base`; `board = esp32-s3-devkitc1-n16r8`; `board_build.mcu = esp32s3`; `board_build.arduino.memory_type = dio_opi`; flags `-DFREEINK_DEVICE_SILKSCREEN=1 -DFREEINK_CAP_USB_MSC=1 -DBOARD_HAS_PSRAM -DUSE_BLOCK_DEVICE_INTERFACE=1` (SDMMC, because FREEINK_SD_SDMMC lists Silkscreen, BoardConfig.h:442-446), `USB_PRODUCT/USB_MANUFACTURER`, `CROSSPOINT_VERSION`, `ENABLE_SERIAL_LOG`, `LOG_LEVEL`, and (only if the light ships) the two FRONTLIGHT/WARMLIGHT flags. No partition file (base `partitions.csv`, two OTA slots, as x4pro), no `extra_scripts`, no `lib_deps`/`lib_ignore`, no sdkconfig (it extends `base`, not `firmware_tuned`).
Upstream comparison: x4pro/metalio/eego/picco each repeat their flags inline in three envs (dev, `-gh_release`, `-gh_release_rc`); only Picco adds `board_build.partitions = partitions_picco.csv` and `extra_scripts` (stock-layout flashing); EEGO adds `board_build.flash_mode = dio`. A shared `[silkscreen]` section has precedent only in `[ble_page_turner]` / `[firmware_tuned]`.

## 5. Other files in scope

| File | +/- | Content by bucket | Notes |
|---|---|---|---|
| .github/workflows/ci.yml | +23 | 36-38 `Sleep/wake source guard` step in the clang-format job = LOCAL-TOOLING (3); 108-109 matrix `silkscreen` = DEV-ENV (2); 110-113 + 118-119 `silkscreen-368`, `-368-test` = X368 (6); 114-117 `silkscreen-test` = TS (4); 186-189 test-firmware check step = TS (4); 190-193 3.68" no-touch check step = X368 (4) | upstream device PRs also edit pr-firmware-links.yml (+1), release.yml (+7), release_candidate.yml (+2), README.md line 10 and docs/contributing/touch-and-ui.md (table row); the fork has none of these |
| .gitmodules | +2/-2 | URL -> github.com/iandchasse/freeink-sdk, branch silkscreen | LOCAL-TOOLING; an upstream PR only bumps the `freeink-sdk` gitlink |
| USER_GUIDE.md | +6 | 81 Silkscreen front-light bullet (double-click, Settings > Display > Frontlight with keys, Toggle Frontlight, long-press Confirm = Frontlight) PB (+KA); 250 5% steps / batlog 1% / `silkscreen-368-batlog` BATTERY-DISPLAY (+BL, X368); 320 "Links and Footnotes" long-press (upstream feature LP_MENU_FOOTNOTES=6 lacked a line) GENERIC-FIX; 322 "Frontlight (Silkscreen)" long-press KEYACCESS; 331 "Toggle Frontlight (Silkscreen)" PB; 332 "Double-Click Power for Light (X4 Pro, Silkscreen)" PB | |
| docs/webserver-endpoints.md | +1/-1 | line 61 `device` string: C3 "X3"/"X4", 3.68" "X3", "board profile's name on every other ESP32-S3 board" | GENERIC-FIX (S3 half is true upstream, up:CrossPointWebServer.cpp:558-560) + X368 clause |
| scripts/check_sleep_wake_sources.sh | +101 | greps src, lib, freeink-sdk/libs for timer/ULP/touch-pad/ext0/UART wake, light sleep, `esp_sleep_pd_config`; skips `#if FREEINK_DEVICE_PICCO` arms | LOCAL-TOOLING (Silkscreen drain rule; would reject upstream code) |
| scripts/check_test_firmware.sh | +95 | nm-based check that the test image links no reader class and has one board tag | LOCAL-TOOLING / TS |
| src/util/RenderWatchdog.{h,cpp} | +168 / +343 | WD; .cpp wholly inside `#if FREEINK_CAP_POWER_POLICY`; about 69 lines are `#if CROSSPOINT_TEST_SUITE` P9 hooks | see A3 |
| src/util/BatteryLogWriter.{h,cpp} | +35 / +161 | BL; whole files inside `#if SILKSCREEN_BATLOG`, `#error` on non-Silkscreen or TS | |
| src/util/PluginEvents.{h,cpp} | +24/-7, +3/-1 | PS cutoff poll: includes HalPowerManager.h + CrossPointSettings.h (new util->settings dependency), `shouldAbort` threaded through `deliverLine` (292, 342, 370-373, 390), `drain()` builds a `std::function` (413-416), stops between subscribers (421-424) and passes it per line (456) | UNG, false off-Silkscreen |
| src/util/PluginHttp.{h,cpp} | +19/-16, +7/-4 | PS: `requestToFile(..., shouldAbort = nullptr)`, `mintPasswordToken(..., shouldAbort = nullptr)`; SecureHttpClient::sendRequest already takes the callback (f9cc1be SecureHttpClient.h:154); 243-257 is clang-format reflow | UNG; default args keep other callers |
| lib/Logging/Logging.{h,cpp} | +4, +16 | WD: `holdLastLogs()` + `logRingHeld` (Logging.cpp:22-30, 33-35, 120-122) all `G(pp)`; `#include <atomic>` (6) and the declaration + comment (Logging.h:70-73) are ungated | other boards: header declaration only |
| lib/TrustedTime/TrustedTime.cpp | +10/-1 | BL: include (12-15) and `noteClockSynced()` in `onTimeSynced` (70-75), G(bl); the one-line callback became six lines | no behavior change without BATLOG |
| lib/Serialization/PersistableStore.cpp | +7 | TS: `writeDocToFile` returns true without writing (11-17) | G(ts) |
| lib/I18n/translations/english.yaml | +6 | 593 STR_BATTERY_TOO_LOW_FOR_UPDATE (PS, OtaUpdateActivity.cpp:73, SdFirmwareUpdateActivity.cpp:73,163); 621-623 STR_FRONTLIGHT_NOT_DETECTED / COOL_ONLY / WARM_ONLY (FL); 624 STR_POWER_BUTTON_FAULT (PS, main.cpp:782); 625 STR_NO_BATTERY (BD, BaseTheme.cpp:86) | UNG: StrIds exist in every board's tables (A5-R8) |
| test/ | +94 | ClippingSyncTest.cpp +26 and stubs/HalPowerManager.h +14 (PS: cutoff stops the clip sync); test_recursive_dictionary.py +28 and test_selection_buttons.py +26 (KEYACCESS: cancelled word selection forgets its range, keys beside touch) | note only |

## 6. Answers

### A1. Minimal device (upstream behavior otherwise)

| Item | IDs / where | Size |
|---|---|---|
| main.cpp | B51 only: OR FREEINK_DEVICE_SILKSCREEN into the AfterUSBPower `#if` list (up:709-710) | +1/-1 code line; 79c214a4 added +3/-1 with a comment; the current 5-line comment can shrink to 1-2 |
| platformio.ini | `[env:silkscreen]`, `-gh_release`, `-gh_release_rc` (617-652) + device flags (606-615) + frontlight_flags (586-588) | commit 79c214a4 shipped 62 lines (light compiled out). Today's equivalent with 3 trimmed envs is about 55-60 lines; the fork's 563-652 is 90 lines because of about 33 comment lines and the display/touch hooks |
| board tag | FirmwareBoardTag.cpp: `#elif FREEINK_DEVICE_SILKSCREEN` + `#define CROSSPOINT_BOARD_NAME "silkscreen"` | +2 (the other 10 added lines are TS 3, X368 7) |
| CI / release | ci.yml matrix `silkscreen` (+2, present); pr-firmware-links.yml +1, release.yml +7, release_candidate.yml +2 (absent in the fork) | +12 |
| docs | README.md line 10 device list; touch-and-ui.md table row | +2 |
| SDK | gitlink bump with the SDK PR | - |

Total about 3 lines of C++, about 60 of ini, 12 of CI, 2 of docs. The fork's own first device commit 79c214a4 touched exactly .gitmodules, the SDK gitlink, platformio.ini (+62), FirmwareBoardTag.cpp (+2) and main.cpp (+3/-1); its message says silkscreen and x4pro built (not re-run by me). Whether upstream's lib/hal needs any change for FREEINK_DEVICE_SILKSCREEN is NOT COVERED here (HalGPIO/HalPowerManager/HalFrontlight belong to another map).
Decision points: (1) ship the light or not: with `frontlight_flags` empty (as 79c214a4) FL drops out; with it on, FREEINK_CAP_FRONTLIGHT_PROBE switches on in the SDK and the SDK/HAL light machinery (HalFrontlight probe API) comes along even if B9/B10/B21/B53a/B57 do not. (2) USB_PRODUCT naming.

### A2. main.cpp: bucket -> IDs -> lines

| Bucket | IDs | Lines (d076703a) | +/- |
|---|---|---|---|
| DEV-WIRING | B51 | 1257, 1265-1269 | +6/-1 |
| POWER-SAFETY | B1 B7 B15 B26 B27 B28 B30 B34 B35 B38 B39 B40 B45 B46d B59 B61a | 2, 20, 28, 158-161, 530-534, 738-805, 822-842, 848-861, 929-930, 946, 948-988, 992-997, 1047-1057, 1073, 1077, 1150-1151, 1167-1169, 1213-1215, 1543-1550, 1559-1565, 1569-1572 | +205/-1 |
| POWER-BUTTON | B5 B6 B12 B25 B42 B43a B46a B48 B67 B68 B69 B70 | 86, 89-90, 149-157, 373-389, 731-734, 1078-1079, 1101-1108, 1112-1113, 1126-1127, 1156-1159, 1163-1165, 1198, 1674-1711, 1714-1764 (8 rename groups), 1789-1791 | +97/-30 |
| FRONTLIGHT | B9 B10 B21 B53a B57 | 168-251, 659-664, 1292-1299 + 1306 + 1308, 1475-1481 | +107/-8 |
| RADIO-AWAKE | B8 B14 B17 B22b B47 B50 B58a B60 B62 B63 B64 B65 B66 | 162-167, 499-514, 643-645, 680, 1172, 1251, 1514-1521 + 1525-1538, 1551-1558, 1573-1631 (minus the four BL tagPath triplets), 1640-1664 | +115/-9 |
| WATCHDOG | B2 B18 B29 B31 B32 B37 B41 B52 B55 | 56-58, 646-650, 806-821, 843-847, 862-898, 1027-1033, 1074-1076, 1283-1291, 1419-1421 | +88 |
| BATLOG | B4 B11 B16 B20 B24 B36 B43b B46b B49 B56a B58b B61b B71 | 63-67, 276-278, 599-642, 655-657, 705-708, 989-991, 1109-1111 + 1114-1125, 1160-1162, 1244-1246, 1423-1426, 1522-1524, four tagPath triplets, 1827-1850 | +116 |
| TESTSUITE | B3 B13 B19 B22a B23 B44 B46c B53b B56b | 59-62, 466/473/489/561, 651-654, 667-671 + 681, 695-697 + 704, 1129-1131 + 1139, 9 guard lines in setup, 1300-1307, 1427-1429 + 1444 | +46 |
| X368 | B54 | 1337 | +1/-1 |
| UNSURE | B33 | 899-928, 941-943 | +33 |

Four compound `#if` lines mix buckets and need one token edited when TS is removed: 188 and 1475 (`&& !CROSSPOINT_TEST_SUITE` on FL), 765 and 1213 (same on PS).

**enterDeepSleep() (643-736; up:459-515).** Upstream body stays. Per bucket, insert: WD = B18 at the very top (before BL staging); BL = B20 right after it, B24 after the Quick Resume block, helper B16 above the function; FL = B21 after `HalPowerManager::Lock`; RA = B17 (replaces the 2 signature lines) and B22b (replaces up:472); PB = B25 before `startDeepSleep`; TS = B19, B22a, B23. TS arms are pure `#if` wrappers: delete the `#if CROSSPOINT_TEST_SUITE ... #else` arm and the `#endif`, keep the `#else` text. The only value shared across buckets is `unattended` (RA -> BL B16/B20). Order inside the function is fixed by the WD marker coming first.

**setup() (1025-1410).** Insertion points in order: after `holdPowerRails()` WD B37; after the serial block PS B38+B39; replacing `gpio.begin()` PS B40, WD B41, PB B42; after `wakeHoldVerified` PB B43a (BL B43b); recovery-mode TS B44; after the device log PS B45; in the `Storage.begin()` failure branch PB B46a, BL B46b, PS B46d, RA B47; at `SETTINGS.loadFromFile()` PB B48 and PS B46d (second site); inside `switch (wakeupReason)` BL B49, RA B50, DEV B51; after the switch WD B52, FL B53a (the only relocation of an upstream block: delete up:684-691, insert 1292-1299 + 1306 + 1308 after the switch), TS B53b; X368 B54. Shared variables: `earlyWakeDecision` and `unverifiedWake` are declared by B42 / B43a and read by B46a and the BL rows; `settingsLoaded` by B6/B48 (read by B25 and B16); `watchdogLogsHeld` by B37 (read by B52). A PS patch (with SDK-PP) is B1 B7 B15 B26-B28 B30 (inline form) B34 B35 B38-B40 B45 B46d B59 B61a; PB needs B6 B42 B43a B46a B48 plus the HAL `begin(bool)` change.

**loop() (1412-1874).** Order: WD B55; BL B56a; TS B56b; FL B57; RA B58a (+BL B58b); PS B59; RA B60; PS B61a (+BL tag); RA B62-B65; RA B66; PB B67-B70; BL B71. Entanglements to cut: `userInput` (B58a) is read by BL B58b and is the base of every RA rule; `timeoutLiftedByRemote` (B58a) is read by B62; B60 deletes upstream's `sleepTimeoutMs` declaration (up:995) which sits inside the PB region, so a PB-only or PS-only patch must leave it; B61a (cutoff) does not depend on RA and can go first; B66 depends only on `awakeCeilingMin`. The PB block (B67-B70) is contiguous and edits about 30 upstream X4 Pro lines (renames), so it cannot be a pure insertion.

**Sleep helpers block (738-997).** PS-only form: keep B34 (probe extraction), B26, B27, B28, B35 and a `sleepIfResetLoop()` equal to the pre-watchdog text (`git show 009130ce^:src/main.cpp` lines 799-824: inline `resolvePanelController(); sleepPanelWithoutDriver(); resetTouchForSleep(); startDeepSleepMinimal()`), no `sleepWithoutDrivers`, no `#if/#else`. The WD patch then turns those four inline lines into `sleepWithoutDrivers()` (B29), adds `resetLoopSlept()` (B31) and B32. B33 (log) is optional. This also removes the dead `#else` copy in B30.

Suggested slicing (dependencies): DEV (B51 + ini + tag) -> PS core (SDK-PP, B34, B35, cutoff B61a, B38-B40, B45) -> PS extras (B27/B28, B15 + cutoff-poll plumbing A4) -> PB (needs settings/enum + MappedInputManager) -> RA (needs B58a; B14/B17 `unattended`) -> FL (HAL probe API) -> WD (needs PS) -> BL/TS/X368 stay local. BL depends on RA (`unattended`), PB (`settingsLoaded`) and PS (early rows).

### A3. RenderWatchdog

What it catches: a `render()` that never returns while holding the render lock, and a sleep entry that never sleeps. ActivityManager.cpp:93 takes `RenderLock` (blocking, `xSemaphoreTake(..., portMAX_DELAY)`, line 552) before `render()` and holds it until it returns (an activity may release it early). Every sleep path then waits on that lock: `enterDeepSleep()` -> `deliverSleepPluginEvents()` -> `ActivityManager::prepareForSleep()` (line 503, blocking `RenderLock`) -> `goToSleep()` (371-375: `replaceActivity()` + `loop()`) -> `loop()` replace branch (line 226, blocking `RenderLock`), and `requestUpdateAndWait()` (546) waits with `portMAX_DELAY` for the render task. So the safety path (sleep) depends on the render task being healthy. With no hardware off the board then draws 30-45 mA until flat (POWER.md 3.1). The ESP32 task watchdog does not see it. The documented trigger was the 3.68" panel freeze, an SDK bug (SPI transaction held across a bus command) since fixed upstream (Free-Ink #139), so the watchdog is a net for unknown hangs.

Mechanism (RenderWatchdog.cpp): render task stamps `millis()` after taking the lock (`beginRender`, ActivityManager.cpp:102) and clears it after (`endRender`:106); `progress()` re-stamps from long renders; `enterDeepSleep()` stamps a sleep mark (B18) never cleared; a core-0 task (4 KB, priority 1, every 5 s) restarts via `esp_restart_noos()` after `BOUND_MS` = 5 min (render) or `SLEEP_ENTRY_BOUND_MS` = 3 min on battery, after darkening the light through GPIO registers and storing an RTC record; the next boot sleeps at once (`sleepIfRenderWatchdog` B32 -> `sleepWithoutDrivers` B29) and the following boot reports it (B52).

Needs beyond FREEINK_CAP_POWER_POLICY (all in the fork's SDK and absent from f9cc1be unless noted): `ChargeLadderDecoder.h` (BatteryMonitor lib) and `SharedAdc.h` (`freeink::adc::Lock`) plus `BoardConfig::hasChargeStatusLadder()` / `ACTIVE.chargeLadder` (FREEINK_CAP_CHARGE_LADDER data) for the lock-free "is USB present" test when the loop task is stale (cpp:141-169); `FrontlightConfig` fields gpio, activeHigh (both exist upstream), colorSelectGpio, offDwellMs, colorSelectWarmHigh (fork-only) under `#if FREEINK_CAP_FRONTLIGHT` for `darkenLight()` (cpp:178-191); `HalGPIO::isUsbConnected()`; `holdPowerRails()` parking the pins LOW at the next boot; for the post-restart sleep: `PowerManager::sleepPanelWithoutDriver`, `HalGPIO::resetTouchForSleep`, `HalPowerManager::startDeepSleepMinimal`; IDF private header `esp_private/system_internal.h` and `esp_rom_printf` (raw, deliberately lock-free). Logging needs `holdLastLogs()`.

Hooks: main.cpp B2 B18 B29 B31 B32 B37 B41 B52 B55 (88 lines); ActivityManager.cpp:36-38, 77-80, 100-107 (all `G(pp)`); HomeActivity.cpp:28-41 and call at 184 (one per cover); EpubReaderActivity.cpp:63-75 and calls at 416, 1683, 1772, 1842, 1853; lib/Logging (Logging.cpp:22-35, 120-122; Logging.h:70-73). On other boards: no code beyond the Logging.h declaration.

Board-independent proposal? The hang class is board independent (any render() that blocks keeps the lock on every board) but the damage is not: boards that can be powered off by hardware (main.cpp:1096-1097 says an X4 battery power-off cuts all power; HalPowerManager.cpp up:100-122 does a battery power-off and up:151-154 a Metalio `powerOff()`) are rescued by the user; Silkscreen is not. The implementation is Silkscreen-shaped: ladder-based USB test, GPIO-register light blanking, a driver-free sleep boot path that exists only behind FREEINK_CAP_POWER_POLICY, 4 KB stack + TCB (too much for the C3's RAM budget unless gated; GUESS about 0.5-1 KB flash). A generic version would need `gpio.isUsbConnected()` to be lock-free on every board (not checked for Picco's I2C charger). Coding-standards friction: restart is "for explicit recovery such as OTA completion", `esp_rom_printf` instead of LOG_*, SDK calls from app code. My read: propose only as a gated feature after the device PR, not as a board-independent PR.

Root fix? Reading the code, the inversion cannot be removed cheaply. (1) A bounded `RenderLock` wait (`xSemaphoreTake` with a timeout) would catch "somebody waits for the lock" but the recovery is still a restart into a driver-free sleep (POWER.md: any in-place sleep step takes the SPI / I2C / storage / front-light / render locks the stuck task can hold), so it reuses B29/B32 and the RTC record, and it does not cover a sleep entry that hangs by itself on the loop task (needs a separate timer). (2) Drawing the sleep screen without the render task needs the render lock or SPI lock anyway. (3) The root cause of any given hang is a missing bound in a driver wait (BUSY, SPI transaction); fixing those in the SDK is the real cure but cannot be proven complete. So the watchdog is defense in depth; an upstream-sized variant would be a bounded `RenderLock` + record + boot sleep (inference, not tested).

### A4. Cutoff-poll plumbing

API: `HalPowerManager::lowBatteryCutoffTripped()` (HalPowerManager.cpp:208-218, .h:106; latch `cutoffTripped`, one reading per second via SDK `BatteryMonitor::isBelowCutoff(nullptr, &mv, radioOn)`, with a radio allowance) and `HalPowerManager::backgroundNetworkMustStop(sleepTimeoutMs)` (cpp:237, .h:98) = cutoff || `awakeCeilingPassed` || `awakeInputCapPassed`. Consumers pass these as `shouldAbort` callbacks that the SDK's `SecureHttpClient::GET/sendRequest(..., AbortCallback)` already supports (f9cc1be SecureHttpClient.h:128-159), as a `Print` that returns 0 (`CutoffAbortPrint.h`, new, 26 lines), or as a flag. One SDK addition: virtual `OpdsTransport::shouldAbort()` (OpdsTransport.h +5; OpdsImplicitAuth.{h,cpp} and OpdsClient.cpp edited to poll it) used by OpdsHttpTransport.cpp:37.
No-op elsewhere: `lowBatteryCutoffTripped()` returns false from the `cutoffTripped` member, then `powerPolicy().lowBatteryCutoffMv == 0` (constexpr off-Silkscreen) before touching the ADC or a `static BatteryMonitor`; `awakeCeilingMs()` / `awakeInputCapPassed()` multiply a zero field. The calls are real out-of-line calls in every image (about 20 sites in 13 files, one per wait loop); no LTO folding assumed.
Sites (d076703a): main.cpp:534, 1565; PluginEvents.cpp:407-424, 456 (drain) + PluginHttp.cpp:229-275 (requestToFile, mintPasswordToken); HttpDownloader.cpp:40, 80, 167-168 (fetchUrl GET), 206-207 (postForm), 253; CrossPointWebServer.cpp:732 (stream via CutoffAbortPrint), 2028 (handleRelay), 2394 (handleFetch); WebDAVHandler.cpp:95 (PUT raw write drops the connection), 362 (GET via CutoffAbortPrint); OpdsHttpTransport.cpp:37; CatalogActivity.cpp:167, 208; CrossPointWebServerActivity.cpp:317; PluginCatalogActivity.cpp:57; FontDownloadActivity.cpp:617; ClippingSync.cpp:26. Only main.cpp, PluginEvents.*, PluginHttp.* are in my list; the rest from grep.

### A5. All-boards behavior changes (risk register)

| # | Where | What changes on other boards |
|---|---|---|
| R1 | main.cpp:1292-1308 (B53a) | `Frontlight.begin()` / Restore-on-Wake now run after the wake switch and after the watchdog report; a wake that sleeps again never lights the LED. Boards with a light (X4 Pro, Picco, EEGO A4, Paper Mono). Low risk, arguably a fix |
| R2 | main.cpp:373-392, 1674-1764 (B5 B12 B67 B69) | X4 Pro double-click renamed and gated through `SETTINGS.powerDoubleClickLight()` (adds `shortPwrBtn != TOGGLE_FRONTLIGHT`); 1705-1711 now clears a stale `lastPowerClickAt` on X4 Pro. Same behavior; every upstream edit to that code now conflicts |
| R3 | main.cpp:1514-1541 (B58a) | per-pass `powerManager.noteInput()` and two function statics (`inputClockStarted`, `timeoutLiftedByRemote`) on every board |
| R4 | main.cpp:1602-1609 (B63) | `awakeInputCapPassed()` called every loop pass on every board (returns false) and its LOG_INF string in every image |
| R5 | main.cpp:659-664, 1478-1480 (B21, B57) | `handleFrontlightTrip()` every pass and at sleep entry, `Frontlight.prepareForSleep()` at sleep entry; no-ops unless the SDK monitor faults / light is "gated". Needs SDK `fault()` and `prepareForSleep()` |
| R6 | main.cpp:530-534; PluginEvents.cpp:407-424, 456 | `backgroundNetworkMustStop()` per sleep delivery, per subscriber and per line; a `std::function` built per `drain()` (GUESS 0.1-0.3 KB flash) |
| R7 | PluginHttp.h:87-98, PluginHttp.cpp:229-275 | API grows a defaulted `std::function` parameter on `requestToFile` and `mintPasswordToken`; reflow churn |
| R8 | lib/I18n/english.yaml:593, 621-625 | 6 new StrIds in every board's tables: each of the 35 language offset tables gets 6 entries (2 B each, `gen_i18n.py` 683-720) plus about 0.3 KB of English text; GUESS 0.7 KB flash on any board that builds all languages |
| R9 | main.cpp:149-166 | 4 B `RTC_NOINIT` + 3 B .bss (`settingsLoaded`, `wakeNoInputArmed`, `storageErrorScreen`) on every board |
| R10 | ci.yml:36-38 | a Silkscreen-only drain guard runs on every upstream PR (rejects timer / ULP / light-sleep use; special-cases Picco). Must not go upstream |
| R11 | platformio.ini 563-879; .gitmodules | pure insertion, no existing env changed (verified: single added hunk); `.gitmodules` points at the fork |
| R12 | TrustedTime.cpp:70-75; Logging.h:70-73; Logging.cpp:6 | formatting / declaration / include only off-Silkscreen |
| R13 | main.cpp:849-858 | dead `#else` copy of the reset-loop sequence compiled off-Silkscreen (folded away) |

### A6. Hygiene (against .agents/rules, at most 10)

A grep over every added line of main.cpp, src/util, lib/Logging, lib/TrustedTime, lib/Serialization found no code use of `throw`, `try`, `catch`, `dynamic_cast`, `typeid`, `Serial.`, `new`, `malloc` or `abort(` (hits were comments and `snprintf`); the findings are:

1. Comment length and history: main.cpp:198-215 (18 lines), 168-174, 863-870, 1573-1580, 1689-1695; rule says 1-2 lines and no investigation history. "as upstream has it" at 1526; RenderWatchdog.cpp:22 "the 3.68" panel's first freeze read as that, from the code".
2. Comments cite files and IDs that do not exist upstream: "POWER.md AW-04" (647, 863, 1028, 1420), "HARDWARE.md 13.1 / 9.2" (191, 742), "docs/silkscreen/TEST-SUITE.md" (653), BAT-01, WK-02 (HttpDownloader.cpp:37, main.cpp:1528); Logging.h:70-73 and Logging.cpp:22-25 name docs/silkscreen.
3. Naming / magic numbers: `kQuickCheckMarginMv` (main.cpp:958, constants are UPPER_SNAKE_CASE); literals 2500 (963), 1010 (972), 16 (960), `delay(10)` (1644), 50 / 0 / 100 (243-244).
4. SDK classes called straight from main.cpp instead of through lib/hal (architecture-hal.md): `freeink::PowerManager::` at 746-759, 776-777, 816, 831-842, 856, 1050, 1547; `BatteryMonitor` at 790-796, 959-976; `freeink::batterylog::` on 18 lines. Upstream main.cpp calls the SDK only for `freeink::applyXteinkDisplayController()`.
5. Device test in shared loop code: `BoardConfig::isSilkscreen()` at main.cpp:1696 (SCOPE.md: device-specific code behind the HAL/SDK boundary); `awakeCeilingMin > 0` used as a proxy for "has a battery policy" at 1644.
6. Dead code: main.cpp:848-858 (`#else` arm never runs); `sleepIfBatteryEmptyAtBoot` has external linkage (955) unlike the other helpers.
7. `std::function` added to library code: PluginEvents.cpp:292, 413-416; PluginHttp.h:89-98 (coding-standards "Template and std::function bloat").
8. RenderWatchdog.cpp:195-214, 250: raw `esp_rom_printf` instead of `LOG_*`; `esp_restart_noos()` (rule: restart only for explicit recovery); Arduino `analogReadMilliVolts` and IDF GPIO calls outside the HAL; a task that is never deleted.
9. Board part references in a shared string table: english.yaml:624 `STR_POWER_BUTTON_FAULT` ("R36 and R72 must not both be fitted"), 621 "about 10 s".
10. main.cpp grows 1110 -> 1874 lines; policy blocks (awake limits 1543-1631, sleep helpers 738-997) belong in HAL / util rather than the entry file.

### A7. Settings and persistence (my files)

- No new JSON key or file in main.cpp / util / lib. New persisted values are enumerators referenced from main.cpp and defined elsewhere: `SHORT_PWRBTN::TOGGLE_FRONTLIGHT = 10` (CrossPointSettings.h:169) and `LP_MENU_FRONTLIGHT_PANEL = 7` (CrossPointSettings.h:185); defaults unchanged (IGNORE, DISABLED). The comments there say Silkscreen units saved these as 7 / 6 before upstream's Bookmark and Footnotes took those numbers (owner-unit migration only). Upstream firmware reading 10 / 7 meets out-of-range values (upstream COUNT = 10 / 7; clamping behavior not checked).
- `doubleClickPwrLight` (upstream key, default 1) keeps its default; its meaning widens to Silkscreen.
- Existing settings now written by firmware: `frontlightOn` = 0 on a trip or "no light" (main.cpp:181-184, 222-225) and `frontlightWarmth` = 0 / 100 on a single-colour light (246-248): 3 new `saveToFile()` sites on the loop task.
- RTC (not persistent across power loss): `sleepWakePolicy` (153), the watchdog record `g_record` (RenderWatchdog.cpp:99) and stack note (TS only), the log-ring hold (RAM only).
- SD: `/silkscreen-battery/battery.csv` (BatteryLogWriter.cpp, BL only); the test firmware suppresses all `/.crosspoint/` writes (PersistableStore.cpp:11-17).
- Plugin outbox / queue behavior: events stay queued when the drain stops; no format change.

## 7. NOT COVERED

- lib/hal/** (HalPowerManager, HalGPIO, HalFrontlight, HalClock, HalDisplay, HalStorage) beyond the APIs main.cpp calls; whether upstream lib/hal builds and runs a FREEINK_DEVICE_SILKSCREEN image unchanged.
- freeink-sdk internals (only symbol presence upstream vs ours was checked); SDK default for FRONTLIGHT / WARMLIGHT is read from BoardConfig.h only.
- CrossPointSettings / SettingsList / MappedInputManager / SettingsActivity bodies, the other call-site files in A4 beyond their hook lines, ActivityManager.cpp beyond the watchdog and TS hooks, HomeActivity and EpubReaderActivity beyond `watchdogProgress()`.
- BatteryLogWriter.cpp beyond its first 70 lines; RenderWatchdog host tests and scripts/silkscreen/** (ignored per brief); docs/silkscreen/** except the AW-04 section and size notes.
- Any compile, ELF or size measurement: FOLD and "no-op" claims are source readings; flash figures marked GUESS are estimates.
- Settings clamping of out-of-range enum values on upstream firmware (A7).
