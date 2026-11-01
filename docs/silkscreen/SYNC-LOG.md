# Silkscreen sync log

One entry per sync, written by `scripts/silkscreen/sync.sh --accept`: what moved, what the census listed, what the builds took.
The procedure is [UPSTREAM-SYNC.md](UPSTREAM-SYNC.md); older, hand-written records are in [PORTING.md](PORTING.md#upstream-merges).

## 2026-10-20  SDK +9 (e67e889), CrossPoint +10 (upstream/develop 51089beb)

- Merged: SDK +9 (e67e889), CrossPoint +10 (upstream/develop 51089beb). Total 2m25s; census items: 4.
- Reviewed: Preserved Silkscreen touch-driver selection and long-press key paths; BLE discovery remains in its existing connection flow and clipping deletion stays key-accessible. All three CrossPoint images passed and were frozen (12m08s, 16m52s, 18m38s); this 2m25s record-only validation recovers a runner interruption after freezing, with two documented Windows host-test skips.

## 2026-10-22  SDK +5 (28edea3), CrossPoint +10 (upstream/develop 83df537d)

- Merged: SDK +5 (28edea3), CrossPoint +10 (upstream/develop 83df537d). Total 3m05s; census items: 0.
- Reviewed: Validation-only pass after separate final combined-tree reader builds/checks (batlog 924s, reader 1536s, 368 1120s), all green with zero compiler warnings and frozen images. Reconciled the incoming sparse-font/advance-table incompatibility (42 production-host checks plus negative control), took SDK main 28edea3 explicitly, and preserved the uncommitted cleanup patches; CrossInk follows its separate manual sync.

## 2026-10-23  SDK +11 (059275e), CrossPoint +13 (upstream/develop facaa87e)

- Merged: SDK +11 (059275e), CrossPoint +13 (upstream/develop facaa87e). Total 1m30s; census items: 11.
- Reviewed: Read the 11 census additions and 3 removals: panel-probe GPIO changes are Picco/Sticky gated; EEGO frontlight detection is board gated; the ActivityManager presence check preserves Silkscreen key/gesture access without probing the light at boot. Plugin-only sync and sleep session delivery retain cutoff/awake bounds and radio teardown. Resolved SDK BoardConfig field ordering/testing docs and CrossPoint dictionary-history assertions while retaining cancelled-selection coverage. Owner requested latest FreeInk main (059275e), including the three commits beyond CrossPoint pin 37d272d. CrossInk development 3d815cf was already included; its gitlink now matches the shared SDK 7326126. Prior local cleanups, owner documentation and supported default-Off Standard HALF Refresh patches were restored before all six builds; these pre-existing edits remain uncommitted. SDK profile/display/battery/UI and targeted settings tests pass; routine app host tests pass with two known Windows skips (trusted_time and patch_animatedgif). The new, unused ProgressiveJpeg fixture suite needs absent cjpeg/djpeg tools and was not run. CrossInk retains seven known narrowing warnings per build, identical to the previous build; no new compiler warnings. Combined working-tree builds: cp silkscreen-batlog exit=0 flash=5396870 ram=112724 (562 s including guards/image checks); cp silkscreen exit=0 flash=5432070 ram=112204 (581 s including guards/image checks); cp silkscreen-368 exit=0 flash=5416058 ram=112044 (638 s including guards/image checks); ci silkscreen-batlog exit=0 flash=5381713 ram=122360 warnings=7 narrowing=7 (743 s including guards/image checks); ci silkscreen exit=0 flash=5372997 ram=121824 warnings=7 narrowing=7 (806 s including guards/image checks); ci silkscreen-368 exit=0 flash=5247205 ram=118072 warnings=7 narrowing=7 (792 s including guards/image checks). Warm caches retained; no other-board builds, push, website publication or flashing.

## 2026-10-25  SDK +8 (8a6573b), CrossPoint +11 (upstream/develop 116195b4)

- Merged: SDK +8 (8a6573b), CrossPoint +11 (upstream/develop 116195b4). Total 1m34s; census items: 6.
- Reviewed: The scripted total above is the clean-tree checks/census run without builds or freezing; the final combined-tree checks and builds followed. Read the six census additions and one removal: the upstream control-center icon row retains key entry and brightness/warmth controls; plugin uninstall supports touch hold and physical Confirm hold with confirmation. No new wake, radio or automatic-light activation paths. Resolved the SDK testing-doc and IMU Wire stub conflicts by retaining both local shared-bus support and upstream BMI270 coverage; added the new reserveFullRecord no-op to the existing HTTP test fake. Resolved the CrossPoint panel comment around the upstream icon row. Took latest FreeInk main 8a6573b as requested; shared SDK is 4020a66. CrossInk development 3d815cf was already included and its SDK gitlink now matches. All prior local cleanups, 250 ms deferred light restore, 1-100 interpolated brightness mapping with downward migration, supported Standard HALF Refresh toggle in both apps and watchdog-checker corrections were restored before the final checks and all six builds; that work remains uncommitted. Final app host checks and clipping sync pass with two established Windows skips (trusted_time and patch_animatedgif). SDK profiles (41 cases), IMU, battery, haptic, input variants and panel sleep checks pass. SecureNet HTTP harness passes; HTTPS full-record buffer behavior still needs device validation. Final status lenses pass except expected dirty-tree notices. No new compiler warnings; any emitted CrossInk warnings match the established narrowing-warning baseline. Combined-tree builds: cp silkscreen-batlog exit=0 flash=5402418 ram=112764 (628 s including guards/image checks); cp silkscreen exit=0 flash=5437654 ram=112244 (643 s including guards/image checks); cp silkscreen-368 exit=0 flash=5421626 ram=112084 (674 s including guards/image checks); ci silkscreen-batlog exit=0 flash=5382645 ram=122408 warnings=7 narrowing=7 (1127 s including guards/image checks); ci silkscreen exit=0 flash=5374065 ram=121872 warnings=7 narrowing=7 (924 s including guards/image checks); ci silkscreen-368 exit=0 flash=5250549 ram=118120 warnings=7 narrowing=7 (974 s including guards/image checks). Six complete flash bundles verified against fresh factory images and partition bounds. Warm caches retained; no other-board builds, push, website publication or flashing.

## 2026-10-26  SDK +5 (83f8e01), CrossPoint +3 (upstream/develop 0727f9f0)

- Merged: SDK +5 (83f8e01), CrossPoint +3 (upstream/develop 0727f9f0). Total 0m39s; census items: 6.
- Reviewed: The scripted total above is the clean-tree checks/census run without builds or freezing; the final combined-tree checks and builds followed. Read the six census additions and seven removals: panel entry now follows touch availability, absent-light guards avoid light persistence/input on boards without a light, and the shorter quick-control row is layout only. Existing key routes in settings remain available, including Bluetooth. No panel entry enables the light. Resolved the plugin fetch conflict by hoisting the existing watchdog/browser/low-battery callback and passing it to both http.connect() and fetchResumable(); this preserves battery aborts while taking upstream connect-before-header-copy behavior. Kept upstream OPDS render locks and member-owned page props without adding a nested releaseEntries lock. Took latest FreeInk main 83f8e01 as requested; shared SDK is 2d7f674. CrossInk development 3d815cf was already included; both gitlinks match the shared line. SDK option-dialog window fields default to the previous behavior for current app callers, and display changes are confined to the A4 driver with no public API break. All prior local cleanups, 250 ms deferred light restore, 1-100 interpolated brightness mapping with downward migration, Standard HALF Refresh toggle in both apps and watchdog-checker corrections were restored before final checks and all six builds; that work remains uncommitted. Targeted app checks for frontlight callers, Standard HALF Refresh and keyed settings pass (zero skips). FreeInkUI host tests pass (295951 checks); the real A4 driver host harness passes four-gray/Gray3 encoding and per-refresh power-off checks. No hardware waveform or Bluetooth/OPDS/TLS device test is claimed. Clean-tree sleep/wake, watchdog, status lenses and battery-log analyzer checks pass. No new compiler warnings; any emitted CrossInk warnings match the established narrowing-warning baseline. Combined-tree builds: cp silkscreen-batlog exit=0 flash=5403786 ram=112772 (523 s including guards/image checks); cp silkscreen exit=0 flash=5439322 ram=112252 (550 s including guards/image checks); cp silkscreen-368 exit=0 flash=5423082 ram=112092 (428 s including guards/image checks); ci silkscreen-batlog exit=0 flash=5382661 ram=122408 warnings=7 narrowing=7 (446 s including guards/image checks); ci silkscreen exit=0 flash=5374065 ram=121872 warnings=7 narrowing=7 (464 s including guards/image checks); ci silkscreen-368 exit=0 flash=5250565 ram=118120 warnings=7 narrowing=7 (517 s including guards/image checks). Six complete flash bundles verified against fresh factory images and partition bounds. Warm caches retained; no other-board firmware builds, push, website publication or flashing.

## 2026-10-27  SDK +5 (252e9bb), CrossPoint +11 (upstream/develop a790b144)

- Merged: SDK +5 (252e9bb), CrossPoint +11 (upstream/develop a790b144). Total 40m07s; census items: 1.
- Reviewed: Integrated the landed FreeInk main pin required by CrossInk. Preserved deferred touch and ladder detection alongside LilyGO input, upstream spacing migration and Silkscreen strings; added the cutoff poll to plugin ETag hashing. Builds are run after restoring the uncommitted local feature changes.

## 2026-10-28  CrossPoint +5 (upstream/develop 60d3c8bb)

- Merged: CrossPoint +5 (upstream/develop 60d3c8bb). Validation checks 1m56s; census items: 3.
- PIO build total: 1915.81s.
- silkscreen-batlog: PIO 688.30s, exit=0 flash=5385230 ram=114316
- silkscreen: PIO 646.93s, exit=0 flash=5419802 ram=112756
- silkscreen-368: PIO 580.58s, exit=0 flash=5403502 ram=112596
- Reviewed: Retained the shared SDK (both required pins already contained), wake/light safeguards and keys beside touch; BMP long-press retains Confirm. Affected native tests (18) and local feature tests (5) passed; battery work remains uncommitted and unchanged CrossInk binaries were verified by source fingerprints.

## 2026-10-30  SDK +38 (f1e1d2b), CrossPoint +9 (upstream/develop ad53eb64)

- Merged: SDK +38 (f1e1d2b), CrossPoint +9 (upstream/develop ad53eb64). Validation checks 2m03s; census items: 17.
- CrossInk +6 (upstream/development 88f311be); shared SDK contains both app pins.
- crosspoint-reader/silkscreen-batlog: PIO 451.57s, exit=0 flash=5388098 ram=114308
- crosspoint-reader/silkscreen: PIO 473.77s, exit=0 flash=5422618 ram=112756
- crosspoint-reader/silkscreen-368: PIO 499.54s, exit=0 flash=5406218 ram=112588
- crossink/silkscreen-batlog: PIO 892.07s, exit=0 flash=5659081 ram=130224
- crossink/silkscreen: PIO 876.05s, exit=0 flash=5649417 ram=128672
- crossink/silkscreen-368: PIO 1019.51s, exit=0 flash=5522889 ram=124912
- PIO build total: 4212.51s; all six matching flash folders frozen.
- Reviewed: Census read: captive redirect does not renew the awake lease; upstream generic sleep/light and Metalio touch changes preserve Silkscreen gates. Kept TXT key access, battery estimator, capped light/restore safeguards and SSD1677 refresh toggle. Both apps share the SDK containing their required pins; USB waits now poll the Silkscreen cutoff. Affected host checks passed (one optional supplied-font fixture skipped), and all six builds passed; battery work remains uncommitted.

## 2026-10-30  CrossPoint +3 (upstream/develop e93f8d1f)

- Merged: CrossPoint +3 (upstream/develop e93f8d1f). Validation checks 2m07s; census items: 3.
- CrossInk already current at upstream/development 88f311be; shared SDK contains both app pins.
- crosspoint-reader/silkscreen-batlog: PIO 221.38s, exit=0 flash=5390914 ram=114308
- crosspoint-reader/silkscreen: PIO 216.42s, exit=0 flash=5425526 ram=112756
- crosspoint-reader/silkscreen-368: PIO 235.03s, exit=0 flash=5408998 ram=112588
- crossink/silkscreen-batlog: PIO 317.43s, exit=0 flash=5659065 ram=130224
- crossink/silkscreen: PIO 253.49s, exit=0 flash=5649385 ram=128672
- crossink/silkscreen-368: PIO 275.27s, exit=0 flash=5522885 ram=124912
- PIO build total: 1519.02s; all six matching flash folders frozen.
- Reviewed: Census read: plugin fetch and catalog changes preserve cutoff polling and radio release. CrossPoint +3 through e93f8d1f; CrossInk already current. Both apps retain shared SDK 4ae83f5, battery estimator, local cleanups, light limits/restore and SSD1677 refresh toggle. Battery-only Silkscreen redraw fix and all earlier battery WIP remain uncommitted. Redraw regression and catalog paging host tests passed; six firmware builds and image checks passed. Incoming plugin method/body requests inherit the SDK transport retry behavior; the code-level non-idempotent retry caveat is recorded in the run audit.

## 2026-11-01  SDK +2 (2d383f8), CrossPoint +10 (upstream/develop 91e9efc3)

- Merged: SDK +2 (2d383f8), CrossPoint +10 (upstream/develop 91e9efc3). Validation checks 1m19s; census items: 3.
- CrossInk +5 through upstream/development 72e5c574; shared SDK contains both app pins.
- crosspoint-reader/silkscreen-batlog: PIO 176.90s, exit=0 flash=5372922 ram=114332
- crosspoint-reader/silkscreen: PIO 344.91s, exit=0 flash=5407642 ram=112796
- crosspoint-reader/silkscreen-368: PIO 343.91s, exit=0 flash=5391042 ram=112628
- crossink/silkscreen-batlog: PIO 742.10s, exit=0 flash=5663405 ram=130224
- crossink/silkscreen: PIO 748.27s, exit=0 flash=5653713 ram=128672
- crossink/silkscreen-368: PIO 735.62s, exit=0 flash=5527461 ram=124912
- PIO build total: 3091.71s; all six matching flash folders frozen.
- Reviewed: Census read: kept cutoff callbacks on mapped/retried catalog requests and guarded the new CrossInk streak download. CrossPoint +10, CrossInk +5; SDK takes app-required font pin 2d383f8, both apps share f5b453d. Original battery estimator/passive redraw, light safeguards and refresh toggle preserved. RV-8263 driver fix and all earlier WIP remain uncommitted; RTC sanitizers, 193 targeted native tests, font suites and six firmware builds passed. Physical RTC ticking/sleep retention still needs the owner test.
