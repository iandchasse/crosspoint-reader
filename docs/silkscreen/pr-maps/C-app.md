# C-app: CrossPoint app, Silkscreen fork vs upstream, hunk-level map (read-only research)

Scope: `git diff upstream/develop d076703a` (base 0fcdb3ec) over lib/hal/**, src/CrossPointSettings.*, src/SettingsList.*, src/MappedInputManager.*, src/components/**, src/network/**, src/clippings/**, src/activities/** (excl. testsuite/**), lib/I18n/translations/english.yaml, test/. Line numbers are NEW-side at d076703a. Method: `git diff -U0` per hunk, every added/removed line assigned to exactly one rule range by script (sums reproduce the git totals; one blank line was folded in by hand), plus reading each hunk and the SDK headers (bee6382 vs f9cc1be via `git -C freeink-sdk show`). Nothing was built, run or edited. Guesses are marked GUESS.

Totals in scope: 76 files, +1773 / -235 (+94 lines in 4 test files). Outside this scope but entangled: src/main.cpp (+864, most callers of the new HAL APIs), src/util/{RenderWatchdog,BatteryLogWriter,PluginHttp,PluginEvents}.*, lib/{Logging,Serialization,TrustedTime}, freeink-sdk.

## 0. Legend

Buckets: DEV (HAL glue, no policy), FL (FRONTLIGHT-UI), KEY (KEYACCESS), BAT (BATTERY-SAFETY), RAD (RADIO-AWAKE), DISP (BATTERY-DISPLAY), WDOG, BLOG (SILKSCREEN_BATLOG), TEST (CROSSPOINT_TEST_SUITE), X368 (3.68" variant), GEN (GENERIC-FIX candidate), UNS (unsure).
Gating: `IF m` = removed from other boards by `#if m`. `FOLD x` = compiled in, inert elsewhere because an SDK policy/field is 0/false (SDK `powerPolicy()` is `constexpr PowerConfig{}` and `hasChargeStatusLadder()/hasAdcLadders()` return false without the CAP macro, BoardConfig.h:3434-3456 @bee6382); costs bytes only. `SAME` = compiled in, behaviour identical elsewhere (refactor or predicate swap). `CHG` = compiled in and behaviour differs on a non-Silkscreen board.
SDK tags (all absent at base f9cc1be, present at bee6382): SDK:BTN = `TouchConfig::buttonsAlongsideTouch` (BoardConfig.h:824) and `BoardConfig::hasFrontButtons()` (:3239). SDK:PP = `powerPolicy()`/`PowerConfig` fields (:1035-1103, CAP_POWER_POLICY). SDK:LAD = ADC key ladders (CAP_ADC_LADDERS: `hasAdcLadders`, `adcLadderContact`, `ladderPresence`). SDK:CHG = charge-status ladder (CAP_CHARGE_LADDER: `hasChargeStatusLadder`, `pollChargeStatus`, `chargeStatus`, `chargeStatusLost`, ladder branch of `isExternalPowerPresent`). SDK:TCH = `InputManager::begin(bool)`, `startTouch`, `prepareTouchForSleep`, `holdTouchIntForSleep`, `resetAndHibernateTouch`, `touchIdRefused`, `TouchConfig::hibernateInSleep` (CAP_PROBE_IDENTITY). SDK:I2C = `freeink::i2c::releaseSensorBusForSleep` (CAP_I2C_RECOVERY). SDK:FL = FrontlightManager `fault/clearFault/load/stringMissing/blendsColour/probing/restoreAllowed/prepareForSleep` and `FrontlightConfig::{colorSelectGpio,enablePulseUs,monitorAdc,loadProbeWindowMs}` (CAP_FRONTLIGHT_PROBE). SDK:BAT = `BatteryMonitor::{isBelowCutoff,readMillivoltsAveraged,CELL_PRESENT_MIN_MV,percentTenthsFromMillivolts}`. SDK:RTC = `Rtc::rejectedAddress()`, `SensorsConfig::rtcResyncWhenInvalid`. SDK:OPDS = `OpdsTransport::shouldAbort()` (OpdsTransport.h:64). SDK:DSP = `displayProbePolicy().x4ClassPanel` (CAP_DISPLAY_PROBE_POLICY). SDK:DEV = `Board::Silkscreen`/`isSilkscreen()`/`FREEINK_DEVICE_SILKSCREEN`.
Already in base SDK and therefore NOT new: `SecureHttpClient::GET(cb, shouldAbort)`, `aborted()`, `ButtonAdcSample::button`, `BatteryMonitor::isExternalPowerPresent(bool*)`, `hasPwmFrontlight()`, `FREEINK_CAP_TOUCH`, `FREEINK_CAP_FRONTLIGHT`.

## 1. Bucket summary

| Bkt | files | + | - | IF-gated + | compiled-in + (SAME / FOLD / CHG) | main SDK / cross-file needs |
|---|---|---|---|---|---|---|
| DEV | 14 | 393 | 13 | 100 | 293 (72 / 221 / 0) | SDK:LAD CHG TCH I2C FL RTC DSP; main.cpp callers (begin(true):1079, startTouch:1126, resetTouchForSleep:817,857, startDeepSleepMinimal:818,858,1052) |
| KEY | 24 | 177 | 67 | 0 | 177 (162 / 15 / 0) | SDK:BTN only |
| FL | 20 | 451 | 56 | 99 | 352 (147 / 200 / 5) | SDK:FL DEV; main.cpp double-click state machine (:1686-1790) and notices (:195,220-221) |
| BAT | 16 | 220 | 29 | 0 | 220 (3 / 217 / 0) | SDK:PP BAT OPDS; src/util/PluginHttp.*, PluginEvents.*, main.cpp:534,1565 |
| RAD | 21 | 234 | 43 | 0 | 234 (3 / 231 / 0) | SDK:PP; main.cpp:1514-1623 |
| DISP | 6 | 128 | 17 | 32 | 96 (14 / 82 / 0) | SDK:PP(noChargeableCellNotice) CHG BAT DEV |
| WDOG | 3 | 46 | 0 | 20 | 26 (20 lines of an empty inline `watchdogProgress()` helper with comment, duplicated in HomeActivity and EpubReaderActivity, plus 6 call sites; all fold to nothing) | CAP_POWER_POLICY + src/util/RenderWatchdog.{h,cpp} (343+168 lines, not in scope) |
| BLOG | 3 | 23 | 0 | 23 | 0 | SILKSCREEN_BATLOG (local-only) |
| TEST | 10 | 57 | 2 | 57 | 0 | CROSSPOINT_TEST_SUITE (local-only) |
| X368 | 6 | 35 | 7 | 11 | 24 (SAME) | FREEINK_SILKSCREEN_368 (ignored) |
| GEN | 1 | 9 | 1 | 0 | 9 (FOLD on isSilkscreen) | none (see Q8) |
| UNS | 0 | 0 | 0 | 0 | 0 | C11 (+6/-2) is counted in DEV but is probably dead code |
| Total | 76 | 1773 | 235 | 342 | 1431 | |

Tests (not in the table): test/clipping_sync/{ClippingSyncTest.cpp +26, stubs/HalPowerManager.h +14} BAT; test/test_recursive_dictionary.py +28 GEN; test/test_selection_buttons.py +26 KEY.
Reading of the table: 81% of the added lines (1431 of 1773) compile into every board; it is mostly harmless because it folds on SDK policy values, but it is still flash on the ESP32-C3 (no size was measured; GUESS: a few KB in total, needs `scripts/firmware_size_history.py` before any PR). The TEST column counts as IF-gated about 6 lines that stay compiled in non-test builds (OptionPopup's `static constexpr` 3, `preventAutoSleep()` scaffolding).

## 2. Hunk table (new-side line ranges at d076703a)

### lib/hal
| ID | file:lines | +/- | what it does | bkt | gating | requires / entanglement |
|---|---|---|---|---|---|---|
| C01 | HalGPIO.h:42-44 | +3 | `friend struct TestSuiteAccess` | TEST | IF CROSSPOINT_TEST_SUITE | |
| C02 | HalGPIO.h:74-78 | +5/-2 | `begin(bool deferTouch=false)`, `startTouch()` decls | DEV | SAME (default arg) | SDK:TCH; only caller main.cpp:1079,1126 |
| C03 | HalGPIO.h:92-93 | +2/-1 | comment on rawInputActive | DEV | SAME | |
| C04 | HalGPIO.h:96-100 | +5 | `hasFrontButtons()` decl + comment | KEY | SAME | SDK:BTN (via C12) |
| C05 | HalGPIO.h:101-108 | +8 | decls `prepareForSleep/prepareForMinimalSleep/resetTouchForSleep` | DEV | FOLD (FREEINK_CAP_TOUCH, SDK hibernateInSleep) | callers C51, main.cpp:817,857 |
| C06 | HalGPIO.cpp:10-13 | +4 | include BatteryLog.h | BLOG | IF SILKSCREEN_BATLOG | |
| C07 | HalGPIO.cpp:86-120 | +35 | `logLadderPresence()` (:86-106) and `logRefusedTouchId()` (:108-119) boot diagnostics | DEV | IF CAP_ADC_LADDERS / CAP_PROBE_IDENTITY | SDK:LAD TCH `PowerConfig::ladderPresenceProbe` |
| C08 | HalGPIO.cpp:123,146-159 | +15/-2 | `begin(deferTouch)` passes `!deferTouch` to SDK, new `startTouch()` | DEV | SAME; log calls under the C07 IFs | SDK:TCH |
| C09 | HalGPIO.cpp:164-166 | +3 | `update()` calls `BatteryMonitor::pollChargeStatus()` | DEV | FOLD hasChargeStatusLadder() | SDK:CHG |
| C10 | HalGPIO.cpp:193-195 | +3 | `rawInputActive()` early return `adcLadderContact()` | DEV | FOLD hasAdcLadders() | SDK:LAD |
| C11 | HalGPIO.cpp:198-204 | +6/-2 | `rawInputActive()` lambda also counts a decoded key (`g.button>=0`) | DEV (UNS) | SAME; GUESS identical on X3/X4 (bands <3900), unreachable on AdcLadders boards (return at :195): drop it | |
| C12 | HalGPIO.cpp:213 | +1 | `hasFrontButtons() = !hasTouch() OR ACTIVE.touch.buttonsAlongsideTouch` | KEY | SAME (field false on every board except SILKSCREEN profile, BoardConfig.h:2604) | SDK:BTN |
| C13 | HalGPIO.cpp:214-225 | +12 | `prepareForSleep()` touch sleep command + result logging | DEV | FOLD (SDK returns NotApplicable unless hibernateInSleep, InputManager.cpp:1799) | SDK:TCH |
| C14 | HalGPIO.cpp:226-228 | +3 | batlog flag TouchNotAsleep | BLOG | IF SILKSCREEN_BATLOG | |
| C15 | HalGPIO.cpp:229-255 | +27 | rest of `prepareForSleep()`, `prepareForMinimalSleep()`, `resetTouchForSleep()` | DEV | FOLD as C13 | SDK:TCH |
| C16 | HalGPIO.cpp:330-343 | +14 | `isUsbConnected()` infers USB from the charge-status ladder (no VBUS sense) | DEV | FOLD hasChargeStatusLadder() | SDK:CHG; feeds C57 (ceiling) and C152 |
| C17 | HalClock.cpp:3,14-29 | +17 | RTC identity-reject log, "RTC time invalid" log, new `hasValidTime()` | DEV | FOLD (rejectedAddress()!=0, rtcResyncWhenInvalid); `hasValidTime()` ungated new fn | SDK:RTC; used by C155 |
| C18 | HalClock.h:10-12 | +3 | friend | TEST | IF | |
| C19 | HalClock.h:31-35 | +5 | `hasValidTime()` decl | DEV | SAME | C17 |
| C20 | HalDisplay.cpp:63,71,89,111,127 | +5/-5 | `gpio.deviceIsX3()` -> `isX3Panel()` | X368 | SAME | C21 |
| C21 | HalDisplay.h:5,12-26 | +16 | include HalGPIO.h, `isX3Panel()` (SDK `isX3Mode()` only under FREEINK_SILKSCREEN_368) | X368 | SAME elsewhere | |
| C22 | HalDisplay.h:146-148 | +3 | friend | TEST | IF | |
| C23 | HalStorage.h:79-81 | +3 | friend | TEST | IF | |
| C24 | HalFrontlight.cpp:3-18 | +12 | includes, `darkBeforeRestart`, `hasColourSelect()`, `warmthIsSelection()` | DEV | SAME (false without colour-select GPIO) | SDK:FL |
| C25 | HalFrontlight.cpp:27-38 | +12 | `esp_register_shutdown_handler` so the light is dark before any restart | DEV | FOLD (only if colorSelectGpio/enablePulseUs/monitorAdc set) | SDK:FL |
| C26 | HalFrontlight.cpp:42-51,62-64 | +12 | `begin()` skips restore after "no light" result; probe hooks in begin/setWarmth | DEV | IF CAP_FRONTLIGHT_PROBE | SDK:FL |
| C27 | HalFrontlight.cpp:68-71 | +4/-1 | `setOn()` fault-aware (clearFault, re-arm) | DEV | SAME while `fault()` is false | SDK:FL |
| C28 | HalFrontlight.cpp:74-112 | +38 | `update()`, `noteSwitchOn()`, `settleSwitchOn()` load-probe state machine | DEV | IF CAP_FRONTLIGHT_PROBE | SDK:FL; callers C209, main.cpp:220-221 |
| C29 | HalFrontlight.h:15-20 | +6 | `warmthIsSelection/hasColourSelect` decls | DEV | SAME | C24 |
| C30 | HalFrontlight.h:28-33 | +6/-1 | `isOn() = lit AND NOT fault()`, `fault()` | DEV | SAME unless SDK fault flag set | SDK:FL; every `Frontlight.isOn()` caller |
| C31 | HalFrontlight.h:34-83 | +50 | `update/switchOnPending/noLightFound/stringMissing/takeNoLightNotice/takeRestoreNoLightNotice` | DEV | FOLD (bodies constant false without PROBE) | SDK:FL; consumers C209, C143 |
| C32 | HalFrontlight.h:84-88 | +5 | `prepareForSleep()` passthrough | DEV | SAME (SDK no-op unless gated light) | called by C54, C25 |
| C33 | HalFrontlight.h:91-93 | +3 | friend | TEST | IF | |
| C34 | HalFrontlight.h:101-113 | +13 | `Pending`, notice flags | DEV | IF CAP_FRONTLIGHT_PROBE | |
| C35 | HalPowerManager.h:22-23 | +2 | `_noCellCached`, `_noCellPolledMs` | DISP | FOLD (RAM +16 B) | C61 |
| C36 | HalPowerManager.h:28-29 | +2 | `lastClientTrafficMs`, `lastInputMs` | RAD | FOLD (RAM) | C40 |
| C37 | HalPowerManager.h:30 | +1 | `cutoffTripped` latch | BAT | FOLD | C56 |
| C38 | HalPowerManager.h:31-32 | +2 | `sleepImpl` decl | DEV | SAME | C50 |
| C39 | HalPowerManager.h:63-66 | +4 | `startDeepSleepMinimal()` decl | DEV | SAME | main.cpp:818,858,1052 |
| C40 | HalPowerManager.h:67-91 | +25 | `noteClientTraffic/msSinceClientTraffic`, `noteInput/msSinceInput`, `awakeCeilingMs/Passed`, `awakeInputCapPassed` decls | RAD | FOLD (awakeCeilingMin==0) | SDK:PP; callers C114, C120-121, main.cpp |
| C41 | HalPowerManager.h:92-107 | +16 | `backgroundNetworkMustStop`, `lowBatteryCutoffTripped` decls | BAT | FOLD | SDK:PP; first fn mixes BAT+RAD (see C58) |
| C42 | HalPowerManager.h:108-114 | +7 | `radioOffWhenIdle`, `releaseIdleRadio` decls | RAD | FOLD | SDK:PP |
| C43 | HalPowerManager.h:115-120 | +6 | `firmwareUpdateBlockedByBattery` decl | BAT | FOLD | SDK:PP BAT |
| C44 | HalPowerManager.h:124-129 | +6 | `batteryCachedTenths/atFullClock` accessors | BLOG | IF SILKSCREEN_BATLOG | |
| C45 | HalPowerManager.h:130-139 | +10 | `noChargeableCell()` decl, `NO_CELL_CLEAR_MV = CELL_PRESENT_MIN_MV + 100` | DISP | SAME (decl; constant needs SDK:BAT) | SDK:BAT |
| C46 | HalPowerManager.cpp:6 | +1 | include SharedI2c.h | DEV | SAME | SDK:I2C |
| C47 | HalPowerManager.cpp:11 | +1 | include algorithm (std::max in C57) | RAD | SAME | |
| C48 | HalPowerManager.cpp:14 | +1 | include HalFrontlight.h | DEV | SAME | |
| C49 | HalPowerManager.cpp:25-28 | +4 | include BatteryLog.h | BLOG | IF | |
| C50 | HalPowerManager.cpp:92-99 | +8 | `startDeepSleep` becomes a wrapper of `sleepImpl`; new `startDeepSleepMinimal` | DEV | SAME | needs C38/C39; callers main.cpp |
| C51 | HalPowerManager.cpp:106-118 | +13 | touch sleep command (or INT hold) then `releaseSensorBusForSleep()` before serial teardown | DEV | FOLD (SDK returns true/NotApplicable elsewhere) | SDK:TCH I2C; C05 |
| C52 | HalPowerManager.cpp:119-121 | +3 | batlog SdaStuck flag | BLOG | IF | |
| C53 | HalPowerManager.cpp:122-123 | +2 | closing brace | DEV | SAME | |
| C54 | HalPowerManager.cpp:167-171 | +5 | `Frontlight.prepareForSleep()` before parked-pin holds | DEV | SAME (SDK no-op unless gated light) | SDK:FL |
| C55 | HalPowerManager.cpp:199-202 | +4 | low CPU clock during the bounded power-key release wait | RAD | FOLD (releaseWaitMaxMs) | SDK:PP |
| C56 | HalPowerManager.cpp:208-220 | +13 | `lowBatteryCutoffTripped()` | BAT | FOLD (cutoffMv==0 returns false before any SDK/WiFi call) | SDK:PP BAT |
| C57 | HalPowerManager.cpp:221-236 | +16 | `awakeCeilingMs/Passed`, `awakeInputCapPassed` | RAD | FOLD | SDK:PP; uses `gpio.isUsbConnected()` (C16) |
| C58 | HalPowerManager.cpp:237-240 | +4 | `backgroundNetworkMustStop() = cutoff OR ceiling OR cap` | BAT (+RAD) | FOLD | split blocker: BAT-only version is just `lowBatteryCutoffTripped()` |
| C59 | HalPowerManager.cpp:241-250 | +10 | `radioOffWhenIdle()`, `releaseIdleRadio(tag)` | RAD | FOLD | SDK:PP |
| C60 | HalPowerManager.cpp:251-265 | +15 | `firmwareUpdateBlockedByBattery()` | BAT | FOLD (firmwareUpdateMinMv==0) | SDK:PP BAT |
| C61 | HalPowerManager.cpp:266-287 | +22 | `noChargeableCell()` | DISP | FOLD (noChargeableCellNotice false returns first) | SDK:PP CHG BAT |
| C62 | HalPowerManager.cpp:288-319 | +32 | `silkscreenShownPercent()` + static_asserts, 5% steps and 98->100 | DISP | IF FREEINK_DEVICE_SILKSCREEN AND NOT SILKSCREEN_BATLOG | uses downstream macro; C67 |
| C63 | HalPowerManager.cpp:374-387 | +13/-1 | `getBatteryPercentage()`: Silkscreen 16-sample average + `percentTenthsFromMillivolts`; others `readPercentage()*10` | DISP | FOLD on `isSilkscreen()` (other boards identical) | SDK:BAT DEV |
| C64 | HalPowerManager.cpp:388 | +1/-1 | first-sample seed | DISP | SAME | |
| C65 | HalPowerManager.cpp:389-391 | +3 | batlog `noteUiSeed` | BLOG | IF | |
| C66 | HalPowerManager.cpp:392-400 | +7/-1 | Silkscreen rounded averaging; others unchanged formula | DISP | FOLD isSilkscreen() | |
| C67 | HalPowerManager.cpp:401-410 | +10/-1 | return value: 5% steps (non-batlog Silkscreen), rounded (batlog), floor (others) | DISP | IF/FOLD (see C62) | entangled with BLOG via `!SILKSCREEN_BATLOG` |

### settings, mapped input, strings
| ID | file:lines | +/- | what it does | bkt | gating | requires / entanglement |
|---|---|---|---|---|---|---|
| C68 | CrossPointSettings.cpp:67-79 | +13 | `boardHasPowerDoubleClickLight()` (X4 Pro OR Silkscreen with PWM light), `powerDoubleClickLight()` | FL | FOLD (`#if CAP_FRONTLIGHT`, isSilkscreen); X4 Pro unchanged | SDK:DEV; main.cpp:374,1686 |
| C69 | CrossPointSettings.cpp:120-122 | +3/-2 | comment on by-value persistence | FL | SAME | |
| C70 | CrossPointSettings.cpp:211-215 | +4 | `pluginsEnabled=0` in test firmware | TEST | IF | |
| C71 | CrossPointSettings.cpp:269-276 | +8/-5 | `fromJson`: `longPressMenuValueAccepted/shortPowerButtonValueAccepted` replace `clamp(..COUNT..)` | FL | SAME (accepts 0-6 and 0-9 on non-Silkscreen, same as upstream clamp) | C75, C79 |
| C72 | CrossPointSettings.h:154-185 | +10/-3 | enumerators `TOGGLE_FRONTLIGHT = 10`, `LP_MENU_FRONTLIGHT_PANEL = 7` (+comments) | FL | SAME | appended after upstream's 9 and 6 |
| C73 | CrossPointSettings.h:289-296 | +6/-1 | decls `boardHasPowerDoubleClickLight`, `powerDoubleClickLight` | FL | FOLD | C68 |
| C74 | SettingsList.h:184-194 | +11 | `keyFrontlightAvailable()` | FL | FOLD (CAP_FRONTLIGHT, isSilkscreen) | |
| C75 | SettingsList.h:195-228 | +34 | `LongPressMenuValues`, `longPressMenuValues()`, `longPressMenuValueAccepted()` | FL | SAME (same list on other boards) | prerequisite refactor for value 7 |
| C76 | SettingsList.h:229-277 | +41/-18 | table-driven `buildLongPressMenuValues/DisplayValue/setFromDisplayValue` replace `raw-1` arithmetic | FL | SAME (GUESS flash +0.2-0.4 KB on C3) | C75 |
| C77 | SettingsList.h:278-305 | +4/-1 | comment for short-power options | FL | SAME | |
| C78 | SettingsList.h:306-310 | +5/-2 | Bookmark/Sync/Reader Menu short-power options also require `!hasFrontButtons()` | FL (+KEY predicate) | SAME (upstream boards answer `!hasTouch()`) | split blocker FL/KEY: one condition on line 310 |
| C79 | SettingsList.h:311-339 | +16 | Toggle Frontlight option; `shortPowerButtonValueAccepted()` | FL | FOLD (keyFrontlightAvailable); `isSilkscreen()` branch | C72, C74 |
| C80 | SettingsList.h:340-364 | +12 | `shortPowerButtonToggleFrontlightIndex()` for the web page | FL | SAME | C117, C130 |
| C81 | SettingsList.cpp:53-56 | +4/-1 | Sunlight-fading-fix toggle shown when `hasFrontButtons()` | KEY | SAME | SDK:BTN; also Silkscreen shows it because the glass is X4's |
| C82 | SettingsList.cpp:172-173 | +2/-1 | Front-button-follow-orientation toggle | KEY | SAME | SDK:BTN |
| C83 | SettingsList.cpp:180-181 | +2 | comment | FL | SAME | |
| C84 | SettingsList.cpp:183-187 | +3/-2 | double-click toggle row via `boardHasPowerDoubleClickLight()` (was `isX4Pro()`) | FL | FOLD; X4 Pro unchanged | C68 |
| C85 | SettingsList.cpp:195 | +1/-1 | Back-short-to-file-browser toggle | KEY | SAME | SDK:BTN |
| C86 | MappedInputManager.cpp:21-22 | +2 | reset `powerReleaseHidden/powerClickFrame` each update | FL | FOLD | C91 |
| C87 | MappedInputManager.cpp:72,75 | +2/-2 | `isNavDirectionSwapped()` uses `!hasFrontButtons()` | KEY | SAME | SDK:BTN |
| C88 | MappedInputManager.cpp:190-191 | +2 | `hasFrontButtons()` wrapper | KEY | SAME | C12 |
| C89 | MappedInputManager.cpp:381-384 | +4/-4 | `wasPowerConfirmClick()` uses `SETTINGS.powerDoubleClickLight()` (was `isX4Pro() AND doubleClickPwrLight`) | FL | SAME (X4 Pro: PWR_CONFIRM implies not TOGGLE) | C68 |
| C90 | MappedInputManager.cpp:420-423 | +4 | `wasReleased(Power)` returns the deferred click / hides the release | FL | FOLD (flags set only by main.cpp:1704) | C91 |
| C91 | MappedInputManager.h:49-59 | +10/-2 | `setPowerClickDeferral(hide, deliver)`, comment | FL | FOLD | main.cpp:1696-1760 |
| C92 | MappedInputManager.h:71-73 | +3 | `hasFrontButtons()` decl | KEY | SAME | |
| C93 | MappedInputManager.h:174-175 | +2 | members | FL | FOLD | |
| C132 | english.yaml:593 | +1 | `STR_BATTERY_TOO_LOW_FOR_UPDATE` | BAT | SAME (string in every board) | C193, C197 |
| C133 | english.yaml:621-623 | +3 | `STR_FRONTLIGHT_NOT_DETECTED`, `_COOL_ONLY`, `_WARM_ONLY` | FL | SAME | C209, C104 |
| C134 | english.yaml:624 | +1 | `STR_POWER_BUTTON_FAULT` (174 B, contains "R36 and R72") | DEV | SAME | main.cpp:782-785 |
| C135 | english.yaml:625 | +1 | `STR_NO_BATTERY` | DISP | SAME | C99 |

### components
| ID | file:lines | +/- | what it does | bkt | gating | requires / entanglement |
|---|---|---|---|---|---|---|
| C94 | CoverGridHomeUi.cpp:106-110,116-117,232-236 | +9/-9 | top inset, tab gap, hero accent bar keyed on `!hasFrontButtons()` | KEY | SAME | SDK:BTN |
| C95 | OptionPopup.h:75-80,226,298-302 | +12/-1 | `headlineMaxLines` configurable only in test firmware (`static constexpr 3` otherwise) | TEST | IF (production: same value 3) | |
| C96 | UITheme.cpp:76-84 | +6/-6 | cached metrics keyed on `gpio.hasFrontButtons()`; button-hint height 0 only without keys | KEY | SAME | C12 |
| C97 | UITheme.h:56 | +1/-1 | member rename `metricsForFrontButtons` | KEY | SAME | |
| C98 | UiSliderDialog.h:51-52,59,127 | +4/-2 | step hints shown when `hasFrontButtons()` (pill still for touch) | KEY | SAME | |
| C99 | BaseTheme.cpp:84-93 | +9 | `batteryLabel()` ("No battery" else "NN%", snprintf instead of std::to_string); `fillBatteryIcon` returns early if no chargeable cell | DISP | FOLD (`noChargeableCell()` false) | C61, C135; C106/C107 |
| C100 | BaseTheme.cpp:125-127 | +3/-3 | `drawBatteryLeft` uses `batteryLabel` | DISP | SAME | |
| C101 | BaseTheme.cpp:218,253 | +2/-2 | `drawButtonHints`, `drawSideButtonHints` skip when `!hasFrontButtons()` | KEY | SAME | |
| C102 | BaseTheme.cpp:337-347 | +8/-6 | `applyHeaderStatus`: no-cell glyph and label | DISP | FOLD | |
| C103 | BaseTheme.cpp:756-795 | +19/-10 | extract `drawPopupFrame()` always-inline helper out of `drawPopup()` | FL | SAME (identical drawing) | only needed by C104 |
| C104 | BaseTheme.cpp:796-826 | +31 | `drawWrappedPopup(msg, maxLines)` | FL | FOLD (unused, GC-able, outside Silkscreen) | callers main.cpp:195 (FL) and :785 (DEV power-button fault); C106 |
| C105 | BaseTheme.cpp:931-935 | +4/-4 | `drawStatusBar` battery width via `batteryLabel` | DISP | SAME | |
| C106 | BaseTheme.h:292-294 | +3 | `drawWrappedPopup` decl | FL | FOLD | |
| C107 | BaseTheme.h:310-314 | +5 | `batteryLabel` decl | DISP | SAME | |
| C108 | LyraTheme.cpp:70 | +1 | `fillBatteryIcon` early return if no cell | DISP | FOLD | C61 |
| C109 | LyraTheme.cpp:113,157 | +2/-2 | button hints skip when `!hasFrontButtons()` | KEY | SAME | |
| C110 | RoundedRaffTheme.cpp:203 | +1/-1 | same | KEY | SAME | |

### network, clippings
| ID | file:lines | +/- | what it does | bkt | gating | requires / entanglement |
|---|---|---|---|---|---|---|
| C111 | CrossPointWebServer.cpp:9 | +1 | include HalDisplay.h | X368 | SAME | |
| C112 | CrossPointWebServer.cpp:12 | +1 | include HalPowerManager.h | BAT (shared with RAD) | SAME | |
| C113 | CrossPointWebServer.cpp:38 | +1 | include CutoffAbortPrint.h | BAT | SAME | C122 |
| C114 | CrossPointWebServer.cpp:205-229 | +25 | middleware: every non-poll request calls `noteClientTraffic()` | RAD | FOLD (registered only if awakeCeilingMin>0) | SDK:PP; plugin-job poll excluded (C120) |
| C115 | CrossPointWebServer.cpp:587-590 | +4 | `/api/status` device "X3" on the 3.68" variant | X368 | IF | docs line |
| C116 | CrossPointWebServer.cpp:729-732,749 | +5/-1 | `streamFileToClient` writes through `CutoffAbortPrint` | BAT | FOLD | C122 |
| C117 | CrossPointWebServer.cpp:1437-1442 | +6 | `/api/settings` adds `toggleFrontlightIndex` for shortPwrBtn | FL | FOLD (absent unless Toggle Frontlight offered) | C80, C130; not in docs/webserver-endpoints.md |
| C118 | CrossPointWebServer.cpp:2026-2028 | +3/-1 | `handleRelay` abort callback returns cutoff | BAT | FOLD | |
| C119 | CrossPointWebServer.cpp:2392-2394 | +3/-1 | `handleFetch` abort callback returns cutoff | BAT | FOLD | |
| C120 | CrossPointWebServer.cpp:2635-2637 | +3 | claimed plugin job counts as client traffic | RAD | FOLD | C114 |
| C121 | CrossPointWebServer.cpp:2719 | +1 | WebSocket frame counts as client traffic | RAD | FOLD (a `millis()` store on all boards) | |
| C122 | CutoffAbortPrint.h:1-26 | +26 | new `Print` wrapper whose `write()` returns 0 once the cutoff has latched | BAT | FOLD | used by C116, C129 |
| C123 | FirmwareBoardTag.cpp:38-44 | +7 | tags `silkscreen-368(-test)` | X368 | IF | |
| C124 | FirmwareBoardTag.cpp:45-47 | +3 | tag `silkscreen-test` | TEST | IF | |
| C125 | FirmwareBoardTag.cpp:48-49 | +2 | tag `silkscreen` | DEV | IF FREEINK_DEVICE_SILKSCREEN | the one line any device PR needs |
| C126 | HttpDownloader.cpp:4,37-41,74-80,156-172,199-211,253 | +44/-19 | cutoff poll in every wait loop: `cutoffTripped()` helper, `runGetSecure`/`postToFile` abort lambdas, `fetchUrl`/`postForm` pass it and honour `http.aborted()` (about 20 lines are lambda re-indentation) | BAT | FOLD | SDK:PP BAT; base SDK already has `GET(cb, shouldAbort)` |
| C127 | OpdsHttpTransport.cpp:3-4,35-37 | +5 | `shouldAbort()` override | BAT | FOLD | SDK:OPDS (override fails to compile on base SDK) |
| C128 | OpdsHttpTransport.h:18-20 | +3 | decl | BAT | FOLD | SDK:OPDS |
| C129 | WebDAVHandler.cpp:4,9,91-99,359-363 | +16/-1 | PUT stops and drops the connection on cutoff; GET streams through `CutoffAbortPrint` | BAT | FOLD | C122 |
| C130 | SettingsPage.html:405-412 | +8 | hides the double-click row while Toggle Frontlight is chosen (by list position from C117) | FL | FOLD | C117 |
| C131 | ClippingSync.cpp:6,24-29 | +7 | `request()` refuses at the cutoff | BAT | FOLD | test C219 |

### activities (ActivityManager, home, library, network, plugins)
| ID | file:lines | +/- | what it does | bkt | gating | requires / entanglement |
|---|---|---|---|---|---|---|
| C136 | Activity.h:53-55 | +3 | virtual `showsFrontlightNotice()` | FL | FOLD (new vtable slot in every Activity subclass, GUESS <0.3 KB) | C143, C217, main.cpp:204,221 |
| C137 | ActivityManager.h:124 | +1 | decl | FL | SAME | |
| C138 | ActivityManager.cpp:36-38 | +3 | include RenderWatchdog.h | WDOG | IF CAP_POWER_POLICY | src/util/RenderWatchdog.* |
| C139 | ActivityManager.cpp:39-41 | +3 | include TestSuite.h | TEST | IF | |
| C140 | ActivityManager.cpp:77-80 | +4 | `renderwatchdog::start()` | WDOG | IF CAP_POWER_POLICY | comment cites POWER.md AW-04 |
| C141 | ActivityManager.cpp:100-107 | +7 | `beginRender(name)`/`endRender()` around `render()` | WDOG | IF CAP_POWER_POLICY | |
| C142 | ActivityManager.cpp:145,164,291-292,302,317,376,384,402,426-432 | +16/-1 | `#if !CROSSPOINT_TEST_SUITE` around control-center entry and goTo*; USB-host branch in `preventAutoSleep()` | TEST | IF (non-test builds: same code) | |
| C143 | ActivityManager.cpp:438-441 | +4 | `showsFrontlightNotice()` | FL | FOLD | C136 |
| C144 | CatalogActivity.cpp:6 | +1 | include HalPowerManager.h (shared with C146) | RAD | SAME | |
| C145 | CatalogActivity.cpp:26-30 | +5/-3 | `onExit` restarts also when `radioReleased` | RAD | FOLD | C147, C160, C196 |
| C146 | CatalogActivity.cpp:165-167,207-211 | +8 | download progress cancels on cutoff; `finishDownload` returns on cutoff | BAT | FOLD | |
| C147 | CatalogActivity.h:39-41 | +3 | `radioReleased` member | RAD | FOLD | |
| C148 | SleepActivity.cpp:888 | +1/-1 | `display.isX3Panel()` | X368 | SAME | |
| C149 | HomeActivity.cpp:28-41,184 | +15 | include + `watchdogProgress()` helper, called per cover | WDOG | IF inside an empty inline | duplicate of C167 |
| C150 | LibraryListActivity.cpp:1035 | +1/-1 | hold-help line keyed on `hasFrontButtons()` | KEY | SAME | |
| C151 | CrossPointWebServerActivity.cpp:8,315-317 | +4/-1 | upload pump aborts on cutoff | BAT | FOLD | |
| C152 | UsbDriveActivity.cpp:4-5,52-57,194-212 | +27 | `cablePulledOnBattery()` ends USB Drive session when the charge ladder says cable gone for 3 s | DEV | FOLD (hasChargeStatusLadder) | SDK:CHG; C153 |
| C153 | UsbDriveActivity.h:27-29,34,46-47 | +6 | timeout const, method, members | DEV | FOLD | |
| C154 | WifiSelectionActivity.cpp:3 | +1 | include BoardConfig.h | DEV | SAME | |
| C155 | WifiSelectionActivity.cpp:525-529 | +5/-2 | NTP re-sync when the RTC reports invalid time | DEV | FOLD (rtcResyncWhenInvalid) | SDK:RTC; C17/C19 |
| C156 | WifiSelectionActivity.cpp:536,540,549,554 | +4 | `#if !CROSSPOINT_TEST_SUITE` around loan-clock sync and plugin drain | TEST | IF | |
| C157 | WifiSelectionActivity.cpp:1030-1032 | +3/-3 | "Press OK to scan" hint when `hasFrontButtons()` | KEY | SAME | |
| C158 | PluginCatalogActivity.cpp:7,55-58 | +5 | include + `abortAtCutoff()` | BAT | FOLD | comment cites BAT-01 / POWER.md |
| C159 | PluginCatalogActivity.cpp:340-341,445-446 | +4/-2 | `abortAtCutoff` passed to `pluginhttp::request` | BAT | FOLD | `request()` already has the param upstream |
| C160 | PluginCatalogActivity.cpp:521-527 | +7/-2 | `exitCatalog()` releases the idle radio | RAD | FOLD | C145; GEN candidate G4 |
| C161 | PluginCatalogActivity.cpp:641,669,1423-1424 | +4/-3 | `abortAtCutoff` to `mintPasswordToken`/`requestToFile`/`request` | BAT | FOLD | needs new params in src/util/PluginHttp.{h,cpp} (46 changed lines, not in scope) |

### activities (reader, settings, util)
| ID | file:lines | +/- | what it does | bkt | gating | requires / entanglement |
|---|---|---|---|---|---|---|
| C162 | ClipSelectionActivity.cpp (22 hunks, :107-755) | +72/-16 | with keys beside touch: key loop while no finger is down (`touchInProgress()`), touch hit tests shifted by `textXOffset()`, Look Up/Clip/Bookmark list on 2nd Confirm (`keysChooseAction/showActionPopup/applyChosenAction`), `wordAtPage` split | KEY | SAME (every branch needs `hasFrontButtons()` with touch; no-op on upstream boards, checked per hunk) | SDK:BTN; test C221 |
| C163 | ClipSelectionActivity.h:74,91-94,113-114 | +7 | decls, `chosenAction` | KEY | SAME | |
| C164 | DictionaryDefinitionActivity.cpp:4,533-540 | +9/-1 | a cancelled word selection forgets the remembered range | GEN | FOLD (`isSilkscreen()`; other boards keep old behaviour) | test C220; see Q8 G1 |
| C165 | EpubReaderActivity.cpp:12 | +1 | include HalPowerManager.h | RAD | SAME | |
| C166 | EpubReaderActivity.cpp:55 | +1 | include FrontlightPanelActivity.h | FL | SAME | |
| C167 | EpubReaderActivity.cpp:63-76 | +12 | RenderWatchdog include + `watchdogProgress()` helper | WDOG | IF inside empty inline | duplicate of C149 |
| C168 | EpubReaderActivity.cpp:80-85 | +5/-2 | `xteinkClassPanel()` also true for profiles declaring X4-class glass whose driver runs the grayscale pass | DEV | FOLD (displayProbePolicy default false) | SDK:DSP |
| C169 | EpubReaderActivity.cpp:351 | +1/-1 | `panelCursorShown = hasFrontButtons()` | KEY | SAME | |
| C170 | EpubReaderActivity.cpp:416 | +1 | watchdog call in `showBuildPopup` | WDOG | IF | |
| C171 | EpubReaderActivity.cpp:851-858 | +8 | long-press Confirm case `LP_MENU_FRONTLIGHT_PANEL` opens the panel | FL | FOLD (value 7 accepted only on Silkscreen) | C72, C75 |
| C172 | EpubReaderActivity.cpp:1304-1305 | +2 | long-press threshold for that case | FL | FOLD | |
| C173 | EpubReaderActivity.cpp:1517-1530 | +6 | loan-time sync: `releaseIdleRadio` after cancelled/failed picker | RAD | FOLD | GEN candidate G5 |
| C174 | EpubReaderActivity.cpp:1683,1772,1842,1853 | +4 | watchdog calls in build loops | WDOG | IF | |
| C175 | EpubReaderActivity.cpp:2067 | +1/-1 | `display.isX3Panel()` | X368 | SAME | |
| C176 | EpubReaderActivity.cpp:2591-2593,2682-2684,2711-2712 | +8/-6 | panel cursor shown from the start with keys; comments | KEY | SAME | |
| C177 | EpubReaderActivity.h:77-79 | +3/-2 | comment | KEY | SAME | |
| C178 | EpubReaderBookmarksActivity.cpp:264-265 | +2/-2 | help line keyed on `hasFrontButtons()` | KEY | SAME | |
| C179 | EpubReaderClippingListActivity.cpp:189-190 | +2/-1 | same | KEY | SAME | |
| C180 | EpubReaderFootnoteSelectActivity.cpp:3,64-73 | +10/-1 | Power confirms only when not Toggle Frontlight and not deferred by the double-click | FL | FOLD (`isSilkscreen()`, value 10 never set elsewhere) | C68 |
| C181 | KOReaderSyncActivity.cpp:4,333-336,724-732 | +14 | modem sleep on result screens, `releaseIdleRadio` on failure, re-disable before upload | RAD | FOLD | C182 |
| C182 | KOReaderSyncActivity.h:78-79 | +2 | `modemSleepOn` | RAD | FOLD | |
| C183 | ClearCacheActivity.cpp:4,87-88 | +3 | `skipLoopDelay()` out of line | RAD | FOLD (returns old `true` unless radioOffWhenIdle) | G2 |
| C184 | ClearCacheActivity.h:16-18 | +3/-1 | decl | RAD | FOLD | |
| C185 | ClockSyncActivity.cpp:5,36-46,98-100 | +11/-3 | `radioReleased`, release radio after sync result, `skipLoopDelay()` | RAD | FOLD | G2, G4 |
| C186 | ClockSyncActivity.h:16-18,26-28 | +6/-1 | decls | RAD | FOLD | |
| C187 | FontDownloadActivity.cpp:6,81-102,554 | +24 | `preventAutoSleep()` (result screens: refresh once, then the user's timeout) and `skipLoopDelay()` | RAD | FOLD (old constants when radioOffWhenIdle false) | G2, G3 |
| C188 | FontDownloadActivity.cpp:615-617 | +3 | cancel download on cutoff | BAT | FOLD | |
| C189 | FontDownloadActivity.h:36-37,105-106 | +4/-8 | decls, `resultPolled_` | RAD | FOLD | |
| C190 | KOReaderAuthActivity.cpp:4,74-78,115-116 | +8/-3 | release radio at result; `onExit` condition | RAD | FOLD | G4 |
| C191 | KOReaderAuthActivity.h:32-34 | +3 | `radioReleased` | RAD | FOLD | |
| C192 | OtaUpdateActivity.cpp:4,15-25,46,51,57 | +15/-12 | `finishCheck(state, detail)` replaces three state-set blocks and releases the radio | RAD | FOLD | C193 depends on it |
| C193 | OtaUpdateActivity.cpp:70-76 | +7 | refuse install when `firmwareUpdateBlockedByBattery()` | BAT | FOLD | uses `finishCheck` (C192) and `STR_BATTERY_TOO_LOW_FOR_UPDATE` |
| C194 | OtaUpdateActivity.cpp:104-110,206 | +8/-9 | `onExit` radioReleased; install failure via `finishCheck` | RAD | FOLD | |
| C195 | OtaUpdateActivity.h:3-4,34-36,40-41,51-55 | +12/-1 | include, `radioReleased`, `finishCheck`, `skipLoopDelay()` | RAD | FOLD | |
| C196 | PluginHubInstallActivity.cpp:3,142-144 | +4 | release radio at the Done screen | RAD | FOLD | G4; comment cites AW-02 / POWER.md |
| C197 | SdFirmwareUpdateActivity.cpp:5,70-76,162-168 | +15 | battery gate at validate and again before the write | BAT | FOLD | SDK:PP BAT |
| C198 | SettingsActivity.cpp:8,38 | +2 | includes HalFrontlight.h, FrontlightPanelActivity.h | FL | SAME | |
| C199 | SettingsActivity.cpp:63-66 | +4/-2 | long-press-Confirm row hidden only when `!hasFrontButtons()` | KEY | SAME | |
| C200 | SettingsActivity.cpp:71-75 | +5 | hide double-click row while Short Power = Toggle Frontlight | FL | FOLD | |
| C201 | SettingsActivity.cpp:129-131 | +3/-1 | Remap Front Buttons row follows `hasFrontButtons()` | KEY | SAME | |
| C202 | SettingsActivity.cpp:140-144 | +5 | Settings > Display > "Frontlight" row (light present AND keys fitted) | FL | CHG on any keys+light board (none among release envs: every light board has touch) | C203 |
| C203 | SettingsActivity.cpp:428-435 | +8 | handler opens `FrontlightPanelActivity` | FL | FOLD | |
| C204 | SettingsActivity.h:35 | +1 | enum `FrontlightPanel` | FL | SAME | |
| C205 | FrontlightPanelActivity.cpp:68-75 | +6 | onEnter: single-colour warmth lock, no-light note | FL | FOLD (inline stubs without PROBE) | SDK:FL |
| C206 | FrontlightPanelActivity.cpp:120-220 | +8/-3 | brightness/warmth events and steps use `levelSwitchesOn()`, `noteSwitchOnResult()`, `warmthLocked()` | FL | FOLD | C215 |
| C207 | FrontlightPanelActivity.cpp:224-232 | +9 | `stepWarmthByKey()` | KEY | FOLD | C210 |
| C208 | FrontlightPanelActivity.cpp:234-241 | +4/-1 | `toggleLight()` takes the live state `!Frontlight.isOn()` | FL | SAME on boards where nothing else toggles the light; see G6 | C30 |
| C209 | FrontlightPanelActivity.cpp:243-317 | +67 | `noteSwitchOnResult`, `syncLightState`, `warmthLocked`, `singleStringNote`, note drawing; `syncLightState()` call in `loop()` | FL | IF CAP_FRONTLIGHT_PROBE | SDK:FL; C133 |
| C210 | FrontlightPanelActivity.cpp:363-368 | +6 | Up/Down adjust warmth when `hasColorTemperature() AND hasFrontButtons()` | KEY | FOLD (all warm/cool light boards have touch, so false) | SDK:BTN |
| C211 | FrontlightPanelActivity.cpp:382-389,475-483 | +13 | note rows in `computePanelBottom()` and `buildPanelScreen()` | FL | IF CAP_FRONTLIGHT_PROBE | |
| C212 | FrontlightPanelActivity.cpp:527-528 | +2 | `render()` re-reads `Frontlight.isOn()` | FL | SAME; see G6 | |
| C213 | FrontlightPanelActivity.h:9-10 | +2/-1 | comment | FL | SAME | |
| C214 | FrontlightPanelActivity.h:70-72 | +3 | `stepWarmthByKey` decl | KEY | SAME | |
| C215 | FrontlightPanelActivity.h:74-92 | +19 | decls with inline stubs when PROBE is off | FL | FOLD | |
| C216 | FrontlightPanelActivity.h:105-123 | +19 | probe members and helpers | FL | IF CAP_FRONTLIGHT_PROBE | |
| C217 | FrontlightPanelActivity.h:131 | +1 | `showsFrontlightNotice() override` | FL | SAME | C136 |
| C218 | KeyboardEntryActivity.cpp:541-544,1005-1017 | +8/-4 | touch keyboard stops above the hint band; key cursor wherever keys exist; bottom edge from metrics | KEY | SAME (hint height is 0 on touch-only boards) | |
| C219 | test/clipping_sync/* | +40 | host test + HalPowerManager stub for the cutoff in ClippingSync | BAT | test only | |
| C220 | test/test_recursive_dictionary.py | +28 | pins the Silkscreen-only forget-range behaviour | GEN | test only | |
| C221 | test/test_selection_buttons.py | +26 | pins the keys-beside-touch loop() paths | KEY | test only | |

## 3. Answers

### Q1 KEYACCESS
- Hunks (+177/-67, 24 files): predicate plumbing C04 C12 C88 C92; settings rows C81 C82 C85 C199 C201; nav swap C87; chrome C94 C96 C97 C98 C101 C109 C110; help lines and panel cursor C150 C157 C169 C176 C177 C178 C179; clip/lookup selection C162 C163; warmth keys C207 C210 C214; keyboard C218. Two neighbours are bucketed FL because they carry another reason: C78 (one `!hasFrontButtons()` condition on SettingsList.h:310) and C202 (Settings > Display > Frontlight row). Consumers: 10 static (`BoardConfig::hasFrontButtons()`: SettingsList.cpp:56,173,195; SettingsList.h:310; SettingsActivity.cpp:66,131,141; CoverGridHomeUi.cpp:110,117,232) and 20 runtime (`gpio/mappedInput.hasFrontButtons()`, excluding the two wrappers); each keeps the static-vs-runtime flavour of the `hasTouch()` it replaced, so a missing optional touch tail still behaves as keys-only.
- SDK needs, exactly: `TouchConfig::buttonsAlongsideTouch` (BoardConfig.h:824, default false, set true only in the SILKSCREEN profile at :2604) read by `HalGPIO::hasFrontButtons()` (HalGPIO.cpp:213), and the inline `BoardConfig::hasFrontButtons()` (:3239). Verified: neither name exists at base f9cc1be, and no CAP macro is involved.
- Does it do anything on an upstream device today? No. Every predicate swap equals the old `!hasTouch()` while the field is false (checked hunk by hunk, including ClipSelection where every new branch needs touch AND keys). Upstream devices with both keys and touch (base SDK profiles, f9cc1be): X4 Pro (:1814, Up/Down + Power; Back/Confirm come from touch and the capacitive Home key), Sticky (:1560, "up5 down6; OK == power; back/left/right come from touch"), EEGO A4 (:1482, UP DOWN POWER), Paper Mono (:1108, two buttons), Metalio E-Ink 4 (:1675, BOOT=confirm + volume keys), Picco (:1735, one confirm key). None has Back/Left/Right keys, so none can be driven by keys alone and each stays `buttonsAlongsideTouch=false`. Silkscreen is the first board with a full 8-key set beside touch.
- User-visible change, in plain terms, for a Silkscreen with its touch tail fitted: the key-hint strip and key cursors are drawn on touch screens (Home hero accent bar, reader panel cursor shown from the start, "hold Confirm ..." help lines, slider-dialog step hints, keyboard stops above the hint strip); key-only settings stay (front-button remap, follow-orientation, back-short-to-file-browser, fading fix, long-press-Confirm action); clip/lookup word selection runs on Left/Right/Up/Down/Confirm whenever no finger is on the glass and a Look Up / Clip / Bookmark list appears on the second Confirm; the light panel gets Up/Down for warmth. Without the tail it behaves as an X4.
- Before/independent of the device PR? Technically yes in two shapes: (a) app-only predicate PR (`HalGPIO::hasFrontButtons()` defined as `!hasTouch()`, plus the 10 static sites using an app-side helper) with zero behaviour change, no SDK change, about 80 added / 70 removed lines in about 20 files (all of KEY except the new behaviour in C162, C163, C207, C210, C214); (b) the SDK field. It would be a "speculative hook" under scope-discipline (no device uses it yet), so I recommend landing KEY right after the SDK field exists, in the device PR or its first follow-up, not before.

### Q2 BATTERY-SAFETY
- API: `HalPowerManager::lowBatteryCutoffTripped()` (decl HalPowerManager.h:106, def .cpp:208-219): latch `cutoffTripped`; threshold `BoardConfig::powerPolicy().lowBatteryCutoffMv`; reading by SDK `BatteryMonitor::isBelowCutoff(known, &mv, radioOn)` (one reading per second, CUTOFF_SAMPLES=3, never on external power or below CELL_PRESENT_MIN_MV=2500 mV, radio allowance `cutoffRadioAllowanceMv`). Companions: `backgroundNetworkMustStop(timeout)` (.cpp:237-240, also ORs the awake ceiling/cap), `firmwareUpdateBlockedByBattery()` (.cpp:251-265, `firmwareUpdateMinMv`), `CutoffAbortPrint` (Print wrapper, write() returns 0 after the latch).
- No-op elsewhere: without CAP_POWER_POLICY `powerPolicy()` is a constexpr default `PowerConfig{}`, so `cutoffMv==0` returns false before `BatteryMonitor` or `WiFi` is touched; every poll site is a call plus a test. Verified by reading BoardConfig.h:3453-3457 and the function body; flash not measured.
- Files and sizes (BAT only): HalPowerManager.h +23, .cpp +32; CutoffAbortPrint.h +26; HttpDownloader.cpp +44/-19 (about 20 lines are re-indented lambdas); OpdsHttpTransport.cpp +5, .h +3 (needs SDK:OPDS); CrossPointWebServer.cpp +13/-3; WebDAVHandler.cpp +16/-1; ClippingSync.cpp +7; CatalogActivity.cpp +8; CrossPointWebServerActivity.cpp +4/-1; PluginCatalogActivity.cpp +13/-5; FontDownloadActivity.cpp +3; OtaUpdateActivity.cpp +7; SdFirmwareUpdateActivity.cpp +15; english.yaml +1; tests +40. Total +220/-29 (+40). Outside my scope: src/util/PluginHttp.{h,cpp} (46 changed lines, new `shouldAbort` params for `requestToFile`/`mintPasswordToken`), src/util/PluginEvents.{h,cpp} (35), main.cpp:534,1565 (the loop's own check), SDK `PowerConfig` fields, `isBelowCutoff`, `OpdsTransport::shouldAbort`.
- Board-independent safety feature? The poll point is generic (threshold comes from the board profile, 0 = off), but with only Silkscreen setting a threshold it is 220 lines of inert code upstream, and enabling it for X3/X4 changes shipped behaviour (they would sleep at a cutoff they never had; GUESS on what their cell protector does). Recommended: carry it in the device PR or a follow-up that also sets the field; an upstream-first PR only makes sense together with a proposed threshold for an existing ADC board.

### Q3 Files that mix concerns: what a per-bucket patch looks like
HalPowerManager.cpp (+188 total):
- DEV sleep entry (+30): C46, C48, C50, C51, C53, C54 and header C38, C39 (+6). `startDeepSleep` turns into a wrapper over `sleepImpl(gpio, sleepCommands, wakeOnHeldButton)`; adds `startDeepSleepMinimal`, `gpio.prepareForSleep()/prepareForMinimalSleep()`, `releaseSensorBusForSleep()`, `Frontlight.prepareForSleep()`. Needs the HalGPIO touch hunks (C05, C13, C15) and main.cpp callers.
- BAT (+32, header +23): C56, C60, C58 reduced to `return lowBatteryCutoffTripped();`, plus C37, C41, C43.
- RAD (+31, header +34): C47, C55, C57, C59, and the extra `|| awakeCeilingPassed() || awakeInputCapPassed()` that C58 loses in the BAT patch; header C36, C40, C42.
- DISP (+85, header +12): C61 `noChargeableCell`, C62 shown-percent table, C63/C64/C66/C67 the `getBatteryPercentage()` rewrite; header C35, C45. In an upstream patch C62/C67 must lose the `!SILKSCREEN_BATLOG` condition and the `isSilkscreen()` branches should become an SDK capability.
- BLOG (+10, header +6): C49, C52, C65, C44 stay downstream.
HalGPIO.cpp (+123/-4) and .h (+23/-3):
- DEV-ADC (.cpp 25: the ladder half of C07 at :86-107 with 22 lines, plus C10; header C03 +2; C11 +6/-2 should be dropped). DEV-CHARGE (.cpp 17): C09, C16. DEV-TOUCH (.cpp 67, header 13): the touch-id half of C07 (13), C08 (15), C13 (12), C15 (27); header C02, C05. KEY (.cpp 1, header 5): C12, C04. BLOG (.cpp 7): C06, C14. TEST (header 3): C01. The .cpp pieces sum to 123.
SettingsList.h (+123/-21), all FL: first a pure refactor patch C75+C76+C77 (+79/-19: table-driven long-press mapping, behaviour-identical, prerequisite for value 7); then C74+C78+C79 (+32: key-light availability and the Toggle Frontlight option); then C80 (+12: web index, with C117, C130). The only KEY content is the `&& !BoardConfig::hasFrontButtons()` on line 310 (C78): it must be dropped from a KEY-first series because it only matters once Short Power can toggle the light, and re-added by FL.

### Q4 New persisted state
- No new setting key, no new default, no new SD/NVS file in this scope (grep of added lines for `.crosspoint`, `saveToFile`, `Preferences`, `RTC_*` found nothing new).
- New enum values in existing keys: `shortPwrBtn` 10 = TOGGLE_FRONTLIGHT (CrossPointSettings.h:169, offered only where `keyFrontlightAvailable()`), `longPressMenuFunction` 7 = LP_MENU_FRONTLIGHT_PANEL (:185, Silkscreen without a Home key). Both are appended after upstream's last values (9 and 6), so upstream numbering does not shift; `fromJson` (C71) still maps 0-9 and 0-6 exactly as upstream's `clamp` and rejects 10 and 7 elsewhere. Writer: SETTINGS via the Settings screen (C199-C203) and the web page (`/api/settings`). Migration risk: the comments record that the fork previously saved 7 and 6 for these two actions before upstream's Bookmark and Footnotes took those numbers; `shortPowerButtonValueAccepted()` (SettingsList.h:324-333) makes a Silkscreen file holding 7-9 load as Ignore. Any future upstream append of 10 or 7 collides again and forces a renumber plus a migration in the fork.
- `doubleClickPwrLight` (existing X4 Pro key, default 1) is now visible and live on Silkscreen: by default a Silkscreen short Power press waits out the double-click window (comment at EpubReaderFootnoteSelectActivity.cpp:64-66 says 500 ms) before Sleep/Confirm.
- New response-only JSON field: `/api/settings` `toggleFrontlightIndex` (CrossPointWebServer.cpp:1441), not documented in docs/webserver-endpoints.md.
- RTC-memory records live in the SDK (front-light load record, ladder presence, touch sleep record): not files, cleared on a cold boot.

### Q5 Risk register: behaviour changes visible to other boards
| # | where | change for other boards | evidence |
|---|---|---|---|
| R1 | SettingsActivity.cpp:140-144 (C202) | new Settings > Display > "Frontlight" row on any board with keys and a light | none among release envs (all light boards have touch; DELINK/Murphy are not envs); CHG |
| R2 | FrontlightPanelActivity.cpp:236,527 (C208, C212) | toggle tile and render use `Frontlight.isOn()` instead of the panel's own copy | differs only if the light changes behind the open panel (X4 Pro double-click); GUESS, needs hardware |
| R3 | HalFrontlight.h:31, .cpp:70 (C30, C27) | `isOn()/setOn()` consult `manager.fault()` | false unless the SDK monitor trips (enable-gated lights only) |
| R4 | HalGPIO.cpp:198-204 (C11) | `rawInputActive()` also counts a decoded key on X3/X4 | GUESS identical (bands <3900) |
| R5 | HalPowerManager.cpp:92-171 (C50-C54) | every deep-sleep entry calls `prepareForSleep()`, `releaseSensorBusForSleep()`, `Frontlight.prepareForSleep()` | SDK returns NotApplicable/true/no-op unless hibernateInSleep, i2cBusRecovery, gated light (InputManager.cpp:1799, SharedI2c.h:167) |
| R6 | english.yaml:593,621-625 (C132-C135) | +6 keys, 362 bytes of text in the English table of every board, plus table slots in each language (GUESS ~0.4-0.7 KB) | counted from the diff |
| R7 | Activity.h:53-55 (C136) | one more virtual in every Activity subclass | vtable +4 B per class |
| R8 | SettingsList.h:229-277 (C76) | long-press list mapping rewritten as a table for all boards | list and mapping identical by reading; no test run |
| R9 | BaseTheme.cpp:84-93,125-127,337-347,931-935 (C99-C105) | battery text via snprintf (drops two std::to_string heap strings per draw) and a `noChargeableCell()` call per header draw | text identical; function returns false immediately |
| R10 | HttpDownloader.cpp, CrossPointWebServer.cpp, WebDAVHandler.cpp, CutoffAbortPrint.h (C116-C129) | every download/upload polls the cutoff and streams through a wrapper (one virtual call per 4 KB chunk) | return value always false without a threshold |
| R11 | CrossPointWebServer.cpp:2719,2635 (C120, C121) | `noteClientTraffic()` on every WebSocket frame and claimed job | a `millis()` store; middleware itself is registered only if awakeCeilingMin>0 |
| R12 | ClearCache/ClockSync/OTA/FontDownload `skipLoopDelay()/preventAutoSleep()` (C183-C187, C195) | rewritten as out-of-line functions | return the old constants unless radioOffWhenIdle; sleep timeout, Wi-Fi and loop delay unchanged on other boards |
| R13 | onExit changes in Catalog, ClockSync, KOAuth, OTA (C145, C185, C190, C194) | restart condition reordered | identical while `radioReleased` is false |
| R14 | HalClock.cpp:14-29 (C17) | extra log reads `rejectedAddress()` at boot on RTC boards | getter only |
Sleep timeout, Wi-Fi and strings were checked explicitly: the only sleep-timeout and radio rules added are behind `awakeCeilingMin`, `awakeInputCapMin`, `radioOffWhenIdle`, `releaseWaitMaxMs`, all 0/false in `PowerConfig{}` (BoardConfig.h:1035-1103).

### Q6 Strings
Six keys, English only (english.yaml:593 `STR_BATTERY_TOO_LOW_FOR_UPDATE`; :621 `STR_FRONTLIGHT_NOT_DETECTED`, :622 `_COOL_ONLY`, :623 `_WARM_ONLY`; :624 `STR_POWER_BUTTON_FAULT`; :625 `STR_NO_BATTERY`). They follow the workflow in .agents/rules/generated-files-cache.md and review-i18n-docs (new user-facing text uses `tr()`, English source only, generated I18n headers are gitignored and absent from the diff, `%u` placeholders have precedent). Existing `STR_FRONTLIGHT` and `STR_TOGGLE_FRONTLIGHT` are reused. Problems: `STR_POWER_BUTTON_FAULT` (174 bytes) carries schematic refdes "R36 and R72", a board-only message paid for by every board; the fault popup and the no-light popup need `drawWrappedPopup` (C104). USER_GUIDE.md has +6 lines in the fork (not mine).

### Q7 Hygiene (no exceptions, RTTI, raw Serial, new/malloc, std::function additions or per-frame allocation found in added lines; scan by regex)
1. Dangling references to downstream docs in shared code: HttpDownloader.cpp:37-40, OpdsHttpTransport.h:19, PluginCatalogActivity.cpp:55-56, PluginHubInstallActivity.cpp:142-143, HomeActivity.cpp:33, EpubReaderActivity.cpp:68, ActivityManager.cpp:78 (BAT-01, AW-02, AW-04, docs/silkscreen/POWER.md do not exist upstream).
2. Long comment blocks (rule: usually 1-2 lines): HalDisplay.h:12-18, HalFrontlight.h:34-41, HalGPIO.cpp:331-337, HalPowerManager.h:92-97,100-105,131-136, HalPowerManager.cpp:106-111,377-382, SettingsList.h:195-200, FrontlightPanelActivity.h:74-79, CrossPointWebServer.cpp:205-209, CutoffAbortPrint.h:6-10, HttpDownloader.cpp:75-79.
3. Downstream macros and board names in shared files: `SILKSCREEN_BATLOG` (HalGPIO.cpp:10,226; HalPowerManager.cpp:25,119,288,389,401; HalPowerManager.h:124), `CROSSPOINT_TEST_SUITE` in 10 files, `isSilkscreen()` in app code (HalPowerManager.cpp:376,392,404,409; CrossPointSettings.cpp:70; SettingsList.h:189,327; DictionaryDefinitionActivity.cpp:538; EpubReaderFootnoteSelectActivity.cpp:68) against the "device-specific code behind HAL/SDK" rule.
4. Board refdes in a user string: english.yaml:624.
5. Magic numbers: HalPowerManager.h:138 (+100 mV), HalPowerManager.cpp:257,280,383 (sample counts 16, 4, 16), :293 (98, 5), FrontlightPanelActivity.cpp:290 (`maxLines = 3`), drawWrappedPopup literals 3 and 6 (main.cpp:195,785).
6. A getter that mutates: FontDownloadActivity.cpp:81-94 `preventAutoSleep()` writes `resultPolled_`.
7. Duplicate helper: `watchdogProgress()` copied into HomeActivity.cpp:28-41 and EpubReaderActivity.cpp:68-76; belongs as an inline no-op in RenderWatchdog.h.
8. Dead or redundant code: HalGPIO.cpp:198-204 (see C11).
9. Undocumented API and list-position coupling: CrossPointWebServer.cpp:1437-1442 with SettingsPage.html:405-412 (page hides a row by list index).
10. Capability misuse: `FREEINK_CAP_POWER_POLICY` gates the render watchdog hooks (ActivityManager.cpp:36,77,100; HomeActivity.cpp:28; EpubReaderActivity.cpp:63) though the watchdog is a different concern; and HalPowerManager.cpp:288/401 couple the user-visible percentage to a downstream log macro.

### Q8 GENERIC-FIX candidates (real behaviour on upstream devices, verified by reading code, not run)
- G1 DictionaryDefinitionActivity.cpp:533-540 (C164, test C220). Upstream keeps `content.selection` after a successful lookup (:543), restores it when Back returns from a nested lookup (:420), and only a page turn clears it (:612-635). `startSelection()` hands it to `ClipSelectionActivity`, which restores `rangeStart` from it (ClipSelectionActivity.cpp:96-104) and, in DefinitionLookup mode, treats Back as cancel instead of clearing the range (:659-667). Repro on a keys-only device (X3/X4/X4 Classic), definition that fits one page (no page turn to clear it): select a range and look it up, Back to the definition, Confirm: the selection reopens with the old start fixed (keys only move the end); Back cancels but upstream keeps the range, so the next Confirm reopens it again; only ranges containing that start can be looked up, touch avoids it with a long press. The fork forgets the range on cancel but gates it on `isSilkscreen()` to "match upstream"; ungating is the generic fix. Whether upstream treats the kept range as a feature is GUESS.
- G2 Result screens spin the CPU: upstream `skipLoopDelay()` returns true in ClearCacheActivity.h:16, ClockSyncActivity.h:16, OtaUpdateActivity.h:44, FontDownloadActivity.h:43, and main.cpp:1090-1091 then forces full speed; repro on any upstream device: finish Settings > Clear reading cache or Clock > Sync now and leave it on the result screen. Fork returns true only while working (C183-C187, C195) and only with radioOffWhenIdle. Power saving size is GUESS.
- G3 Font download result keeps the device awake: upstream `preventAutoSleep()` returns true for COMPLETE and ERROR (FontDownloadActivity.h:36-42), so after Manage Fonts finishes the device never auto-sleeps, Wi-Fi stays associated and the loop spins (with G2) until the user leaves. The fork refreshes the idle timer once, then lets the sleep timeout run (C187).
- G4 Wi-Fi left on at a finished flow, against "Network flows release WiFi ... when they end" (ui-activities.md): ClockSync result (C185), OTA no-update/failed result (C192/C194), KOReader auth result (C190), Plugin Hub Done screen (C196; upstream downloadFinished at PluginHubInstallActivity.cpp:135-142 only sets DONE), plugin catalog picker (C160), KOReader sync failure (C181). Upstream turns Wi-Fi off only in `onExit`. Repro: Settings > OTA update, "no update" screen: radio stays up until Back or the sleep timeout.
- G5 EpubReaderActivity.cpp:1517-1530 (C173): cancelling the Wi-Fi picker in the loan-time sync (upstream :1487-1491) calls `finish()` with the station still up and nothing on the way home restarts; the failed-sync popup waits with Wi-Fi up too. Repro: open a book that needs the clock sync, choose Sync, cancel the picker. Severity of an idle STA radio is GUESS.
- G6 FrontlightPanelActivity.cpp:236,527 (C208, C212): panel state follows the live light; on an X4 Pro, toggling by power double-click while the panel is open leaves the panel's copy stale in upstream. Needs hardware to confirm (GUESS).
- Not bugs but free wins: BaseTheme battery text without std::to_string (C100, C105).
Not generic: KeyboardEntry/ClipSelection changes (need touch+keys), Settings Frontlight row, RTC re-sync (C155), USB cable detect (C152).

### PR cut line suggested by the data (dependencies in brackets)
1. SDK PR(s) first (other map). 2. Device PR: C125 plus the DEV hunks the boot needs: touch/ADC/charge glue C02, C05, C07-C10, C13, C15-C17, C19, C24-C34, C38-C39, C46-C48, C50-C54, C134, C152-C155, C168, and main.cpp. 3. KEY (+177/-67) [SDK:BTN]. 4. G2-G5 radio fixes ungated and policy-free, then RAD policy (+234/-43) [SDK:PP]. 5. BAT (+220/-29) [SDK:PP BAT OPDS, PluginHttp, main.cpp]. 6. FL split in three: enum+table refactor+settings (C68-C85, C132-C133), power double-click (C86-C93, C180, main.cpp), panel notices/probe UI (C103-C106, C136-C137, C143, C166, C171-C172, C198-C217). 7. DISP (+128/-17). Keep downstream: BLOG, TEST, X368; WDOG only with its 511-line implementation.

## 4. NOT COVERED
- src/main.cpp (+864): the only caller of begin(true), startTouch, resetTouchForSleep, startDeepSleepMinimal, setPowerClickDeferral, noteInput, awakeCeilingPassed, takeNoLightNotice, drawWrappedPopup; its hunks decide whether several DEV/FL/RAD pieces are dead code upstream. Not read here beyond grep for callers.
- src/util/{RenderWatchdog.h/.cpp, BatteryLogWriter.*, PluginHttp.*, PluginEvents.*}, lib/Logging (holdLastLogs, RTC ring hold under CAP_POWER_POLICY), lib/Serialization/PersistableStore.cpp (test firmware: no writes), lib/TrustedTime (batlog hook): other agents; the PluginHttp and PluginEvents signature changes are prerequisites of C159/C161.
- platformio.ini envs, partitions, scripts/**, docs/**, USER_GUIDE.md (+6), .github, CrossInk, test-suite, 3.68" variant beyond listing X368 hunks.
- freeink-sdk: only the symbols named above were checked at bee6382 and f9cc1be; no full SDK map, no host tests.
- No build, no flash size measurement, no hardware run, no test run. Every "identical on other boards" statement is by reading the hunk and the SDK default; items I could not confirm are marked GUESS.
- Hunk granularity: the 221 IDs group neighbouring hunks (for example C162 is 22 hunks of one concern, C142 is 9 `#if` sites); ranges are exact for the others. In the scripted pass every added and removed line landed in exactly one ID (the sums reproduce the git totals +1773/-235); one blank line, FrontlightPanelActivity.cpp:232, was folded into C207 by hand.
