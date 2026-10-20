# Upstreaming the 4.26" Silkscreen to CrossPoint and the FreeInk SDK

Plan of 2026-10-20. Nothing here is pushed, committed upstream-side or opened; the fork stays the working line. CrossInk and the 3.68" variant
are out of this series. The hunk maps behind the numbers are in [pr-maps/](pr-maps/) (a snapshot at CrossPoint `d076703a` and SDK `bee6382`: the
IDs and descriptions stay useful, the line numbers do not survive the next sync).

## 1. The goal and the rules we plan around

The owner's goal: the 4.26" Silkscreen in CrossPoint's included devices (v2.0.0rc, 2026-10-19, lists Metalio E-Ink 4, EEGO A4 and BOOX Picco as the
ESP32-S3 devices of 2.0.0). A second reason: the fork's permanent merge surface is 204 app files (+29,135 / -318) and 103 SDK files
(+11,062 / -182); every line upstream takes is a line the next sync no longer carries.

What upstream's own files say (crosspoint-reader `AGENTS.md`, `SCOPE.md`, `.github/PULL_REQUEST_TEMPLATE.md`, `.agents/`; the SDK `README.md` "Adding a new device"):

- **One concern per PR** (`refactor-for-review`). Device support is welcome; device-specific code stays behind the HAL / SDK boundary; downstream-specific
  UI, policy and feature semantics are out of scope; flash footprint is a priority (the ESP32-C3 is the tightest target), so what other boards compile
  must be near zero. "Adding a new device means adding data (a board profile + a driver config), not editing the generic code."
- **Who does what.** The human writes PR descriptions and opens PRs; an agent gives factual notes and test results, never ready-to-paste PR prose, and never
  opens or closes a PR. Push only on the owner's explicit instruction. Creating or amending a local commit needs the owner's approval. Hardware testing is
  the owner's and happens before a PR. The template asks whether AI tools were used (YES / PARTIALLY / NO): the owner answers.
- **Coordinate first.** A PR that touches `freeink-sdk/`, `lib/hal/`, the bootloader, OTA or recovery needs a maintainer's heads-up: a Discussion in the
  Ideas category of crosspoint-reader is the place (the SDK repo has no Discussions tab and 22 open issues).
- **Mechanics.** Base `develop` (SDK: `main`). Branch `<type>/<short-description>`, commit `<type>: <summary>` (50 characters, no scope), no assistant
  trailers. `firmware-handoff` per logical change: four read-only reviewers (correctness, architecture, embedded, i18n/docs), at most two rounds, a final
  build of each relevant target, and the owner's explicit acceptance of the behaviour and the maintenance it brings.
- **Precedent for size.** CrossPoint-side device PRs: X4 Classic +61/-11 (7 files), EEGO A4 +214/-55, Metalio +264/-42, Picco +384/-44 (26 files), merged in
  1 to 8 days. SDK-side: Waveshare 397 +456/-15, Murphy M4 +673/-435, EEGO A4 frontlight +168/-62, Picco +1,718/-49, Metalio +2,711/-59 (the largest, by a
  maintainer, 0 reviews). A newcomer's 9,000-line first PR would be unreviewable; PRs of 100 to 2,500 lines are what has been merged.

## 2. Remotes (state 2026-10-20)

| Repo | Remote | URL | Use |
|---|---|---|---|
| crosspoint-reader | `upstream` | crosspoint-reader/crosspoint-reader (push disabled) | base of every topic branch: `upstream/develop` |
| crosspoint-reader | `fork` | iandchasse/crosspoint-reader-de-link (public fork; added, never fetched or pushed) | topic branches only |
| freeink-sdk | `fk-upstream` | Free-Ink/freeink-sdk (push disabled) | base: `fk-upstream/main` |
| freeink-sdk | `origin` | iandchasse/freeink-sdk (public fork) | topic branches only |

The `silkscreen` branches (215 CrossPoint commits, 85 SDK, CrossInk's) exist only on this PC, and `.gitmodules` names an SDK branch that is on no remote.
Do not push them to the public forks: that would publish the whole fork (docs, notes, the test suite). A private backup is a separate step and needs the
owner's yes. A topic branch keeps upstream's `.gitmodules` and changes only the `freeink-sdk` gitlink, to an SDK commit that exists in Free-Ink's repo,
which is why the SDK PR merges before the CrossPoint PRs that need it. Upstream's `develop` is the default branch of the CrossPoint fork's parent; the fork's
own default branch (`s3-port`) is old and irrelevant to a PR.

## 3. What the fork adds to the 4.26" device

CrossPoint, `git diff upstream/develop d076703a`: 204 files, +29,135 / -318.

| Part | Files | Lines | Upstream? |
|---|---|---|---|
| Hardware test firmware (`src/activities/testsuite/`, env `silkscreen-test`) | 72 | +15,766 | no: the owner's bench tool |
| Sync and guard tooling (`scripts/`) | 25 | +6,240 | no |
| Documents (`docs/silkscreen/`) | 7 | +3,288 | no (a short device page may go) |
| Production code and config | about 100 | about +3,800 / -318 | by concern, below |

By concern, production code of the app (hunk maps B and C; `main.cpp` +814/-50 counted in its concern):

| Concern | App lines | In the first series? |
|---|---|---|
| Device env and glue (`platformio.ini` envs 73, tag 2, CI, HAL glue 393) | about 470 | yes (D1); the glue shrinks to what a first boot needs |
| Key access beside touch (24 files) | +177 / -67 | yes (K1) |
| Radio and idle rules, of which the generic fixes (Wi-Fi left on, CPU spinning on result screens) | +234 / -43 and +115 in `main.cpp` | the generic part only (F1) |
| No-hardware-off safety: low-battery cutoff, empty-battery sleep, reset-loop sleep, transfers stop at the cutoff | +220 / -29 in 16 files, +205 in `main.cpp`, +53 / -28 in util | wave 2 |
| Front light: safety notices, Toggle Frontlight, power double-click, panel keys | +451 / -56, +107 in `main.cpp` | wave 2 |
| Battery display (5 % steps, "No battery") | +128 / -17 | wave 2 |
| Render watchdog | +511 in util, +88 in `main.cpp`, +46 hooks | wave 2 / downstream |
| Battery log, test-suite branches, 3.68" variant, hardware checks | about 330 + 100 + 35 | no |

81 % of the added app lines compile into every board; most fold to nothing because an SDK policy value is 0 off Silkscreen. None of it has been measured
against the ESP32-C3's flash budget.

FreeInk SDK, `git diff f9cc1be bee6382` (upstream CrossPoint's pin): 103 files, +11,062 / -182; source 43 files +7,033 / -144, tests 57 files +3,634, docs +395.
Source lines by concern: probes 1,715; battery log 1,441; hardware (power, parking, SD, I2C, RTC) 1,015; diagnostics 798; front light 465 (gate 293, blend 172);
policy 428; profile and registration 351; 3.68" variant 303; ADC-ladder input 248; generic fixes 160; display tuning 54. Only 42 % of the source lines sit
inside a Silkscreen-only `#if`; the rest is data-gated at runtime or header-only, so "other boards pay nothing" is true for behaviour, not yet shown for flash.
A 4.26" board needs about 1,600-1,700 of the 7,033 (no touch) or 1,900-2,000 (with the touch tail); the other 5,000 are the owner's own tooling.

## 4. The series

**Five PRs first, in this order of independence.** Two small fixes that need no device, then the device in three parts.

| # | Repo | Branch and title | Contents | Size | Needs |
|---|---|---|---|---|---|
| S0 | SDK | `fix/sd-volume-sync`: fix: sync the SD volume before shutdown | `SDCardManager::syncVolume()` before `FsVolume::end()` in `shutdown()` and in USB-MSC detach (map A102). The base comment says `end()` flushes SdFat's caches; in SdFat 2.3.1 (the copy in the owner's CrossInk libdeps; upstream's pinned version is still to be checked) it only clears pointers, so a dirtied sector or an open writer is lost at sleep | about +40 / -10, one host stub | nothing |
| F1 | CP | `fix/flow-radio-idle`: fix: switch Wi-Fi off and idle the CPU when a flow ends | the policy-free form of the radio rules: OTA, clock sync, KOReader auth and sync, Plugin Hub Done, plugin catalog, loan-time sync, catalog and the Clear Cache / Font Download result screens (maps C145-C196, Q8 G2-G5): the radio is released when a flow ends instead of at exit, a finished result screen no longer holds the CPU at full speed or blocks auto-sleep. Read from the code (upstream's OTA activity does leave Wi-Fi up on its result screens until exit), not yet run on a device | about +100-150 / -40 in about 12 files (estimate) | nothing |
| S1 | SDK | `feat/silkscreen-board`: feat: Add Silkscreen board support | the minimum set as ten commits (A report, Q1): P0 ADC lock (S3 only), P1 registration and capability defaults, P2 ADC-ladder keys, P3 charge-status ladder, P4 pin parking and the externally biased power key, P5 parked SDMMC, P6 front-light gate (fixed warm/cool, no monitor, probe or blend), P7 I2C and RTC low power, P8 touch tail (hibernate, reset, `buttonsAlongsideTouch`), P9 the slim profile (about 170 of 288 lines), the SSD1677 config, a sample env, a device page, host tests | about 1,900-2,000 source lines + about 700 tests and docs | S0 (its parked-SD branch calls `syncVolume`) |
| D1 | CP | `feat/silkscreen-device`: feat: Add support for Silkscreen | `[silkscreen]` + `silkscreen`, `-gh_release`, `-gh_release_rc` (about 60 ini lines), the board tag (+2), the CI / release matrices (+12), README and device-table rows, the one `main.cpp` line (B51), the SDK pointer, and the HAL glue a first boot needs (charge-ladder polling, the USB state inferred from it, touch and I2C sleep calls); the light on through the SDK defaults, without the probe | about +150 plus HAL glue (measured when the branch is cut; at most about 400) | S1 merged |
| K1 | CP | `feat/keys-beside-touch`: feat: Use the keys on touch screens that have a full key set | the KEY bucket (map C, Q1): key hints and cursors drawn on touch screens, clip and look-up selection by keys when no finger is down, a Look Up / Clip / Bookmark list on the second Confirm, warmth by keys, keyboard layout, help lines. It reads `TouchConfig::buttonsAlongsideTouch` and `BoardConfig::hasFrontButtons()` (P8) and does nothing on any upstream device today | +177 / -67 in 24 files, +26 test | S1 merged, after D1 |

Also in wave 1, as an **issue, not a PR**: the recursive dictionary look-up keeps its selected range after Back, and on keys-only devices the range start can
never be moved (map C, Q8 G1; the fork forgets the range on Silkscreen only). A repro and a nine-line fix; the maintainers decide whether the kept range is a
feature.

Order and independence: S0, F1 and the issue can go the day the owner is ready and depend on nothing. S1 starts review in parallel and merges after S0. D1 and
K1 are cut when S1 is merged (each needs the SDK pointer) and are independent of each other in code, so they can be reviewed in parallel; K1 is merged after D1
because it does nothing without the device. Each merged PR removes its hunks from the fork's diff: at the next sync the conflict resolves to upstream's text.

Why five and not fewer or more. One or two PRs would be about 9,000 lines across two repos and would fail upstream's own one-concern rule and its review
capacity. Seven or more means ten reviewer handoffs, ten owner hardware sessions and ten rounds of maintainer attention for changes that mostly cannot be
merged separately anyway (each SDK mechanism would sit unused until the profile lands, which `scope-discipline` calls a speculative hook). If a maintainer asks
for a smaller SDK PR, the cut lines are the packs: P0-P5 + P7 (bring-up: boots, draws, keys, charge, sleep, SD, RTC) and P6 + P8 (front light, touch), each of
which builds and runs on its own with the other's profile fields left at their defaults; the SDK map (A report, Q6) names finer cuts (ADC ladders, pin parking,
charge ladder, parked SD, light, I2C/RTC, touch hibernate) for the case that is wanted.

### Wave 2: raise as Discussions after the device is in; PRs only if a maintainer asks

| Id | What | Size | Why it is not in wave 1 |
|---|---|---|---|
| W-P | no-hardware-off safety: low-battery cutoff at boot, in the loop and during transfers, empty-battery sleep, reset-loop sleep, the update gate; `PowerConfig` fields | SDK about 430, app about +480 / -60 | policy; nine of the twelve `PowerConfig` fields have no SDK reader, only CrossPoint; SCOPE.md rules downstream policy out; enabling it for other boards would change what they ship |
| W-L | front light: load probe, trip and "no light" notices, blend, Toggle Frontlight, power double-click generalised from the X4 Pro code, panel keys | SDK about 640, app about +560 / -64 | the X4 Pro double-click edit touches upstream code (30 renamed lines); the probe is a feature for a light that may not be fitted |
| W-B | battery display: 5 % steps, "No battery", the Li-ion curve in tenths | about +170 | policy |
| W-W | render watchdog (a render that runs 5 minutes on battery restarts into sleep) | about +650 | needs W-P; the hang class it guards was an SDK bug since fixed upstream (Free-Ink #139); the implementation is Silkscreen-shaped (ladder-based USB test, driver-free sleep boot path) |

### Stays downstream for good

The hardware test firmware and its checks; the battery log and its analyzer; the diagnostics (`FREEINK_DIAGNOSTICS`); the bring-up probes of PROBES (1,715 SDK
lines) unless a maintainer wants them; the sync, guard and CI tooling; the 3.68" variant (a follow-up once the 4.26" board is in); the Silkscreen documents.
The honest effect on syncs: wave 1 removes about a third of the production-code conflict surface (K1 and F1 are in the busiest UI files), but `main.cpp` and
`lib/hal` stay conflict hotspots until wave 2, because their Silkscreen hunks are policy. Moving those blocks out of `main.cpp` into their own files, with a
handful of hook lines left behind, would cut that surface whether or not upstream ever takes them.

## 5. How a topic branch is cut

1. A separate worktree, so the fork's tree and builds stay as they are: `git worktree add ../pr-<name> -b <type>/<desc> upstream/develop` (SDK:
   `fk-upstream/main`). For CrossPoint the SDK pointer is set last, to a merged SDK commit.
2. The PR's content comes from the maps: new files whole; shared files by hunk, applied with context (never `--unidiff-zero`) and read afterwards. The mixed
   hunks (`main.cpp`, `HalPowerManager`, `BoardConfig.h` structs, `FrontlightManager`, `InputManager::begin()`) are hand-edited; the maps list each entanglement
   and how to separate it (A report Q6 E1-E15, B A2, C Q3).
3. Downstream tokens go: `SILKSCREEN_BATLOG`, `CROSSPOINT_TEST_SUITE`, `FREEINK_DIAGNOSTICS` arms; `isSilkscreen()` and policy reads become profile data or a
   capability; comments that name `POWER.md`, `AW-04`, `BAT-01`, `HARDWARE.md` or the de-link history are cut to one or two lines of what the code does; the
   three profile asserts that force the probe and policy capabilities on are deleted; `FREEINK_CAP_FRONTLIGHT_PROBE` becomes opt-in.
4. Checks before the owner is asked to test: the SDK host tests that cover the pack (ladder decode, charge ladder, profile asserts), `./bin/clang-format-fix -g`
   for the app (the SDK has no formatter), a warm build of `silkscreen`, flash and RAM deltas from `scripts/firmware_size_history.py`. The other boards' envs
   are built by the fork's GitHub Actions once the topic branch is pushed there (needs the owner's go), not by local cold builds.
5. `firmware-handoff`: four read-only reviewers in parallel, one round and at most one follow-up, then the final build. The owner reads the diff and accepts
   the behaviour and the maintenance in their own words; that acceptance is the step upstream's rules cannot skip.
6. Hardware, by the owner, on the board with the topic-branch image: S1 and D1 run the bring-up checks (keys, charge state, sleep and wake, SD, RTC, light, touch;
   the test firmware's T1-T3, T9, T11-T14, P3 and P5 are the evidence), K1 the key checks of the key-access list, F1 each flow's result screen with the
   radio state read back, S0 a write held open across a sleep.
7. The owner writes the PR text, answers the AI-usage question, pushes the branch to the fork and opens the PR.

## 6. Open points for the owner

- The order and the count above (five, with the dictionary issue), or a different cut.
- The front light in D1: on through the SDK defaults with the probe opt-in (recommended: it is the board's second feature and costs D1 two ini lines), or off as the
  fork's first commit had it.
- The touch tail inside S1 (recommended: K1 is meaningless without it; the cost is about 300 SDK lines).
- A private backup remote for the `silkscreen` branches, and the first push of a topic branch to a public fork: both need a yes.
- The Discussion that introduces the device and the series: the owner posts it. Facts for it: ESP32-S3-WROOM-1-N16R8; 4.26" 800x480 SSD1677 with tuned drive
  values; eight keys on two resistor ladders plus a power key on IO18 with an external bias; FT6336U touch; TPS923610 warm and cool light; SD card behind a PMOS
  gate with the pins parked in sleep; no fuel gauge, a 500 mAh cell read by ADC and an analog charge-status ladder; no hardware power-off; the PCB is public
  (iandchasse/silkscreen-pcb). One question for the maintainers: whether a community board without a purchase link fits the included-devices list.
- Local commits on the topic branches need the owner's approval under upstream's rules; a yes for the five branches of this series covers the first round.
