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
