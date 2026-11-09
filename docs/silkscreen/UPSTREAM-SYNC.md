# Silkscreen upstream sync

One command brings the shared Silkscreen SDK line and CrossPoint up to their upstreams. Run it from
`crosspoint-reader/` in Git Bash:

```
scripts/silkscreen/sync.sh              # the shared SDK line, then CrossPoint
```

CrossInk is synced separately, using the [manual steps below](#crossink-sync). `sync.sh crossink` and `sync.sh all`
are not implemented: both exit 2. CrossInk must use the same shared SDK head as CrossPoint.

A person or an agent is needed when a merge stops on conflicts or the census lists something to read. The long procedure
this page replaced (every lens, every rule the history taught) is kept as
[UPSTREAM-SYNC-REFERENCE.md](UPSTREAM-SYNC-REFERENCE.md): use its merge guidance for a hard conflict or an unusual upstream
change, not its old build matrix or reviewer rounds as a routine checklist.

## What the command does

| Phase | What | Time (replay of the 2026-10-19 sync) |
|---|---|---|
| status | fetches the upstreams, says what is due, lists the files that would conflict (`git merge-tree`; nothing is changed) | 2-8 s |
| merge | the SDK commit upstream CrossPoint pins into the shared SDK line; the pointer move; `upstream/develop` into CrossPoint. Merge commits, never a rebase; the message lists the commits taken | 12 s |
| guards | the sleep-and-wake source guard, the render watchdog source guard, `upstream_status.sh --no-fetch` (the hook, pin and variant lenses), the battery log analyzer tests; the SDK profile asserts when `BoardConfig` moved, the app host tests when the clip selection, the clipping sync or `test/` moved | 20-30 s |
| census | `sync_census.py`: what the merge added or removed at the places that cost battery, hold a radio on, or leave the keys; plus the files the merge changed that the port had changed | 4 s |
| build | `silkscreen-batlog`, `silkscreen`, `silkscreen-368`, one at a time under the shared build lock, with the image checks `build_matrix.sh` runs for them | about 3 min per env when warm; about 15 when `platformio.ini`, the SDK pin's build flags or a dependency changed |
| freeze | the built images into `../flash/<env>/` (bootloader, partition table, boot selector, app, `flash_args`, SHA256SUMS, a one-step `.ps1`); the previous app image stays as `<env>-prev.bin` | seconds |

What it does **not** do: no build matrix, no other board's env (`x4pro`, `picco`, `gh_release`...), no proxy env (`c3check`
and `stickycheck` wipe all of `.pio/build`, so the next sync would build from scratch), no reviewer rounds, no second
round the same day, no hand-written records. `build_matrix.sh --full` is still there for a release; it is not part of a sync.
The bench firmware (`silkscreen-test`, `silkscreen-368-test`) is built when asked: `--also "silkscreen-test silkscreen-368-test"`.
It has routing stubs of its own, so an upstream change to `ActivityManager` or `CrossPointSettings` can break it alone.

Options: `--envs`, `--also`, `--no-merge` (validate the current tree against the already-fetched upstream snapshot), `--no-build`,
`--no-freeze`, `--dry-run`, `--accept`, `--note`. `sync.sh --help` lists them.

## CrossInk sync

This is a manual sync after the shared SDK / CrossPoint sync. Use step 3 of
[the reference procedure](UPSTREAM-SYNC-REFERENCE.md#the-steps) and the SDK-pin cases and drain checklist in
CrossInk's `docs/silkscreen/PORTING.md`. Keep the routine work lean:

1. In the clean `crossink/` checkout on `silkscreen`, run `git fetch crossink`, inspect the incoming commits, then
   `git merge crossink/development`. Keep upstream's intent and re-apply the required Silkscreen differences.
2. Resolve the SDK gitlink to the shared `silkscreen` head in `crosspoint-reader/freeink-sdk`, including when the merge
   reports no conflict. If CrossInk's incoming SDK pin is already an ancestor of that head, keep the shared head. If it
   is a newer commit on Free-Ink `main`, merge it into the shared line first and update both apps' gitlinks. For a
   pre-merge SDK PR pin, keep the shared head; if new app code requires that API, wait for it to land in Free-Ink `main`.
   Never take CrossInk's pin with `--theirs` without checking. Fetch `silkscreen-sdk` in `crossink/freeink-sdk` as needed,
   then run `git submodule update` after resolving the gitlink so the SDK checkout matches it.
3. Keep every `Silkscreen:` CHANGELOG line under `## [Unreleased]` (recreate the heading if a release renamed it).
   Read the incoming hunks against CrossInk's drain and key-access rules, and retain the battery-log and watchdog hooks.
   From `crosspoint-reader/`, `scripts/silkscreen/upstream_status.sh --no-fetch` checks both apps' pins and hooks.
   Its pin checks read committed `HEAD` gitlinks; pending pin changes remain flagged until approved commits exist.
4. Build the three routine CrossInk envs under the shared lock. Starting in `crosspoint-reader/`:

   ```bash
   cd ../crossink
   ../crosspoint-reader/scripts/silkscreen/pio_locked.sh "crossink-sync" \
     bash scripts/silkscreen/build_matrix.sh silkscreen-batlog silkscreen silkscreen-368
   ```

   Explicit envs avoid the script's default matrix and proxy builds. The script runs CrossInk's sleep-wake and render
   watchdog guards and image checks. Build `silkscreen-368-batlog` too only when that image is needed.
5. After successful builds and resolved findings, return to `crosspoint-reader/` (`cd ../crosspoint-reader` after
   the build above) and freeze:

   ```bash
   python -I -X utf8 scripts/silkscreen/freeze_images.py --repo crossink --prefix crossink- \
     silkscreen-batlog silkscreen silkscreen-368
   ```

`sync.sh`'s resume state, census, exit codes and `--accept` / `SYNC-LOG.md` record cover the shared SDK and CrossPoint
only. They do not automate or accept the CrossInk sync. Commit only with the owner's approval; publish only when asked.

## When it stops

- **Exit 10, a merge stopped on conflicts.** It names the files. For each, find the upstream PR in the merge message
  and read it, keep upstream's intent, then re-apply the Silkscreen hunk on top. Commit the merge (the prepared message is in
  `.git/MERGE_MSG`; add the conflict resolutions to its body) and run `sync.sh` again: it carries on. The hotspots are
  `src/main.cpp`, `lib/hal/HalGPIO.*`, `lib/hal/HalPowerManager.*`, `src/SettingsList.*`, `src/CrossPointSettings.*`,
  `src/activities/settings/SettingsActivity.cpp`, `src/activities/ActivityManager.cpp`, the clip selection files,
  `platformio.ini` (the `[silkscreen]` and `[silkscreen368]` sections) and the SDK's `BoardConfig.h`. A clean auto-merge of a
  hotspot can still be wrong: on 2026-10-19 git merged `SettingsList.cpp` without a conflict, and the Silkscreen rows in it
  had still to be put back by hand. Read `git diff <pre-merge>..HEAD -- <file>` for the rows the port owns.
- **Exit 11, green, the census lists items.** Read those hunks (the command prints the file; it is about 100 lines for a big
  upstream drop, a few for a small one) and nothing else. Fix what they show, commit, run `sync.sh --no-merge`, then
  `sync.sh --accept --note "what was found and done"`.
- **Exit 12, a guard or a build failed.** The page says which. A guard that is not there is a failure, so a merge that
  deletes one cannot turn the sync green.
- **Exit 0.** Run `sync.sh --accept` to write the census baseline and the log entry (`SYNC-LOG.md`), and flash.

## The census

`sync_census.py` greps the app and the SDK (read through `git grep` at a revision, nothing is checked out) for nine kinds of
surface and compares a revision with the accepted baseline (`scripts/silkscreen/census_baseline.txt`):

| Rule | The question for a new line |
|---|---|
| NET | does the transfer poll the low-battery cutoff (BAT-01), and is it bounded? (it says whether its function mentions the cutoff API) |
| CUTOFF | a removed line is a dropped poll |
| RADIO | does every flow that turns Wi-Fi or Bluetooth on turn it off when the flow ends (AW-02)? |
| WAKE | does the sleep floor or the wake list change (POWER.md section 2)? |
| AWAKE | can an input source lift the awake ceiling (AW-01, AW-03)? |
| SD | does the card stay powered outside a use? |
| LIGHT | is the front light lit by anything but a user action or Restore Light on Wake? |
| PINS | does a pin float or a pull drain in sleep? |
| TOUCH | is every function a new touch control reaches also reachable with the keys (key access)? |

Replayed on the merges of 2026-10-19 it listed every defect the reviewers of those syncs found, in about 120 lines: the
transfers and the plugin catalog without a cutoff poll, the OPDS transport, the Plugin Hub's Wi-Fi, the Bluetooth remote
beside the awake ceiling, the dictionary's touch-only long press. It cannot see a new caller of old code that polls less (fix
the callee, as the OPDS login now is), a rule that is in no pattern, or a thing that is wrong only on the board. The
battery log (`silkscreen-batlog`) is the empirical backstop: an unexpected drain shows up in the next days of the CSV.
A new pattern belongs in `RULES` in `sync_census.py`; after the census was read, `--accept` makes it the new baseline.

## Rules that stay

- **Match upstream.** At every fork in the road take upstream's choice (defaults, enum values, language and font sets, flags,
  wording, UI). A Silkscreen difference stays only where the hardware or a standing rule needs it (key access, drain, the
  front light, the board profile, the battery log, the 5 % display step), gated to Silkscreen, placed after upstream's entries,
  and named in the log entry with its reason. A choice this rule does not settle is reported, not made.
- **Order.** The shared SDK line takes the commit upstream CrossPoint pins (not Free-Ink `main`), CrossPoint's gitlink is the
  shared head, CrossInk's gitlink is always the shared head.
- **Merge, never rebase**; commits stay local; no AI co-author or generated-by trailer; never push without the owner's word.
- **Settings are stored by index**: append new enum values, never renumber. When upstream takes a value a Silkscreen shortcut
  held, upstream's number stays and Silkscreen's moves after it (the owner re-picks the shortcut).
- **One `pio` at a time**, across both checkouts: every build takes `.pio-build.lock` (`scripts/silkscreen/pio_locked.sh`),
  because pioarduino rewrites a shared package while a build runs.
  CrossInk's `lib_ignore = BLE` can also leave BT include paths removed after a
  sequential build. If CrossPoint then cannot find `esp_bt.h`, preserve the
  active `esp32s3/pioarduino-build.py` and restore its matching packaged
  `pioarduino-build.py.esp32s3` backup under that same lock before retrying.
  The packaged S3 script's `bt/include/esp32c3/include` path is intentional.
- **The SDK has no `.clang-format`**: format SDK edits by hand; run clang-format 21 on the app files you touched.
- A guard is a grep and cannot see `#if`: a new device-gated block with sleep calls of its own needs the guard's exemption read
  before the guard is widened.

## Build speed

A reader env rebuilt after a few source changes takes about 3 minutes; from scratch about 15 (NimBLE adds 189 object files).
Nothing in this procedure wipes `.pio/build`. The compiler runs far below the machine's capacity (24 jobs in flight, 27 % CPU,
two thirds of it kernel time: file opens and process starts), the usual signature of real-time antivirus scanning on Windows.
Excluding `%USERPROFILE%\.platformio` and `%USERPROFILE%\Documents\PlatformIO` from Defender's real-time scan is the owner's
call (it is a security setting); a WSL2 Ubuntu build mirror is the other lever and is not set up.

## Records

`SYNC-LOG.md` gets one entry per sync, written by `--accept`: what moved, the census counts, the builds with their minutes and
sizes, what the reader found. The older, hand-written rows are in [PORTING.md](PORTING.md#upstream-merges).
