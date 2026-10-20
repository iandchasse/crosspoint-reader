# Hunk maps for the upstream PR series

Three read-only maps of what the fork adds to upstream, written 2026-10-20 for [UPSTREAM-PRS.md](../UPSTREAM-PRS.md). They are a snapshot: CrossPoint
`d076703a` against `upstream/develop` `0fcdb3ec`, and the SDK `bee6382` against `f9cc1be`. IDs and descriptions stay useful when cutting a topic branch;
line numbers do not survive the next sync (regenerate from `git diff <base> <ours>` when they matter).

| File | Covers | IDs |
|---|---|---|
| [A-sdk.md](A-sdk.md) | the whole SDK diff (103 files): buckets, hunk table, minimum set for a 4.26" board (packs P0-P7), capability macros, new `BoardConfig` fields, display tuning, entanglements E1-E15, generic fixes, risk register, hygiene | A1-A120, Q1-Q8 |
| [B-main.md](B-main.md) | `src/main.cpp` (+814/-50) hunk by hunk, `platformio.ini` (+317), `src/util/`, small libs, the render watchdog, the cutoff-poll plumbing, risk register, hygiene | B1-B71 |
| [C-app.md](C-app.md) | `lib/hal`, settings, mapped input, components, network, clippings, activities: buckets DEV / FL / KEY / BAT / RAD / DISP / WDOG / BLOG / TEST / X368 / GEN, the key-access and cutoff analyses, new persisted state, risk register, generic-fix candidates | C01-C221, Q1-Q8 |

Method of the maps: `git diff`, `git show` and `git grep` on the pinned revisions; every added and removed line is assigned to one hunk ID (the sums reproduce
the git totals). Nothing was built, run or measured: statements of the form "folds to nothing on other boards" are source readings, flash and RAM figures
marked GUESS are estimates, and `pio run -t size` on the ESP32-C3 envs is what settles them before any PR.
