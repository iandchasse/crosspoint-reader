# Silkscreen departures from upstream

Audit date: **2026-10-21**, after the FROM-upstream sync. This is an audit and extraction recommendation, not a firmware change or PR description.

## Scope and evidence

| Repository | Audited tree | Comparison base |
|---|---|---|
| CrossPoint Reader | `e9f8e0e3a96eb56c2d7fc71a996ca107f432fee8` | `upstream/develop`, `51089beb2299e2e322d318f45e97a349f38ee275` |
| Its pinned FreeInk SDK | `eda3e3f670e92736e61b825537201c72b156c21c` | `fk-upstream/main`, `e67e889117a99f709f37c180af1a4bcc6e72e0ef` |

These are local, fetched upstream snapshots, not a claim about a later remote tip. The SDK comparison uses the SDK revision pinned by this upstream CrossPoint tree. CrossInk's application diff is outside this audit; its shared Silkscreen SDK line is included. The existing uncommitted `UPSTREAM-SYNC.md` correction is excluded from the comparisons and preserved.

The audit covered every changed production file and classified every remaining path. Sources were checked against HANDOFF, the [repository rules](../../AGENTS.md), [REQUIREMENTS.md](REQUIREMENTS.md), [POWER.md](POWER.md), [MODULARITY.md](MODULARITY.md), port history, and the existing [PR plan](UPSTREAM-PRS.md) and [hunk maps](pr-maps/README.md). The older maps describe earlier revisions; the evidence and line references below are from the trees above. The complete path manifest at the end is generated from `git diff --numstat <base> HEAD`; numbers count changed text, including comments, not compiled instructions.

## Conclusion

The reusable firmware work is already substantially separated by capabilities. **Keys beside touch, their layout, and their settings should travel together as generic app support.** They do not require Silkscreen pin numbers, ADC thresholds, battery policy, or frontlight load probing. There is no need for another input framework.

The largest remaining opportunities are to reduce policy and diagnostic hooks in shared files, remove one unnecessary change to other boards, and simplify repeated settings construction. Removing electrical branches indiscriminately would make the port incomplete. The stable dual-controller probe, touch sleep, SD parking, external power-key bias, and frontlight rail waits are required mechanisms.

| CrossPoint category | Changed files | Added | Removed |
|---|---:|---:|---:|
| Production source and assets | 89 | 3,385 | 317 |
| Build/repository configuration | 4 | 359 | 2 |
| SDK gitlink | 1 | 1 | 1 |
| Hardware test firmware | 72 | 15,766 | 0 |
| Host tests | 4 | 94 | 0 |
| Tooling | 31 | 8,473 | 0 |
| Documentation | 16 | 4,667 | 1 |
| **Total** | **217** | **32,745** | **321** |

| SDK category | Changed files | Added | Removed |
|---|---:|---:|---:|
| Production source | 43 | 7,042 | 146 |
| Host tests/stubs | 57 | 3,634 | 29 |
| Documentation | 3 | 396 | 9 |
| **Total** | **103** | **11,072** | **184** |

About 89% of added CrossPoint lines are documentation, tooling, and tests. The production-source comparison across the app and SDK is **132 changed paths, +10,427/-463**. Whole-file counts must not be confused with feature sizes: `main.cpp`, `BoardConfig.h`, frontlight classes, settings, and themes each mix several concerns.

## Reusable app behavior, separate from hardware

| Concern | Current source evidence | Extraction boundary |
|---|---|---|
| Keys coexist with touch | SDK `BoardConfig.h:861,3329`; `HalGPIO.cpp:213`; `MappedInputManager.cpp:71-77,188-190` | Keep `buttonsAlongsideTouch` and the existing front-key capability. No Silkscreen identity needed in activities. The SDK profile predicate and HAL runtime predicate have different purposes: optional touch can be absent at runtime. |
| Hint strip and selected-item cursor | `UITheme.cpp:75-87`; `BaseTheme.cpp:216-255`; `LyraTheme.cpp:111-159`; `RoundedRaffTheme.cpp:201-205`; `CoverGridHomeUi.cpp:106-117,232` | Reserve and draw key controls when front keys exist. This includes layout metrics, not just accepting button events. |
| Keyboard sizing and hit testing | `KeyboardEntryActivity.cpp:537-544,1005-1018` | Keep the keyboard above the hint band and exclude that band from touch overflow. Retain both touch and selected-key feedback. |
| Slider and list help | `UiSliderDialog.h:48-59,127`; bookmarks `:262`; clipping list `:190` | Key help depends on keys. Finger target size still depends on touch. |
| Key settings on touch screens | `SettingsList.cpp:57,174,181-196`; `SettingsList.h:413`; `SettingsActivity.cpp:123-127`; `EpubReaderActivity.cpp:1297` | Remapping, orientation, and long-Confirm remain available with a full key set. Include the long-press filter integration from the latest upstream sync. |
| Selection by keys and touch | `ClipSelectionActivity.cpp:257-334,445-523,603-608,651-758`; EPUB reader `:351,2595,2715` | Keep cursor geometry, repeat handling, touch/key ownership, and Look Up / Clip / Bookmark action selection together. Coordinate conversion is part of correctness; the deferred popup-result application prevents navigation from inside its callback. |
| Frontlight reachable by keys | `SettingsActivity.cpp:136-138,423-430`; `FrontlightPanelActivity.cpp:224-231,364-368`; `HalFrontlight.cpp:17` | The settings entry and warmth keys are generic access to an existing control. They belong with key parity even if probe notices and optional light shortcuts are extracted separately. |
| Live state in the light panel | `FrontlightPanelActivity.cpp:234-240,525-530` | Toggling from the actual light state and following external toggles are generic consistency fixes. Pending-probe and missing-string handling are a separate contract. |
| Dictionary cancellation | `DictionaryDefinitionActivity.cpp:552-558` | A saved range pins a start that keys cannot move. This is a generic UX issue currently restricted with `isSilkscreen()`. Discuss/extract independently; do not silently change other boards during a port cleanup. |
| Terminal work releases CPU/radio | `ClearCacheActivity.cpp:87`; `ClockSyncActivity.cpp:36-45,100`; `FontDownloadActivity.cpp:81-100`; `KOReaderAuthActivity.cpp:74,116`; `KOReaderSyncActivity.cpp:333-336,724-731`; `OtaUpdateActivity.cpp:15-24`; `PluginHubInstallActivity.cpp:142-144`; `CatalogActivity.cpp`; `PluginCatalogActivity.cpp` | Generic lifecycle corrections, currently selected by `radioOffWhenIdle`. Preserve existing flow ownership, subsequent downloads/uploads, Retry, modem-sleep, and heap-cleanup restart behavior. |
| Cancellation primitives | SDK OPDS's four changed files; app `PluginHttp.h:87-98`, `.cpp:228-275` | Optional abort propagation is generic transport machinery. The battery/awake-time predicate passed to it is consumer policy. Reuse existing callbacks rather than invent a parallel cancellation system. |
| SD volume-cache sync | SDK `SDCardManager.h:128-152`, `SDCardManager.cpp` | Generic correctness fix before unmount/rail removal/MSC handoff. Keep the existing precondition that file users have stopped. Root sync flushes volume caches; it does not commit an open writer's private size/first-cluster metadata. |
| Popup/text helpers | `BaseTheme.cpp:760-824,930-937` | Wrapped drawing and fixed-buffer percentage formatting are reusable code, but the new popup's current production callers are hardware/policy notices. Keep no-cell policy separate. Small-string optimization means an allocation saving from replacing short `std::string` values has not been established. |

**Keep legitimate touch branches.** `UiSliderDialog.h:56` uses touch to size controls; `EpubReaderActivity.cpp:2710` uses it for dense rows; gesture/hit routing must still test touch. Replacing every `hasTouch()` with `hasFrontButtons()` would be wrong. The useful separation is touch availability versus physical-key availability, not one global input mode.

## Hardware mechanisms and consumer policy

The hardware-facing implementation is reusable in mechanism, but its parameters and required sequencing belong to the device profile. This is different from an app feature becoming Silkscreen-specific merely because the first user is Silkscreen.

| Mechanism | Hardware reason and current boundary |
|---|---|
| Board/profile/build registration | `BoardConfig.h`, app envs, board tag and SDK pin. ESP32-S3 N16R8, SDMMC, PSRAM/TinyUSB recipe and physical pins are device facts. The 3.68-inch profile/tag prevents flashing a 4.26-inch image onto different glass. |
| ADC keys and shared ADC lock | `AdcLadderDecode.h`, `SharedAdc.h`, `InputManager`, battery and LED-monitor consumers. Thresholds, release rule, missing-pull-up checks and concurrent ADC use are electrical requirements. Preserve the owner's shared-lock decision. |
| Analog charge-status decoder | `ChargeLadderDecoder.h`, `BatteryMonitor`, HAL polling and USB Drive detach fallback. The board has no ordinary VBUS sense. Rate limiting, debounce and unknown states are not arbitrary UI branches. |
| External-bias power key and parking | `InputManager`, `PowerManager`, profile park data. No internal pull where the board biases the pin; bounded held-key release and invalid-idle handling prevent drain/wake storms. |
| Parked SDMMC | `SDCardManager`, `SdmmcBlockDevice`, `SdmmcParkedMount`. Preserve cache flush, unmount, bus/host release, rail gating and pad order. The SPI and SDMMC shutdown bodies legitimately differ. |
| Shared I2C and RTC | `SharedI2c.h`, RTC, IMU/environment callers and `HalClock`. Share clock/recovery rules, validate DS3231 identity before writes, support the configured fallback RTC, and preserve invalid-clock handling and sleep parking. The 0x51 fallback has no identity check, as already documented in MODULARITY's J6 notes. |
| Optional touch | `InputManager`, profile identity/fallback data, HAL deferred start and sleep calls. Default FT6336U tail uses the existing FT5x06 backend; optional GT911 alternate-jumper support must also sleep an attached controller. Keep the latest upstream per-backend compile gates. |
| 4.26-inch display controller | `XteinkDetect`, `EpdBus`, profile and app panel-family glue. Keep cold-boot SSD1677/UC8279D detection, deep-sleep-only cache and validation, BUSY polarity, overrides, and correct driver-free sleep commands. |
| 3.68-inch display variant | `FreeInkDisplay`, UC8279 driver, profile assertions and `HalDisplay::isX3Panel()`. Fixed UC8279d, no probe/touch/other driver. Panel behavior must not be inferred from X3 board identity; the S3 variant uses X3 glass without becoming an X3 board. |
| Gated frontlight | `FrontlightManager`, `FrontlightLoad.h`, HAL and probe-aware UI. Enable timing, fault/off state, charged-rail wait, missing-string fallback and sleep/restart parking go together. Warm/cool blend and shortcut choices are separable behavior. |

UC8279D support is **already upstream and stable in the documented integration**. The production X4 driver diff is only diagnostics (+42 lines across its two files). The fork needs the Silkscreen detector/cache/profile integration, not a replacement driver. See [MODULARITY's completed detector](MODULARITY.md#module-table), `REQUIREMENTS.md:68`, and the SDK detector host coverage. The 3.68-inch module's historical `VER=00 0F 00` is a different panel/build case and is not evidence against 4.26-inch probing.

Consumer policy and diagnostics should be separately owned:

- **Required no-hardware-off behavior:** cutoff checks, early re-sleep, held-key handling, panel/touch sleep after reset, and pin/rail parking. `REQUIREMENTS.md:141-142` requires early clean sleep; this cannot be deferred merely because the old PR plan labels the app cutoff “wave 2.” Threshold/configuration policy can be discussed independently of providing safe behavior.
- **Awake limits and network policy:** ceiling, input cap, ghost-wake/no-input timeout, storage-error timeout, background stop predicate and result-screen radio behavior. The app consumes these profile values; they are not key/UI requirements.
- **Light shortcut policy:** Toggle Frontlight, long-Confirm panel and Silkscreen's full short-click deferral. Keep their settings and input ownership together. The footnote guard (`EpubReaderFootnoteSelectActivity.cpp:64-75`) prevents one physical release jumping and later firing a global deferred action; it is not a redundant branch.
- **Battery presentation:** averaging/interpolation, 5% display steps versus batlog's 1%, no-chargeable-cell state and text. Keep raw cutoff readings independent from display rounding (`HalPowerManager.cpp:288-409`).
- **Render watchdog:** `RenderWatchdog.{h,cpp}`, retained logs, activity begin/end/progress, boot recovery and sleep-entry hooks. Useful robustness work with its own policy and task/RTC-memory cost; it is not minimal key support or panel support.
- **Battery log:** `BatteryLog*`, `BatteryLogWriter`, clock/CPU/input/radio/light/transfer hooks. It is explicitly a diagnostic image, not reader feature debt that must all go upstream.
- **Bench firmware:** 72 app files, diagnostic SDK APIs, test routing/friends, `OptionPopup` test headline count and env filters. Keep outside production device/UI proposals.

## Concrete simplifications, in recommended order

### 1. Remove the unnecessary non-Silkscreen raw-contact departure

`HalGPIO.cpp:191-206` first takes the required `hasAdcLadders() -> adcLadderContact()` path. Silkscreen returns there. The subsequent fallback additionally changed upstream's raw threshold test into `decoded button OR raw threshold`; this affects other boards and is not needed by the port. Restore that fallback to upstream when implementing cleanup, unless separately justified as a generic correction. Keep the Silkscreen early return and shared ADC locking. The old map already flags this as C11; it remains present in current code.

**Benefit:** directly removes a shared-file behavioral departure and an extra helper/condition without changing the Silkscreen path. This is the clearest small cleanup.

### 2. Use one bounded settings descriptor representation

`SettingsList.h:206-278` stores long-press values then separately switches over them to make labels. `:294-364` repeatedly constructs a temporary short-power option vector for labels, get/set and the web toggle index. Its initializer-list construction is followed by `reserve()`, so the existing initial elements are allocated before the larger capacity is reserved.

Use bounded `{persistedValue, StrId}` storage, similar to the existing bounded long-press list, and reuse it for labels and lookup. Generate a `std::vector` only where `SettingInfo` actually needs ownership. Avoid a general callback framework.

Preserve upstream menu order and saved values: **Toggle Frontlight = 10, long-Confirm Frontlight = 7**. Preserve loading hidden upstream options, the Silkscreen old 7–9 ambiguity fallback at `:327-330`, and web hiding based on the actual toggle position at `:357-364`. The migration rule is not dead code.

**Benefit:** fewer mapping branches, less temporary heap work, one source of label/value truth. Binary savings need a before/after image.

### 3. Give the watchdog one capability-aware facade

Home `:28-41` and EPUB `:63-76` duplicate guarded includes, no-op `watchdogProgress()` and policy comments. Put the off-capability no-op in `RenderWatchdog.h` and leave explicit progress calls at callers. The same approach can reduce begin/end call-site guards in `ActivityManager.cpp:77,100-107`.

Separate watchdog selection from the broad `FREEINK_CAP_POWER_POLICY` when that subsystem is next changed: enabling a battery policy should not silently opt a new device into this watchdog. Keep progress ownership, rendering/sleep bounds, task retry, and RTC recovery unchanged.

**Benefit:** fewer fork hunks in busy upstream UI files. Inlining already folds these wrappers, so a flash reduction is not demonstrated.

### 4. Make off-policy state and hooks genuinely absent

`HalPowerManager.h:23-31` adds no-cell cache, input/client clocks and cutoff latch to all class layouts. `main.cpp:1517-1523` maintains the input clock; `CrossPointWebServer.cpp:2719` maintains client traffic. `BoardConfig.h:3543-3546` folds policy values off on other builds, but that does not by itself remove unconditional fields or timestamp writes.

Keep the state behind one compile-time policy boundary, with inline no-op hooks off-capability, or move it into a small power-session module. Keep cutoff/awake predicates at transport wait points: moving the main-loop check cannot replace checks inside blocking work.

**Benefit:** explicit off-device ownership and potential RAM/instruction reduction. Measure resulting class layout and linked code; do not assume the compiler removes everything.

### 5. Reduce the policy footprint in `main.cpp`

The current `main.cpp` delta is **+814/-50**. Move cohesive downstream boot/recovery/awake/light-notice routines into ordinary functions in a dedicated module, leaving ordered setup/loop/sleep hooks. Avoid dynamic dispatch or per-feature activity branches. Keep the order of early battery/wake decisions, controller resolution, peripheral initialization, storage drain, light parking and sleep.

There is also an immediate duplicate: `sleepIfResetLoop()` at `:829-860` contains an off-capability copy of the driver-free sleep sequence, although the constexpr off-policy limit is zero and the function returns before it. A single compile-time implementation/no-op boundary removes the duplicate text. Keep the shared `sleepWithoutDrivers()` sequence used by reset-loop and watchdog recovery.

**Benefit:** much smaller conflict surface in upstream's busiest file. Moving code alone does not shrink the image or the total fork delta.

### 6. Upstream generic end-of-work behavior as generic corrections

Once accepted as generic behavior, the F1 lifecycle changes can stop branching on `radioOffWhenIdle()` at every result screen. Retain the existing distinctions: Font Download supports more downloads; Clock Sync releases only its own connection; KOReader retains/re-enables radio for upload; Plugin Hub ERROR supports Retry; catalog-to-picker ends its old connection. `radioReleased` records whether this flow used the radio even after it is off and is still needed for cleanup/restart decisions.

**Benefit:** eliminates product-policy gates through convergence with upstream. This is a deliberate behavior proposal for other boards, not a behavior-preserving local deletion.

### 7. Share the power-key electrical rule and simplify the SD declaration

`InputManager.cpp:70-75` and SDK `PowerManager.cpp:28-33` duplicate `powerPinMode()`: external bias means `INPUT`, otherwise pull opposite the active level. Give that hardware rule one owner. Do not merge superficially similar GPIO drive helpers whose hold-release ordering differs.

`SDCardManager.h:128-143` declares the identical `void shutdown()` in both preprocessor arms. Use one declaration and explain both backends in its contract; retain separate backend implementations.

**Benefit:** fewer redundant branches and fewer opportunities for hardware-rule drift; likely primarily a source improvement.

### 8. Keep test instrumentation out of the production API surface

`SharedI2c.h:125-163` exposes `ScanResult` / `scanSensorBus()` used only by bench I2C/RTC/touch tests. `InputManager.h:375-400` exposes RAM-only touch mapping used only by `TouchMappingTest`. `OptionPopup.h:74-80,298-303` has a test-only headline-count setter. Prefer test helpers or the existing diagnostics boundary when touching these areas; do not expand the production SDK solely for bench UI.

`EpdBus.cpp:538-638` repeats diagnostics-only `closeRefresh()` at six exits. One small diagnostics-only scope guard can close consistently while preserving each branch's `recordWait()` result. Do not collapse the real BUSY polarity, slice-hook and ISR/polling decisions.

**Benefit:** less shared-driver conflict surface and API coupling. Most instrumentation is already gated or unreferenced in release images; removing source is not an equivalent flash saving.

### 9. Smaller follow-up experiments

- Let the compiler decide whether to inline `BaseTheme.cpp:760`'s `drawPopupFrame()` instead of forcing `always_inline` into both popup callers. The helper is forced inline at both call sites; the linked size effect still needs measurement.
- Consider a shared SDMMC host/slot setup helper for parked versus ordinary mount, keeping the distinct pin/rail sequence and failure cleanup. Do not unify the storage backends into a runtime abstraction just to remove `#if`s.
- Of the 14 added `PowerConfig` fields, nine have only app consumers: early wake decision, abnormal-reset limit, update minimum, awake ceiling, radio-idle policy, no-input wake timeout, storage-error timeout, input cap and no-cell notice. A consumer-policy block outside the mutable shared `BoardProfile` would improve ownership. This is optional architecture work: both CrossPoint and CrossInk must retain the same behavior, and moving fields alone is not a code-size optimization.
- The 317 added ini lines repeat S3 target details and the two test-source exclusion lists. Shared local ini sections can reduce source maintenance, but preserve `base`/`dio_opi`/TinyUSB flags, release version suffixes, independent test tags and no-touch defaults. This has no firmware-size benefit by itself and is lower priority than production-source cleanup.
- Reuse optional abort predicates consistently in existing network wrappers. `ClippingSync.cpp:22-56` currently checks cutoff before each request, not inside `sendRequest`; other wrappers poll it in flight. A common seam could reduce repetition and improve coverage. This audit does not establish an unbounded-drain bug: request timeout behavior still applies.

## What should not be removed for a smaller first port

1. **Stable probing:** retain dual-controller 4.26-inch support. Much of the UC8279 implementation is already upstream, so restricting S1 to SSD1677 would discard supported hardware without removing the original upstream driver.
2. **Frontlight rail safety:** `FrontlightManager.cpp:771-799` places charged-rail waits inside `FREEINK_CAP_FRONTLIGHT_PROBE`. Stripping “probe” wholesale also strips these waits. An unloaded enable has hardware OVP protection; switching a different string onto a charged rail is a separate hazard. Retain the required contract from `REQUIREMENTS.md:185,187-189`, even if optional blend/notice/shortcut work is split.
3. **No-hardware-off safety:** do not ship a hardware port that assumes real power-off exists. Split app policy from SDK mechanics without dropping cutoff, clean sleep, crash-path peripheral sleep or pin parking.
4. **Keys+touch layout:** dropping only hint/cursor/spacing changes leaves key input technically accepted but poorly or incorrectly presented. Keep that vertical slice together.
5. **Input ownership:** do not collapse press/release/long-press guards, selection coordinate transforms or the footnote deferral guard merely to reduce condition count.
6. **Compile-time driver pruning:** fewer textual `#if`s are not always smaller firmware. The no-touch 3.68-inch build intentionally excludes touch, probing and other panel drivers; preserve those proven boundaries.

## Implication for the upstream PR plan (audit-time recommendation)

The following recommendation records the initial audit. The owner subsequently chose **generic firmware features first, then SDK hardware support, then CrossPoint device integration**. The current order, including an independent SDK key capability before app key support and explicit decisions on all software features, is maintained in [UPSTREAM-PRS.md](UPSTREAM-PRS.md). Its updated extraction boundaries supersede the older plan described here.

Keep its independent generic-fix and generic-key work, but revise the proposed minimal hardware cut before using it as an extraction recipe. S1/D1 currently propose SSD1677-only support, removal of light monitoring/probing, and defer app cutoff; those choices do not represent all supported hardware and standing requirements. The “ADC lock S3 only” suggestion also conflicts with the recorded decision to keep the shared lock.

A sensible dependency order is: generic SD fix and terminal-work fixes independently; SDK board mechanisms with the required safety and stable probe integration; minimal app env/tag/HAL integration and required no-hardware-off behavior; generic keys+touch including key access to the light. Keep optional shortcuts, presentation choices, watchdog, battery log and bench tooling separate. Generic cancellation APIs can be proposed independently of battery policy. Hardware/SDK maintainers still own acceptance of their respective interfaces.

This audit supplies facts and extraction boundaries. It does not draft PR prose, create branches, change firmware, or publish anything.

## Size evidence and limits

No new builds or tests were run for this read-only audit. Existing `silkscreen` ELF/map files were inspected with Xtensa `nm -S -C`; its `firmware.bin` SHA-256 is `6af0e7c668ebb48fe942253309d4cbfa5fba1ff2be86c6d4444f566614bb9b8c`, matching the frozen image manifest from the preceding sync.

Selected retained symbols: `shortPowerButtonOptions()` 235 bytes, `buildLongPressMenuValues()` 268 bytes, and writable `BoardConfig::ACTIVE` 428 bytes. These are current symbol sizes, **not estimated savings or whole-feature sizes**: literal pools, inline code, shared code, padding, task stacks and other state are separate. No new matched upstream/C3 baseline was built, so there is no claim of measured off-board flash/RAM overhead or a measured optimization win.

For subsequent implementation, source simplifications need normal behavior checks; footprint claims need a matched before/after build and map. Preserve upstream defaults, persisted values, input transitions and the documented sleep/rail contracts. Existing historical C3 and hardware results remain evidence for their recorded revisions, not measurements of a future extracted topic.

## Cleanup implementation (2026-10-21)

The follow-up working-tree cleanup implements bounded label/value descriptors for both button menus, one shared capability-aware progress facade, removal of the unreachable off-policy reset-sleep duplicate, and a single SDK shutdown declaration. Settings persistence, menu order, web indices and hardware sequences are preserved. See [UPSTREAM-PRS.md](UPSTREAM-PRS.md) for the corrected extraction boundaries.

The raw-contact fallback is deliberately retained: restoring upstream there would change current other-board behavior. Larger policy extraction/state gating, SDK test API relocation, generic radio changes, driver instrumentation restructuring and optional inlining experiments remain separate work. The inventory and source line references above remain the pre-cleanup audit snapshot.

The new compatibility check covers both light capability settings and 32 board/touch/Home/key/PWM combinations, all stored bytes, ordered labels, invalid display indices and the web toggle index. The same fixtures pass against the pre-cleanup menu implementation. The watchdog checks retain the hook-order and lock-free guards and their negative controls, and compile the progress facade both enabled and disabled.

For the owner's next device check, confirm that short-power and long-Confirm options keep their labels/order and survive save/restart (Toggle Frontlight 10, long-Confirm Frontlight 7). Check menus with keys and touch in each orientation, open a book and return Home to exercise progress sites, then sleep/wake and toggle the light normally. Unexpected menu actions, changed light state or failed wake are failure signs. This is a behavior-preserving refactor; no new layout cache format or hardware sequence is introduced. Device checks remain required before a PR.

## Complete changed-path manifest

The following manifest accounts for every path in the two comparisons. A mixed label means the file must be split by concern; it is not a recommendation to upstream the whole file. Production behavior is analyzed above; test/tool/document paths are classified so their size is not mistaken for reader firmware.

### CrossPoint production paths

| Path | + | - | Concern |
|---|---:|---:|---|
| `lib/I18n/translations/english.yaml` | 6 | 0 | Policy/hardware notice strings |
| `lib/Logging/Logging.cpp` | 16 | 0 | Retained-log/watchdog primitive |
| `lib/Logging/Logging.h` | 4 | 0 | Retained-log/watchdog primitive |
| `lib/Serialization/PersistableStore.cpp` | 7 | 0 | Test-image storage isolation |
| `lib/TrustedTime/TrustedTime.cpp` | 10 | 1 | Battery-log hook |
| `lib/hal/HalClock.cpp` | 17 | 0 | RTC validity/identity; diagnostics |
| `lib/hal/HalClock.h` | 8 | 0 | RTC validity/identity; test API |
| `lib/hal/HalDisplay.cpp` | 5 | 5 | Panel-family behavior |
| `lib/hal/HalDisplay.h` | 19 | 0 | Panel-family behavior; test API |
| `lib/hal/HalFrontlight.cpp` | 78 | 1 | Light hardware/probe adapter; live UI state |
| `lib/hal/HalFrontlight.h` | 83 | 1 | Light hardware/probe adapter; live UI state |
| `lib/hal/HalGPIO.cpp` | 123 | 4 | Keys capability; hardware startup/sleep; diagnostics |
| `lib/hal/HalGPIO.h` | 23 | 3 | Keys capability; hardware startup/sleep; diagnostics |
| `lib/hal/HalPowerManager.cpp` | 188 | 4 | Hardware sleep; cutoff/awake/battery/radio policy |
| `lib/hal/HalPowerManager.h` | 81 | 0 | Hardware sleep; cutoff/awake/battery/radio policy |
| `lib/hal/HalStorage.h` | 3 | 0 | Test access only |
| `src/CrossPointSettings.cpp` | 28 | 7 | Key settings; light shortcuts/migration; test isolation |
| `src/CrossPointSettings.h` | 16 | 4 | Key settings; light shortcuts/migration; test isolation |
| `src/MappedInputManager.cpp` | 14 | 6 | Keys beside touch; light-click deferral |
| `src/MappedInputManager.h` | 15 | 2 | Keys beside touch; light-click deferral |
| `src/SettingsList.cpp` | 12 | 5 | Key settings; light shortcut menu mapping |
| `src/SettingsList.h` | 126 | 24 | Key settings; light shortcut menu mapping |
| `src/activities/Activity.h` | 3 | 0 | Frontlight-notice ownership API |
| `src/activities/ActivityManager.cpp` | 37 | 1 | Watchdog; frontlight notices; test routing |
| `src/activities/ActivityManager.h` | 1 | 0 | Watchdog; frontlight notices; test routing |
| `src/activities/CatalogActivity.cpp` | 14 | 3 | Radio lifecycle; cutoff cancellation |
| `src/activities/CatalogActivity.h` | 3 | 0 | Radio lifecycle; cutoff cancellation |
| `src/activities/boot_sleep/SleepActivity.cpp` | 1 | 1 | Panel-family behavior |
| `src/activities/home/HomeActivity.cpp` | 15 | 0 | Watchdog progress |
| `src/activities/library/LibraryListActivity.cpp` | 1 | 1 | Key layout |
| `src/activities/network/CrossPointWebServerActivity.cpp` | 4 | 1 | Cutoff upload cancellation |
| `src/activities/network/UsbDriveActivity.cpp` | 27 | 0 | Analog supply/cable-loss handling |
| `src/activities/network/UsbDriveActivity.h` | 6 | 0 | Analog supply/cable-loss handling |
| `src/activities/network/WifiSelectionActivity.cpp` | 13 | 5 | Key hint; RTC resync; test exclusions |
| `src/activities/plugins/PluginCatalogActivity.cpp` | 20 | 7 | Radio lifecycle; cutoff cancellation |
| `src/activities/reader/ClipSelectionActivity.cpp` | 72 | 16 | Key/touch selection, cursor geometry and action ownership |
| `src/activities/reader/ClipSelectionActivity.h` | 7 | 0 | Key/touch selection, cursor geometry and action ownership |
| `src/activities/reader/DictionaryDefinitionActivity.cpp` | 9 | 1 | Key selection; board-gated generic cancellation fix |
| `src/activities/reader/EpubReaderActivity.cpp` | 52 | 12 | Keys; panel family; light shortcut; radio; watchdog |
| `src/activities/reader/EpubReaderActivity.h` | 3 | 2 | Keys; panel family; light shortcut; radio; watchdog |
| `src/activities/reader/EpubReaderBookmarksActivity.cpp` | 2 | 2 | Key help |
| `src/activities/reader/EpubReaderClippingListActivity.cpp` | 2 | 1 | Key help |
| `src/activities/reader/EpubReaderFootnoteSelectActivity.cpp` | 10 | 1 | Deferred light-click action ownership |
| `src/activities/reader/KOReaderSyncActivity.cpp` | 14 | 0 | Radio result/upload lifecycle |
| `src/activities/reader/KOReaderSyncActivity.h` | 2 | 0 | Radio result/upload lifecycle |
| `src/activities/settings/ClearCacheActivity.cpp` | 3 | 0 | Terminal-screen idle policy |
| `src/activities/settings/ClearCacheActivity.h` | 3 | 1 | Terminal-screen idle policy |
| `src/activities/settings/ClockSyncActivity.cpp` | 11 | 3 | Owned-radio release; terminal idle |
| `src/activities/settings/ClockSyncActivity.h` | 6 | 1 | Owned-radio release; terminal idle |
| `src/activities/settings/FontDownloadActivity.cpp` | 27 | 0 | Radio/idle lifecycle; cutoff callback |
| `src/activities/settings/FontDownloadActivity.h` | 4 | 8 | Radio/idle lifecycle; cutoff callback |
| `src/activities/settings/KOReaderAuthActivity.cpp` | 8 | 3 | Radio/idle lifecycle |
| `src/activities/settings/KOReaderAuthActivity.h` | 3 | 0 | Radio/idle lifecycle |
| `src/activities/settings/OtaUpdateActivity.cpp` | 30 | 21 | Radio/idle lifecycle; battery write gate |
| `src/activities/settings/OtaUpdateActivity.h` | 12 | 1 | Radio/idle lifecycle; battery write gate |
| `src/activities/settings/PluginHubInstallActivity.cpp` | 4 | 0 | Radio lifecycle |
| `src/activities/settings/SdFirmwareUpdateActivity.cpp` | 15 | 0 | Battery write gates |
| `src/activities/settings/SettingsActivity.cpp` | 23 | 1 | Key remap/light access; optional light shortcuts |
| `src/activities/settings/SettingsActivity.h` | 1 | 0 | Key remap/light access; optional light shortcuts |
| `src/activities/util/FrontlightPanelActivity.cpp` | 115 | 4 | Key warmth; live light state; hardware probe UI |
| `src/activities/util/FrontlightPanelActivity.h` | 44 | 1 | Key warmth; live light state; hardware probe UI |
| `src/activities/util/KeyboardEntryActivity.cpp` | 8 | 4 | Key cursor; hint-band layout/hit testing |
| `src/clippings/ClippingSync.cpp` | 7 | 0 | Per-request cutoff gate |
| `src/components/CoverGridHomeUi.cpp` | 9 | 9 | Key hints/cursor/spacing |
| `src/components/OptionPopup.h` | 12 | 1 | Bench-only headline limit |
| `src/components/UITheme.cpp` | 6 | 6 | Key hint metrics |
| `src/components/UITheme.h` | 1 | 1 | Key hint metric API |
| `src/components/UiSliderDialog.h` | 4 | 2 | Key help alongside touch targets |
| `src/components/themes/BaseTheme.cpp` | 76 | 25 | Key hints; battery/no-cell UI; popup helpers |
| `src/components/themes/BaseTheme.h` | 8 | 0 | Popup helper API |
| `src/components/themes/lyra/LyraTheme.cpp` | 3 | 2 | Key hints; no-cell UI |
| `src/components/themes/roundedraff/RoundedRaffTheme.cpp` | 1 | 1 | Key hints |
| `src/main.cpp` | 814 | 50 | Mixed hardware sequencing, policy, shortcuts, logging and test hooks |
| `src/network/CrossPointWebServer.cpp` | 53 | 3 | Cutoff/awake policy; settings UI; panel tag |
| `src/network/CutoffAbortPrint.h` | 26 | 0 | Stream cancellation bound to cutoff |
| `src/network/FirmwareBoardTag.cpp` | 12 | 0 | Board/panel/test update identity |
| `src/network/HttpDownloader.cpp` | 44 | 19 | In-flight cutoff cancellation |
| `src/network/OpdsHttpTransport.cpp` | 5 | 0 | OPDS cancellation adapter |
| `src/network/OpdsHttpTransport.h` | 3 | 0 | OPDS cancellation adapter API |
| `src/network/WebDAVHandler.cpp` | 16 | 1 | Stream/upload cutoff cancellation |
| `src/network/html/SettingsPage.html` | 8 | 0 | Light shortcut/double-click UI |
| `src/util/BatteryLogWriter.cpp` | 161 | 0 | Battery-log image only |
| `src/util/BatteryLogWriter.h` | 35 | 0 | Battery-log image only |
| `src/util/PluginEvents.cpp` | 24 | 7 | Optional cancellation; battery/awake policy |
| `src/util/PluginEvents.h` | 3 | 1 | Optional cancellation; battery/awake policy |
| `src/util/PluginHttp.cpp` | 19 | 16 | Generic optional transport cancellation |
| `src/util/PluginHttp.h` | 7 | 4 | Generic optional transport cancellation |
| `src/util/RenderWatchdog.cpp` | 343 | 0 | Watchdog policy/recovery; test instrumentation |
| `src/util/RenderWatchdog.h` | 168 | 0 | Watchdog policy/recovery; test instrumentation |

### SDK production paths (relative to freeink-sdk)

| Path | + | - | Concern |
|---|---:|---:|---|
| `libs/display/FreeInkDisplay/include/FreeInkDisplay.h` | 15 | 0 | Panel-family/driver selection; diagnostics |
| `libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp` | 23 | 1 | Panel-family/driver selection; diagnostics |
| `libs/display/FreeInkDisplay/src/bus/EpdBus.cpp` | 176 | 5 | Electrical bus behavior; display policy; diagnostics |
| `libs/display/FreeInkDisplay/src/bus/EpdBus.h` | 50 | 0 | Electrical bus behavior; display policy; diagnostics |
| `libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.cpp` | 52 | 3 | Board tuning; diagnostics |
| `libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.h` | 8 | 0 | Board tuning; diagnostics |
| `libs/display/FreeInkDisplay/src/driver/Uc8279Driver.cpp` | 15 | 0 | Diagnostics only; production driver upstream |
| `libs/display/FreeInkDisplay/src/driver/Uc8279Driver.h` | 3 | 0 | Diagnostics only; production driver upstream |
| `libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp` | 35 | 0 | Diagnostics only; production driver upstream |
| `libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.h` | 7 | 0 | Diagnostics only; production driver upstream |
| `libs/hardware/BatteryMonitor/include/BatteryMonitor.h` | 75 | 2 | Analog supply/ADC; cutoff; percentage mechanism |
| `libs/hardware/BatteryMonitor/include/ChargeLadderDecoder.h` | 107 | 0 | Pure analog charge-state decoder |
| `libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp` | 292 | 4 | Analog supply/ADC; cutoff; percentage mechanism |
| `libs/hardware/BoardConfig/include/BoardConfig.h` | 1190 | 20 | Board data/capabilities; electrical helpers; policy/bench API |
| `libs/hardware/BoardConfig/include/ProbeChecks.h` | 83 | 0 | Pure probe/identity checks |
| `libs/hardware/BoardConfig/include/SharedAdc.h` | 60 | 0 | Shared ADC serialization |
| `libs/hardware/BoardConfig/include/SharedI2c.h` | 177 | 0 | Shared bus startup/recovery/parking; bench scan API |
| `libs/hardware/EnvironmentSensor/src/EnvironmentSensor.cpp` | 2 | 1 | Shared-I2C startup |
| `libs/hardware/FrontlightManager/include/FrontlightLoad.h` | 149 | 0 | Pure frontlight load-state decisions |
| `libs/hardware/FrontlightManager/include/FrontlightManager.h` | 263 | 1 | Gated light/load/rail safety; blend; diagnostics |
| `libs/hardware/FrontlightManager/src/FrontlightManager.cpp` | 821 | 3 | Gated light/load/rail safety; blend; diagnostics |
| `libs/hardware/Imu/src/Imu.cpp` | 2 | 1 | Shared-I2C startup |
| `libs/hardware/InputManager/include/AdcLadderDecode.h` | 55 | 0 | Pure ADC-key decoder |
| `libs/hardware/InputManager/include/InputManager.h` | 201 | 2 | ADC keys; power bias; touch discovery/sleep; diagnostics |
| `libs/hardware/InputManager/src/InputManager.cpp` | 660 | 28 | ADC keys; power bias; touch discovery/sleep; diagnostics |
| `libs/hardware/PowerManager/include/BatteryLog.h` | 137 | 0 | Battery-log image only |
| `libs/hardware/PowerManager/include/BatteryLogCore.h` | 942 | 0 | Battery-log image only |
| `libs/hardware/PowerManager/include/PowerManager.h` | 82 | 3 | Pin/rail/sleep hardware; recovery/release policy |
| `libs/hardware/PowerManager/src/BatteryLog.cpp` | 356 | 0 | Battery-log image only |
| `libs/hardware/PowerManager/src/PowerManager.cpp` | 331 | 9 | Pin/rail/sleep hardware; recovery/release policy |
| `libs/hardware/Rtc/include/Rtc.h` | 20 | 2 | RTC validity/identity-result API |
| `libs/hardware/Rtc/src/Rtc.cpp` | 105 | 28 | DS3231 identity; fallback/validity/low-power behavior |
| `libs/hardware/SDCardManager/include/SDCardManager.h` | 47 | 8 | Parked/gated SDMMC; generic cache sync; diagnostics |
| `libs/hardware/SDCardManager/src/SDCardManager.cpp` | 92 | 2 | Parked/gated SDMMC; generic cache sync; diagnostics |
| `libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp` | 23 | 6 | Parked/gated SDMMC; generic cache sync; diagnostics |
| `libs/hardware/SDCardManager/src/SdmmcBlockDevice.h` | 16 | 0 | Parked/gated SDMMC; generic cache sync; diagnostics |
| `libs/hardware/SDCardManager/src/SdmmcParkedMount.cpp` | 104 | 0 | Parked/gated SDMMC; generic cache sync; diagnostics |
| `libs/hardware/XteinkDetect/include/XteinkDetect.h` | 33 | 1 | Stable detector integration/cache; fixed 3.68-inch variant |
| `libs/hardware/XteinkDetect/src/XteinkDetect.cpp` | 204 | 9 | Stable detector integration/cache; fixed 3.68-inch variant |
| `libs/network/Opds/include/OpdsImplicitAuth.h` | 6 | 1 | Generic optional cancellation |
| `libs/network/Opds/include/OpdsTransport.h` | 5 | 0 | Generic optional cancellation |
| `libs/network/Opds/src/OpdsClient.cpp` | 1 | 1 | Generic optional cancellation |
| `libs/network/Opds/src/OpdsImplicitAuth.cpp` | 17 | 5 | Generic optional cancellation |

### Other CrossPoint paths

| Path | + | - | Classification |
|---|---:|---:|---|
| `.gitattributes` | 17 | 0 | Build/repository config |
| `.github/workflows/ci.yml` | 23 | 0 | Build/repository config |
| `.gitmodules` | 2 | 2 | Build/repository config |
| `USER_GUIDE.md` | 6 | 0 | Documentation |
| `docs/silkscreen/BATTERY-LOG.md` | 566 | 0 | Documentation |
| `docs/silkscreen/MODULARITY.md` | 229 | 0 | Documentation |
| `docs/silkscreen/PORTING.md` | 522 | 0 | Documentation |
| `docs/silkscreen/POWER.md` | 435 | 0 | Documentation |
| `docs/silkscreen/REQUIREMENTS.md` | 306 | 0 | Documentation |
| `docs/silkscreen/SYNC-LOG.md` | 9 | 0 | Documentation |
| `docs/silkscreen/TEST-SUITE.md` | 914 | 0 | Documentation |
| `docs/silkscreen/UPSTREAM-PRS.md` | 165 | 0 | Documentation |
| `docs/silkscreen/UPSTREAM-SYNC-REFERENCE.md` | 349 | 0 | Documentation |
| `docs/silkscreen/UPSTREAM-SYNC.md` | 104 | 0 | Documentation |
| `docs/silkscreen/pr-maps/A-sdk.md` | 382 | 0 | Documentation |
| `docs/silkscreen/pr-maps/B-main.md` | 296 | 0 | Documentation |
| `docs/silkscreen/pr-maps/C-app.md` | 368 | 0 | Documentation |
| `docs/silkscreen/pr-maps/README.md` | 15 | 0 | Documentation |
| `docs/webserver-endpoints.md` | 1 | 1 | Documentation |
| `freeink-sdk` | 1 | 1 | SDK gitlink |
| `platformio.ini` | 317 | 0 | Build/repository config |
| `scripts/check_sleep_wake_sources.sh` | 101 | 0 | Tooling |
| `scripts/check_test_firmware.sh` | 95 | 0 | Tooling |
| `scripts/silkscreen/battery_log_report.py` | 2002 | 0 | Tooling |
| `scripts/silkscreen/build_matrix.sh` | 212 | 0 | Tooling |
| `scripts/silkscreen/census_baseline.txt` | 1354 | 0 | Tooling |
| `scripts/silkscreen/check_no_touch.sh` | 205 | 0 | Tooling |
| `scripts/silkscreen/check_render_watchdog_elf.py` | 298 | 0 | Tooling |
| `scripts/silkscreen/freeze_images.py` | 128 | 0 | Tooling |
| `scripts/silkscreen/msvc/README.md` | 170 | 0 | Tooling |
| `scripts/silkscreen/msvc/bin-hostcc/c++` | 5 | 0 | Tooling |
| `scripts/silkscreen/msvc/bin-hostcc/cc` | 5 | 0 | Tooling |
| `scripts/silkscreen/msvc/bin-syntax/c++` | 5 | 0 | Tooling |
| `scripts/silkscreen/msvc/bin/c++` | 5 | 0 | Tooling |
| `scripts/silkscreen/msvc/cc_hostcc.bat` | 3 | 0 | Tooling |
| `scripts/silkscreen/msvc/cxx_hostcc.bat` | 3 | 0 | Tooling |
| `scripts/silkscreen/msvc/cxx_msvc.bat` | 3 | 0 | Tooling |
| `scripts/silkscreen/msvc/cxx_msvc.py` | 107 | 0 | Tooling |
| `scripts/silkscreen/msvc/cxx_syntax.bat` | 3 | 0 | Tooling |
| `scripts/silkscreen/msvc/cxx_syntax.py` | 38 | 0 | Tooling |
| `scripts/silkscreen/msvc/gnu_shim.h` | 16 | 0 | Tooling |
| `scripts/silkscreen/msvc/hostcc.py` | 89 | 0 | Tooling |
| `scripts/silkscreen/msvc/run_app_host_tests.py` | 90 | 0 | Tooling |
| `scripts/silkscreen/msvc/run_picco_shim.py` | 19 | 0 | Tooling |
| `scripts/silkscreen/pio_locked.sh` | 56 | 0 | Tooling |
| `scripts/silkscreen/publish.sh` | 50 | 0 | Tooling |
| `scripts/silkscreen/render_watchdog_host_test.cpp` | 263 | 0 | Tooling |
| `scripts/silkscreen/sync.sh` | 339 | 0 | Tooling |
| `scripts/silkscreen/sync_census.py` | 292 | 0 | Tooling |
| `scripts/silkscreen/test_battery_log_report.py` | 1355 | 0 | Tooling |
| `scripts/silkscreen/test_render_watchdog.py` | 758 | 0 | Tooling |
| `scripts/silkscreen/upstream_status.sh` | 404 | 0 | Tooling |
| `src/activities/testsuite/BoardOptions.cpp` | 135 | 0 | Hardware test firmware |
| `src/activities/testsuite/BoardOptions.h` | 37 | 0 | Hardware test firmware |
| `src/activities/testsuite/PlaceholderTest.h` | 27 | 0 | Hardware test firmware |
| `src/activities/testsuite/SleepRecord.cpp` | 186 | 0 | Hardware test firmware |
| `src/activities/testsuite/SleepRecord.h` | 76 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestCatalog.cpp` | 76 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestCatalog.h` | 38 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestChoiceActivity.cpp` | 43 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestChoiceActivity.h` | 33 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestResultLog.cpp` | 177 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestResultLog.h` | 48 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestScreen.cpp` | 334 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestScreen.h` | 153 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestSleepActivity.cpp` | 76 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestSleepActivity.h` | 18 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestStrings.h` | 96 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestSuite.cpp` | 110 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestSuite.h` | 22 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestSuiteAccess.h` | 26 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestSuiteActivity.cpp` | 129 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestSuiteActivity.h` | 32 | 0 | Hardware test firmware |
| `src/activities/testsuite/TestSuiteRouting.cpp` | 51 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/BatteryTest.cpp` | 299 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/BoardOptionsTest.cpp` | 81 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/BootProbes.cpp` | 162 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/BootProbes.h` | 39 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/ChargeTest.cpp` | 734 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/FrontlightTest.cpp` | 894 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/GrayPass.cpp` | 174 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/GrayPass.h` | 59 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/GrayscaleTest.cpp` | 341 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/I2cScanTest.cpp` | 299 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/InversionTest.cpp` | 198 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/KeysTest.cpp` | 957 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/LightCheck.cpp` | 133 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/LightCheck.h` | 91 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/LightTripTest.cpp` | 881 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/LoadTest.cpp` | 738 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/NoTouchTest.h` | 40 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/OrientationTest.cpp` | 287 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/PanelFamily.h` | 26 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/PanelPatterns.cpp` | 177 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/PanelPatterns.h` | 36 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/PanelProbe.cpp` | 238 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/PanelProbe.h` | 42 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/PanelRun.cpp` | 266 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/PanelRun.h` | 156 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/ReaderData.cpp` | 332 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/ReaderData.h` | 65 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/ReaderDataTest.cpp` | 139 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/RefreshTest.cpp` | 503 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/RenderWatchdogTest.cpp` | 312 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/RenderWatchdogTest.h` | 18 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/RtcAccess.cpp` | 99 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/RtcAccess.h` | 38 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/RtcSleepTest.cpp` | 190 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/RtcTest.cpp` | 630 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/SdCardTest.cpp` | 660 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/Sha256.h` | 47 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/SleepWakeTest.cpp` | 669 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/SpiClockTest.cpp` | 367 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/Supply.h` | 35 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/SystemInfoTest.cpp` | 316 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/TestFactories.h` | 68 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/TestNames.cpp` | 148 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/TestNames.h` | 29 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/TextBuffer.cpp` | 24 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/TextBuffer.h` | 15 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/TouchMappingTest.cpp` | 845 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/TouchSleepTest.cpp` | 249 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/UsbTest.cpp` | 299 | 0 | Hardware test firmware |
| `src/activities/testsuite/tests/WifiTest.cpp` | 398 | 0 | Hardware test firmware |
| `test/clipping_sync/ClippingSyncTest.cpp` | 26 | 0 | Host tests/stubs |
| `test/clipping_sync/stubs/HalPowerManager.h` | 14 | 0 | Host tests/stubs |
| `test/test_recursive_dictionary.py` | 28 | 0 | Host tests/stubs |
| `test/test_selection_buttons.py` | 26 | 0 | Host tests/stubs |

### Other SDK paths (relative to freeink-sdk)

| Path | + | - | Classification |
|---|---:|---:|---|
| `README.md` | 257 | 8 | Documentation |
| `docs/testing.md` | 91 | 1 | Documentation |
| `docs/xteink-x3-uc8279-support.md` | 48 | 0 | Documentation |
| `libs/display/FreeInkDisplay/test/host/epdbus_txn_model.h` | 114 | 0 | Host tests/stubs |
| `libs/display/FreeInkDisplay/test/host/pro_stubs/BoardConfig.h` | 1 | 1 | Host tests/stubs |
| `libs/display/FreeInkDisplay/test/host/pro_stubs/EpdBus.h` | 1 | 1 | Host tests/stubs |
| `libs/display/FreeInkDisplay/test/host/run_uc8253_power.py` | 69 | 13 | Host tests/stubs |
| `libs/display/FreeInkDisplay/test/host/run_uc8279.py` | 39 | 12 | Host tests/stubs |
| `libs/display/FreeInkDisplay/test/host/run_x3_diagnostics.py` | 83 | 0 | Host tests/stubs |
| `libs/display/FreeInkDisplay/test/host/test_uc8279.cpp` | 58 | 1 | Host tests/stubs |
| `libs/display/FreeInkDisplay/test/host/test_x3_diagnostics.cpp` | 77 | 0 | Host tests/stubs |
| `libs/hardware/BatteryMonitor/test/host/run.sh` | 3 | 0 | Host tests/stubs |
| `libs/hardware/BatteryMonitor/test/host/stubs/Arduino.h` | 4 | 0 | Host tests/stubs |
| `libs/hardware/BatteryMonitor/test/host/stubs/Logging.h` | 6 | 0 | Host tests/stubs |
| `libs/hardware/BatteryMonitor/test/host/stubs/Wire.h` | 4 | 0 | Host tests/stubs |
| `libs/hardware/BatteryMonitor/test/host/stubs/freertos/FreeRTOS.h` | 5 | 0 | Host tests/stubs |
| `libs/hardware/BatteryMonitor/test/host/stubs/freertos/semphr.h` | 7 | 0 | Host tests/stubs |
| `libs/hardware/BatteryMonitor/test/host/test_charge_ladder_decoder.cpp` | 214 | 0 | Host tests/stubs |
| `libs/hardware/BoardConfig/test/host/README.md` | 62 | 0 | Host tests/stubs |
| `libs/hardware/BoardConfig/test/host/run_profile_asserts.py` | 207 | 0 | Host tests/stubs |
| `libs/hardware/BoardConfig/test/host/test_probe_checks.cpp` | 173 | 0 | Host tests/stubs |
| `libs/hardware/FrontlightManager/test/host/README.md` | 25 | 0 | Host tests/stubs |
| `libs/hardware/FrontlightManager/test/host/test_load_policy.cpp` | 182 | 0 | Host tests/stubs |
| `libs/hardware/HapticManager/test/host/stubs/freertos/semphr.h` | 9 | 0 | Host tests/stubs |
| `libs/hardware/Imu/test/host/stubs/Wire.h` | 4 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/eego_stubs/Wire.h` | 4 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/eego_stubs/driver/gpio.h` | 15 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/metalio_stubs/Arduino.h` | 4 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/metalio_stubs/Wire.h` | 5 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/metalio_stubs/driver/gpio.h` | 13 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/metalio_stubs/esp32-hal-periman.h` | 4 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/metalio_stubs/esp_rom_sys.h` | 2 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/metalio_stubs/esp_system.h` | 3 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/metalio_stubs/freertos/semphr.h` | 10 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/metalio_stubs/soc/soc_caps.h` | 2 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/picco_stubs/Arduino.h` | 4 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/picco_stubs/Wire.h` | 5 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/picco_stubs/driver/gpio.h` | 13 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/picco_stubs/esp32-hal-periman.h` | 4 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/picco_stubs/esp_rom_sys.h` | 2 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/picco_stubs/esp_system.h` | 3 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/picco_stubs/freertos/semphr.h` | 10 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/picco_stubs/soc/soc_caps.h` | 2 | 0 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/run.sh` | 4 | 1 | Host tests/stubs |
| `libs/hardware/InputManager/test/host/test_adc_ladder_decode.cpp` | 175 | 0 | Host tests/stubs |
| `libs/hardware/PowerManager/test/host/README.md` | 121 | 0 | Host tests/stubs |
| `libs/hardware/PowerManager/test/host/run_sleep_panel.py` | 125 | 0 | Host tests/stubs |
| `libs/hardware/PowerManager/test/host/test_batterylog_core.cpp` | 1113 | 0 | Host tests/stubs |
| `libs/hardware/PowerManager/test/host/test_sleep_panel.cpp` | 208 | 0 | Host tests/stubs |
| `libs/hardware/Rtc/test/host/stubs/BoardConfig.h` | 6 | 0 | Host tests/stubs |
| `libs/hardware/Rtc/test/host/stubs/driver/gpio.h` | 13 | 0 | Host tests/stubs |
| `libs/hardware/Rtc/test/host/stubs/esp32-hal-periman.h` | 4 | 0 | Host tests/stubs |
| `libs/hardware/Rtc/test/host/stubs/esp_rom_sys.h` | 3 | 0 | Host tests/stubs |
| `libs/hardware/SDCardManager/test/host/stubs/SdFat.h` | 3 | 0 | Host tests/stubs |
| `libs/hardware/XteinkDetect/test/host/README.md` | 67 | 0 | Host tests/stubs |
| `libs/hardware/XteinkDetect/test/host/stubs/Arduino.h` | 1 | 0 | Host tests/stubs |
| `libs/hardware/XteinkDetect/test/host/stubs/driver/gpio.h` | 5 | 0 | Host tests/stubs |
| `libs/hardware/XteinkDetect/test/host/stubs/esp_system.h` | 14 | 0 | Host tests/stubs |
| `libs/hardware/XteinkDetect/test/host/test_silkscreen_368_policy.cpp` | 141 | 0 | Host tests/stubs |
| `libs/hardware/XteinkDetect/test/host/test_silkscreen_policy.cpp` | 179 | 0 | Host tests/stubs |
