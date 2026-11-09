# Upstreaming the 4.26" Silkscreen to CrossPoint and the FreeInk SDK

Updated **2026-10-21** after the [departure audit](DEPARTURE-AUDIT.md), a behavior-preserving cleanup pass, and the owner's direction to upstream the reusable firmware features first. No upstream topic branches or PRs have been created; the published fork remains the working line. CrossInk and the 3.68" variant are outside the first device series.

[Implementation Discussion #1](https://github.com/iandchasse/crosspoint-reader/discussions/1) is published in this fork's Ideas category for CrossPoint/FreeInk maintainer feedback. It covers the full feature inventory, proposed interfaces and contribution order, evidence limits, and the latest checked upstream commits. Publication does not establish upstream agreement on the proposals.

The audit compares CrossPoint `e9f8e0e3` with upstream `51089beb`, and SDK `eda3e3f` with upstream `e67e889`. The [hunk maps](pr-maps/) are older snapshots at CrossPoint `d076703a` / SDK `bee6382`; use their concern IDs as guides, then re-read the current code. Their minimal-port recipes and line counts are not the extraction authority.

## 1. The goal and the rules we plan around

The owner's goal: the 4.26" Silkscreen in CrossPoint's included devices (v2.0.0rc, 2026-10-19, lists Metalio E-Ink 4, EEGO A4 and BOOX Picco as the
ESP32-S3 devices of 2.0.0). The agreed order is **reusable firmware features on the existing upstream devices first, then Silkscreen's SDK hardware support, then CrossPoint device integration**. Each software concern should be understandable and testable without the new board. This also reduces repeated conflicts in the reader files before hardware support is added.

At the audit snapshot the app differs in 217 paths (+32,745/-321), the SDK in 103 (+11,072/-184). Most app additions are documentation, tooling and tests; production source/assets are 89 app paths (+3,385/-317) and 43 SDK paths (+7,042/-146). These are text counts, not image sizes.

What upstream's own files say (crosspoint-reader `AGENTS.md`, `SCOPE.md`, `.github/PULL_REQUEST_TEMPLATE.md`, `.agents/`; the SDK `README.md` "Adding a new device"):

- **One concern per PR** (`refactor-for-review`). Device support is welcome; device-specific code stays behind the HAL / SDK boundary; downstream-specific
  UI, policy and feature semantics are out of scope; flash footprint is a priority (the ESP32-C3 is the tightest target), so what other boards compile
  must be near zero. "Adding a new device means adding data (a board profile + a driver config), not editing the generic code."
- **Who does what.** The human writes PR descriptions and opens PRs; an agent gives factual notes and test results, never ready-to-paste PR prose, and never
  opens or closes a PR. Push only on the owner's explicit instruction. Creating or amending a local commit needs the owner's approval. Hardware testing is
  the owner's and happens before a PR. The template asks whether AI tools were used (YES / PARTIALLY / NO): the owner answers.
- **Coordinate first.** A PR that touches `freeink-sdk/`, `lib/hal/`, the bootloader, OTA or recovery needs a maintainer's heads-up. The recorded coordination route is a Discussion in CrossPoint's Ideas category.
- **Mechanics.** Base `develop` (SDK: `main`). Branch `<type>/<short-description>`, commit `<type>: <summary>` (50 characters, no scope), no assistant
  trailers. `firmware-handoff` per logical change: four read-only reviewers (correctness, architecture, embedded, i18n/docs), at most two rounds, a final
  build of each relevant target, and the owner's explicit acceptance of the behaviour and the maintenance it brings.
- **Precedent for size.** CrossPoint-side device PRs: X4 Classic +61/-11 (7 files), EEGO A4 +214/-55, Metalio +264/-42, Picco +384/-44 (26 files), merged in
  1 to 8 days. SDK-side: Waveshare 397 +456/-15, Murphy M4 +673/-435, EEGO A4 frontlight +168/-62, Picco +1,718/-49, Metalio +2,711/-59 (the largest, by a
  maintainer, 0 reviews). A newcomer's 9,000-line first PR would be unreviewable; PRs of 100 to 2,500 lines are what has been merged.

## 2. Remotes (state 2026-10-20, after the first publication and the clean-up)

| Repo | Remote | URL | Use |
|---|---|---|---|
| crosspoint-reader | `upstream` | crosspoint-reader/crosspoint-reader (push disabled) | base of every topic branch: `upstream/develop` |
| crosspoint-reader | `fork` | iandchasse/crosspoint-reader (public fork; the owner renamed it from `crosspoint-reader-de-link`) | `silkscreen` (its only branch, the default) and tag `silkscreen-port-1`; later the topic branches |
| freeink-sdk | `fk-upstream` | Free-Ink/freeink-sdk (push disabled) | base: `fk-upstream/main` |
| freeink-sdk | `origin` | iandchasse/freeink-sdk (public fork) | `silkscreen`, tag `silkscreen-port-1`; later the topic branches |
| crossink | `crossink` | uxjulia/CrossInk (push disabled) | upstream |
| crossink | `fork` | iandchasse/CrossInk (public, a separate repository and NOT a GitHub fork) | `silkscreen` (the default), plus `main` and `development` copied from upstream |

`scripts/silkscreen/publish.sh` pushes the three `silkscreen` branches (SDK first, fast-forward only, nothing forced; `--dry-run`). A plain clone of either app at
`silkscreen` resolves its `freeink-sdk` gitlink from `iandchasse/freeink-sdk` (checked with shallow clones). CrossInk is its own repository because GitHub allows one
fork per account per fork network and CrossInk shares CrossPoint's: asking for a second fork returns the first. The cost is the missing "forked from" badge and Sync
fork button on `iandchasse/CrossInk`; a pull request to `uxjulia/CrossInk` would have to come from a branch of `iandchasse/crosspoint-reader`, which is in the same network.

The CrossPoint repository is the old de-link fork (created 2026-02-20, 12 stars, two merged upstream PRs from it). It cannot be detached from its network, because
someone else has forked it (`AndieSandie/crosspoint-reader-de-link-devkitc`), and deleting it would drop the stars, so it was renamed and cleaned instead: on the owner's order
every branch except `silkscreen` was deleted on 2026-10-20 (`master`, `s3-port`, `s3-port-generic-epd`, `seamless-rtc`, `upstream-og`, `C3-custom-font-ttf`, `S3-custom-font-ttf`,
`feature/custom-fonts`, `gdey_075`, and `crossink/silkscreen` after the move). A verified git bundle of every ref as it was is in
`EPD_Hub_ESP32/github-archive/crosspoint-reader-fork-before-cleanup-2026-10-20.bundle` (233 MB, refs listed beside it). Two older local clones of that fork
(`crosspoint-reader-de-link`, `cp-de-link-2`) hold newer unpushed work (`s3-port` +45 commits, `de-link-v2` +18, `seamless-rtc` +1, a stash each); they were not touched.

A topic branch keeps upstream's `.gitmodules` and changes only the `freeink-sdk` gitlink, to an SDK commit that exists in Free-Ink's repo, which is why the SDK PR
merges before the CrossPoint PRs that need it. Upstream's default and PR base branch is `develop`. The fork's CI workflow runs only on pushes to `master` and on pull
requests, so pushing `silkscreen` starts nothing.

## 3. Boundaries for extraction

Extract generic firmware behavior before introducing the Silkscreen profile. Existing devices supply the baseline behavior and, where they have the relevant controls, the hardware validation. Optional product choices also get an explicit proposal and adoption decision before the device series; they are no longer deferred automatically until after Silkscreen.

| Concern | Intended placement |
|---|---|
| SD volume-cache synchronization | S0, generic SDK correctness fix, before device support |
| Terminal-screen CPU idling and state-specific radio ownership | F1, generic app corrections, before device support |
| Optional cancellation through plugin HTTP and OPDS login | N1, separate generic SDK/app topics before consumer power policy |
| Front-key capability beside touch | K0, small generic SDK prerequisite, independent of Silkscreen's profile |
| Keys beside touch, hints/cursors/spacing, selection geometry, remapping and key access to the light panel | K1, generic HAL/app support after K0 and before device support |
| Dictionary cancellation and actual light-state consistency | F2 and F3, independent generic fixes |
| Light shortcuts/blending, battery presentation, additional awake policy and watchdog/log retention | W-L, W-B, W-P and W-W, independent proposals and adoption decisions before the device series |
| Popup/formatting helpers, bounded settings and other small cleanups | Include only with the generic topic that uses them, or a separately justified cleanup |
| Non-Silkscreen raw-contact fallback | Independent fix decision; exclude from Silkscreen topics |
| Battery log, bench firmware and diagnostic SDK APIs | Assess reusable diagnostics separately first; retain board-specific tests outside the normal reader feature series |
| Profile, ADC keys/lock, analog charge decoder, pin/SD parking, external-bias power key, shared I2C/RTC, optional touch sleep and identity | S1, required SDK mechanisms after the reusable baseline |
| Stable SSD1677/UC8279D detection/cache/BUSY policy and driver-free panel sleep | S1; the UC8279D production driver is already upstream |
| Gated frontlight timing, fault state, load/rail wait and missing-string handling | S1 with the required HAL/outcome integration in D1 when light support is enabled |
| Early battery checks, cutoff during blocking work, update gate and clean sleep/recovery on a board without hardware off | Generic contracts agreed before S1/D1; board readings and required safe sequencing supplied by S1/D1 |

The SDK currently has 14 added `PowerConfig` fields; nine have app-only readers. Decide their ownership while extracting the software topics: reader timing and presentation belong with their firmware consumers; electrical configuration belongs with the SDK. Reuse existing HAL contracts before adding interfaces. Off-device zero flash/RAM overhead has not been measured: default-zero policy reads do not remove unconditional class fields or bookkeeping by themselves.

Generic does not mean enabling every behavior on every device. Preserve existing profile defaults and persisted values unless a separately accepted fix changes them. A touch device with a Home or page key does not necessarily have a complete navigation key set. The current SDK capability is `buttonsAlongsideTouch`; K0 must retain that distinction. HAL `hasFrontButtons()` also observes runtime touch availability, while the SDK predicate observes the profile. Preserve both meanings when optional touch is absent.

The current cleanup keeps all menu ordering and saved values, replaces temporary option vectors with bounded descriptors, shares the watchdog progress facade, removes an unreachable copied reset-sleep body, and collapses identical SD shutdown declarations. Larger `main.cpp` policy extraction, policy-state gating, test API relocation, and generic behavior changes remain separate work. The non-Silkscreen raw-contact fallback remains unchanged in the working fork: restoring it would change current behavior on other boards. Leave that unrelated hunk out of a device topic unless independently justified.

## 4. The series

The previous five-topic order is superseded. These are concern boundaries and dependencies, not a fixed PR count. Each actual PR must have one reviewable purpose and work on its stated upstream base. The old 1,900-2,000-line SDK and 400-line app ceilings are also withdrawn: they assumed removing required behavior.

### First: generic fixes and input support

S0, F1, F2, F3 and N1 can be assessed independently. K0 must precede K1. Start with small fixes; the table does not impose dependencies between unrelated topics.

| Id | Repo | Contents and validation boundary | Needs |
|---|---|---|---|
| S0 | SDK | Flush SdFat volume caches before unmount, shutdown and USB-MSC handoff. Upstream pins SdFat 2.3.1; `FsVolume::end()` clears pointers without this sync. Root sync does not commit an open writer's private size/first-cluster metadata: all file users must stop first. Cover existing SPI storage as well as the later SDMMC consumer. | Independent |
| F1 | CP | Result-screen CPU idling and end-of-flow radio release. Preserve Font Download's next-download path, Clock Sync's connection ownership, KOReader's upload/modem-sleep path, Plugin Hub Retry, and heap-cleanup restarts. Propose the generic behavior explicitly instead of copying Silkscreen policy gates. | Independent |
| F2 | CP | Dictionary cancellation clears the remembered selection start so keys can choose a fresh range. Explain the current Silkscreen-only restriction and agree the correction for existing key-driven readers. | Independent |
| F3 | CP | The frontlight panel toggles the actual state and follows changes made elsewhere. Test with existing frontlight controls; omit load-probe and missing-string notices. | Independent |
| N1 | SDK / CP, separate PRs | Optional cancellation propagation through OPDS implicit login and plugin HTTP/download/token operations. Retain the existing request/error contracts and default behavior without an abort callback. Do not claim every blocking request is interruptible. The concrete battery/awake predicate belongs to its consumer. | Relevant existing HTTP APIs; SDK changes merge before an app uses them |
| K0 | SDK | Add the small keys-beside-touch capability and profile predicate, preserving existing profiles' defaults. Upstream currently lacks `buttonsAlongsideTouch` and `hasFrontButtons()`. No Silkscreen pins, electrical drivers or profile are needed here. | Independent |
| K1 | CP | Generic HAL/input/UI support using K0: hints/cursors, spacing, keyboard hit bounds, remapping, selection geometry and touch/key ownership, Look Up / Clip / Bookmark action choice, Settings > Display > Frontlight and warmth keys. Preserve touch target sizing and gesture gates. Upstream already supplies selection repeat/queue, clipping, dictionary and the light panel. | K0 merged and pinned; no S1/D1 dependency |

Include the bounded settings descriptors with the menu work where needed; preserve saved values, order, hidden-option compatibility and web indices. Wrapped popup drawing and percentage formatting travel with an accepted caller that needs them. Avoid a standalone API or cleanup PR without an independent benefit. Assess the unrelated non-Silkscreen raw-contact change as its own fix; it cannot ride along in K0 or S1.

### Next: resolve the remaining software features before the device series

The owner wants all these features represented in the upstream plan first. Each gets a separate adoption decision and, if accepted, an implementation on the existing firmware before Silkscreen is stacked on top. Current Silkscreen guards are not evidence that a feature is already portable. Record accepted, declined or deferred outcomes; keep any unaccepted behavior as an explicit downstream remainder. An optional feature's rejection does not justify weakening device safety or silently dropping the feature from this inventory.

| Id | Concern | Work to separate before Silkscreen |
|---|---|---|
| W-L | Light shortcuts and warm/cool blending | Separate the app's Toggle Frontlight / long-Confirm shortcut and double-click ownership from the SDK's color-select blending mechanism. Preserve persisted values and existing shortcuts. Use existing light capabilities; the current blend code depends on carrier, load-state and rail safeguards. It needs a suitable existing hardware consumer or remains with its later hardware implementation. A generic light panel does not make that electrical backend portable. |
| W-B | Battery presentation | Propose averaging/interpolation, display steps and no-cell UI independently of raw cutoff measurements. Keep cell curves and charge detection with the hardware that supplies them. Agree behavior on existing devices; do not apply Silkscreen's voltage curve or no-cell inference to them. Deadband and load-aware display sampling may be reusable. The local 500 mAh TP4056/ladder estimator, learned USB/light offsets, NVS step mean and BATLOG charge trace remain Silkscreen-specific until another consumer justifies a shared contract. Include popup/text helpers only where needed. |
| W-P | Awake and background-work policy | Agree firmware ownership of ceiling, input cap, client activity, unattended-wake and storage-error timeouts. Reuse the existing sleep/power APIs, keep existing device defaults unless adopted explicitly, and use N1 where a blocking operation supports cancellation. Low-battery cutoff and safe sleep remain required device contracts. |
| W-W | Render/sleep-entry watchdog and retained logs | Review the progress/recovery design as its own feature before board support. Separate generic detection, hooks and retained evidence from board-specific USB sensing, lock-free light shutdown and early safe sleep. Agree HAL recovery contracts and task/core placement before enabling it on an existing device; the current implementation fixes core 0 and a 4 KB stack. Measure task/stack/RTC costs and legitimate progress bounds on the target MCU. The upstream SPI deadlock fix does not replace this defense. |
| T1 | Battery logging, bench firmware and diagnostic APIs | Decide whether any reusable logging or test mechanism merits a separate optional diagnostic contribution on existing devices. The present battery logger and bench UI are Silkscreen diagnostics, not new normal-reader features. Keep board-specific test cases, scan/calibration APIs and instrumentation out of generic reader PRs. Their acceptance is not a dependency for S1/D1. |

Do not invent a general policy framework just to prepare these topics. Move a cohesive policy block out of `main.cpp` only when the extraction needs it and existing mechanisms are insufficient. A feature for which existing boards lack the necessary hardware can have its contract reviewed first, but hardware validation must wait for a real consumer. Do not change another board's profile to pretend it has Silkscreen's controls or circuitry.

### Then: stack the hardware and device support

Once the applicable generic topics have landed and the remaining feature decisions are recorded, re-diff the fork against that new upstream baseline. The intended result is that S1/D1 consume accepted firmware behavior instead of reintroducing it in device hunks. A declined optional feature is an explicit downstream remainder, not an indefinite blocker for the board series.

| Id | Repo | Contents | Needs |
|---|---|---|---|
| S1 | SDK | 4.26" profile; ADC keys/shared lock and charge ladder; external-bias power key and pad parking; parked SDMMC; shared I2C/RTC; optional touch identity/hibernate/fallback; stable SSD1677/UC8279D probe/cache/BUSY/override integration; gated frontlight including monitor/load/rail handling; host/profile checks and a device page. Enable the already accepted K0 capability through board data. Reuse upstream drivers and detectors. | S0 and applicable generic SDK contracts merged; software-feature decisions recorded; maintainer agreement on SDK/HAL interfaces |
| D1 | CP | 4.26" env/release recipes, update tag, CI/device documentation, upstream-owned SDK pin and HAL integration. Supply charge/USB inference, ordered peripheral sleep, early battery/wake decisions, cutoff/update guards and failure-path clean sleep. Consume accepted K1 and software features. Preserve required light outcome/restore integration when light is enabled. | S1 merged and pinned; applicable generic app topics merged; agreed no-hardware-off behavior |

If maintainers request smaller hardware topics, split cohesive mechanisms with runnable profiles and explicit dependencies. A light-enabled intermediate profile must retain rail waits and required fault handling. Do not advertise a complete device before its required app sleep path exists. Production discovery/identity/sleep checks are not bench-only merely because the older map called them PROBES.

The supported SSD1677 HALF refresh choice belongs with S1/D1: the SDK owns the waveform switch, and CrossPoint owns the saved `standardHalfRefresh` setting and its 4.26" Silkscreen/SSD1677 availability. It defaults to Off, preserving the existing full-waveform HALF behavior. Users seeing excessive refresh flashing can enable Standard HALF Refresh and return to Off if clearing worsens. The owner reported a successful panel test on 2026-10-23; this is a per-panel choice, not a claim that all SSD1677 displays prefer it. Keep it out of the generic firmware feature topics. CrossInk exposes the same saved choice under Settings > Display; its application integration remains outside this upstream series.

Local sync/build tooling and research documents remain downstream. The 3.68" profile is a later device variant with its own fixed UC8279d/no-touch build and update tag. CrossInk application work remains outside this series. No fraction of future conflict reduction or binary savings is claimed before the extracted topics and matched images exist.

## 5. How a topic branch is cut

1. Use a separate worktree from the current upstream base: `git worktree add ../pr-<name> -b <type>/<desc> upstream/develop` (SDK:
   `fk-upstream/main`). Stack dependent work only on its stated prerequisites; independent topics start from upstream. For CrossPoint the SDK pointer is set last, to a merged SDK commit containing every already adopted SDK prerequisite. Do not regress earlier S0/K0/N1 adoption by pinning an older topic commit. K0/K1 and the generic software topics must not require S1/D1 or Silkscreen build flags.
2. Use the current audit boundaries, requirements and source first; use older maps to locate concerns. Apply shared-file hunks with context (never `--unidiff-zero`) and read them afterwards. Hand-separate mixed `main.cpp`, HAL, profile, frontlight and input changes. Record which existing behavior each omitted hunk no longer needs.
3. Omit battery-log/bench/diagnostics-only code from the device topics. Keep required profile assertions and capability gates until an equivalent reviewed contract replaces them. Do not mechanically remove probe/monitor gates or turn off safeguards. Generic UI uses capabilities; app policy remains a separately named concern. Keep source comments short and about the maintained contract.
4. Choose checks for the actual topic: existing-device flow tests for F1, selection transitions for F2/K1, actual light state for F3, cancellation/error paths for N1, and storage ownership/cache tests for S0. K0/K1 cover key-only, touch-only and combined capabilities, including absent optional touch; a synthetic capability test does not replace hardware evidence. Use the app's `./bin/clang-format-fix -g` (the SDK has no formatter), relevant host tests and flash/RAM deltas from `scripts/firmware_size_history.py`. Generic contributions need existing-device build/size evidence. Agree the build route before extraction: current CI runs on `master` pushes and PRs, so publishing a topic branch alone will not run it. Do not open a PR to bypass the pre-PR checks, or start unrequested cold/proxy builds. A suitable existing environment or an explicitly agreed CI route must satisfy the final-build requirement. S1/D1 also use the relevant SDK profile/ladder tests and warm Silkscreen build. Follow the shared build lock and existing-environment rules.
5. `firmware-handoff`: four read-only reviewers in parallel, one round and at most one follow-up, then the final build. The owner reads the diff and accepts
   the behaviour and the maintenance in their own words; that acceptance is the step upstream's rules cannot skip.
6. Hardware, by the owner with existing-device testers as needed, before a PR: validate generic topics on suitable existing devices first. K1 exercises the key-access list and touch transitions; F1 checks each flow's result/retry/upload radio state; S0 checks dirty-volume flush/remount and MSC handoff after writers close. W-L/W-B/W-P/W-W need their agreed behavior and recovery/resource checks. When the board series is ready, S1/D1 run the Silkscreen bring-up checks (keys, charge state, sleep/wake, SD, RTC, light, touch; test firmware T1-T3, T9, T11-T14, P3 and P5). Features with no existing hardware consumer record that limit and get device validation when the consumer exists.
7. The owner writes the PR text, answers the AI-usage question, pushes the branch to the fork and opens the PR.

## 6. Open points for the owner

- The feature-first order is the owner's direction. The exact number of PRs follows the concern boundaries and maintainer decisions; the old five-PR count is superseded.
- Maintainer adoption and interface decisions for the generic features, especially light blending, battery presentation, awake policy, watchdog recovery and optional diagnostics. No acceptance is assumed. Record outcomes before cutting S1/D1.
- Suitable existing-device testers and a working build/CI route for each generic topic, especially input combinations, light hardware and lock-independent watchdog recovery. A topic-branch push does not trigger the current CI; settle this before promising build evidence.
- The enabled frontlight belongs in S1/D1 with its complete required contract. If maintainers request a lightless first topic, use an explicitly light-disabled profile; do not silently strip monitoring/load/rail handling from a light-enabled one.
- The optional touch hardware tail belongs with S1; K0/K1 provide the generic keys+touch behavior first. Re-measure extraction size against the baseline after the generic topics merge.
- A private backup remote for the `silkscreen` branches, and the first push of a topic branch to a public fork: both need a yes.
- The detailed implementation Discussion is now published on the fork at the owner's request. Upstream maintainer coordination is still required before SDK/HAL/OTA/recovery PRs; a fork post alone is not an upstream acknowledgement. Device facts for that coordination: ESP32-S3-WROOM-1-N16R8; 4.26" 800x480 SSD1677 or UC8279D with stable cold-boot detection and deep-sleep cache; eight keys on two resistor ladders plus a power key on IO18 with an external bias; FT6336U touch; TPS923610 warm and cool light; SD card behind a PMOS
  gate with the pins parked in sleep; no fuel gauge, a 500 mAh cell read by ADC and an analog charge-status ladder; no hardware power-off; the PCB is public
  (iandchasse/silkscreen-pcb). One question for the maintainers: whether a community board without a purchase link fits the included-devices list.
- Local commits on topic branches still need explicit owner approval under upstream's rules. Agreement with this plan does not authorize commits, publication or PR creation.
