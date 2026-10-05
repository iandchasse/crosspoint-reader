# Silkscreen battery log

The `silkscreen-batlog` build is the Silkscreen release image plus a battery sleep/wake log: one CSV row per completed
sleep/wake cycle, appended to `/silkscreen-battery/battery.csv` on the SD card. Silkscreen has no fuel gauge, so the log
is the way to see where the charge goes without a meter on the cell: wake storms, awake tails that run on, ghost wakes,
abnormal resets and, over weeks of ordinary use, a trend in the sleep current. It does not replace the meter: it cannot
see the microamp floor ([What it can and cannot find](#6-what-it-can-and-cannot-find)).

This page is the operating manual: what the build is, how to run a campaign, how to read the file, what to send for
analysis and how to prove on a bench that the build adds no drain. It was written from the 2026-10-04 design review.
**Nothing here has run on a board yet.** Every timing and current below marked [est] is an estimate, and the sleep-current
claim is "expected zero, not measured" until the bench test M1 ([section 9](#9-bench-proof)) has run.

Both firmwares carry the same log through the same SDK code. CrossPoint's page is this one; CrossInk's differences are in
`crossink/docs/silkscreen/BATTERY-LOG.md`.

## 1. What the build is

- **Its own env.** `silkscreen-batlog` is a copy of the repo's release env plus `-DSILKSCREEN_BATLOG=1`:

  | | CrossPoint | CrossInk |
  |---|---|---|
  | Release env it copies | `silkscreen-gh_release` | `silkscreen` |
  | Version string | `<x.y.z>-batlog` | `<x.y.z>-silkscreen-batlog` |
  | Flash | `pio run -e silkscreen-batlog -t upload` | `pio run -e silkscreen-batlog -t upload` |
  | Back to the release image | `pio run -e silkscreen-gh_release -t upload` | `pio run -e silkscreen -t upload` |

  Nothing else is added to the release flags: no USB-serial wait (`CROSSPOINT_WAIT_FOR_USB_SERIAL`), no lower
  `LOG_LEVEL`, no debug flag. Boot timing and the ghost-wake path therefore match the release image, which is what the
  log is meant to observe. (The dev envs wait 250 ms for USB serial on every boot, ghost wakes included; the `# fw`
  line in the file says which kind of image wrote the rows.)
- **The screen keeps 1% steps.** The release image shows the battery percentage in 5% steps (truncated, 100 from 98
  up; [PORTING.md](PORTING.md)). This image leaves that out, so what its screen shows still matches the log: `e_ui`
  and `w_ui` are tenths of a percent read from the raw cache and from the first fresh burst, so the display step would
  never have coarsened them, but the analyzer derives the percent the screen showed from them (`ui_pct()`, rounded to
  the nearest percent), and that is only what a 1% screen shows. Apart from the log, it is the one place this image
  behaves differently from the release image.
- **No setting, no screen.** There is no toggle, settings entry, i18n string, Quick Action or reserve change. The log
  runs whenever the image has the macro, and "off" means flashing the release env.
- **Gated in code.** The macro comes only from this env's `build_flags` and is never defined in code. Every line of the
  feature (the SDK files, the app hooks, the writer) sits inside `#if SILKSCREEN_BATLOG`, so every other env compiles to
  what it did before. A build with the macro on a device other than Silkscreen, or in the hardware test image, stops at an
  `#error`. The one blocker the other way round is the release image's 5% display step: its helper and `static_assert`s
  above `HalPowerManager::getBatteryPercentage()`, and the return inside it, sit in
  `#if FREEINK_DEVICE_SILKSCREEN && !SILKSCREEN_BATLOG`, which this image compiles out. The device term is what leaves
  every other board's code as it was.
- **Swappable with the release image.** The board tag and the firmware device type are the release image's
  (`silkscreen`), so the SD card firmware update accepts either one in place of the other.
- **Where the code is:**

  | Part | Where |
  |---|---|
  | Pure rules: CRC-16, the 53-column CSV, burst arithmetic, the record's state machine | SDK `libs/hardware/PowerManager/include/BatteryLogCore.h`; compile-time test and state table in `test/host/` |
  | RTC record, ADC burst, entry tail, wake capture, session ledger | SDK `BatteryLog.h`, `src/BatteryLog.cpp`; one call at the head of `PowerManager::deepSleep()` |
  | SD writer | each app's `src/util/BatteryLogWriter.{h,cpp}` |
  | Hooks | each app's `src/main.cpp`, `HalPowerManager`, `HalGPIO`, and the clock-sync callback (CrossPoint `TrustedTime`, CrossInk `HalClock`) |
  | Analyzer and its tests | CrossPoint `scripts/silkscreen/battery_log_report.py`, `test_battery_log_report.py` |

### One cycle, in order

1. **Sleep trigger.** The app stages in RAM what it knows: how the session ended, USB and charge state, what the last
   render showed, the idle tail, light and radio state, a settings snapshot, the sleep timeout. Microseconds, no I/O.
2. **Head of `PowerManager::deepSleep()`** (after the sleep screen, the saves, Wi-Fi off, the panel asleep and the card
   off). **E:** one ADC burst on the battery divider (IO8), the RTC time stamp, the record sealed in `RTC_NOINIT`
   memory. About 5-9 ms [est].
3. **Deep sleep.** RTC memory is kept; nothing runs. A ghost wake that re-sleeps only adds to counters in the record.
4. **A verified wake.** After the wake decision and before the touch probe, **W:** the reset reason, the wake cause,
   the RTC time, `time()` if the clock was synced, and a second ADC burst (deep-sleep wakes only). About 5-9 ms [est].
5. **After the first paint**, once the render task has been seen busy and 2.5 s have passed since boot, the row is
   appended to the file and the card is closed. One SD attempt per wake, 20-60 ms [est]. If the session ends before the
   loop gets to it, a fallback append runs in the sleep entry, only with the radio off and no earlier bad attempt.

An ADC burst is 2 discarded conversions, 64 counted ones (mean, spread, range, zero count) and 16 raw ones, all under one
`freeink::adc::Lock` taken with a **20 ms bound**: if something else holds the lock, the burst is skipped and flagged and
the sleep goes on. Nothing the log adds between the sleep decision and `esp_deep_sleep_start` waits without a bound.

### What it costs

- **Awake charge:** about 0.3-1.3 µAh per cycle [est] (two bursts at 25-40 mA for 5-9 ms, one append at 25-60 mA for
  20-60 ms). At 20 cycles a day that is 6-26 µAh a day, 0.25-1.1 µA on average, 0.001-0.005 % of a 500 mAh cell a day.
- **Sleep current:** expected zero, not measured. No pin, pull, hold, timer, task, wake source, I2C or light call is
  added, and RTC memory is retained in sleep anyway. The one thing the code review cannot settle is how the S3's sleep code
  treats the SAR ADC block after a burst a few milliseconds before `esp_deep_sleep_start`. M1 settles it.
- **RTC memory:** the record is 184 B (a `static_assert` caps it at 192). Free RTC slow memory, read from `firmware.map`:
  CrossPoint 2048 B on `silkscreen-gh_release` before the log and 1864 B on `silkscreen-batlog`; CrossInk 1648 B on
  `silkscreen` and 1464 B on `silkscreen-batlog`.
- **Flash and RAM** (the final build matrix, 2026-10-04, in bytes; [PORTING.md](PORTING.md#upstream-merges) records every
  build):

  | Image | Release env | `silkscreen-batlog` | The log adds |
  |---|---|---|---|
  | CrossPoint flash / static RAM | 5795154 / 103904 (`silkscreen-gh_release`) | 5804178 / 104440 | 9024 / 536 |
  | CrossPoint `firmware.bin` | 5800272 | 5809296 | 9024 |
  | CrossInk flash / static RAM | 5613605 / 115536 (`silkscreen`) | 5622425 / 116080 | 8820 / 544 |
  | CrossInk `firmware.bin` | 5618800 | 5627616 | 8816 |

  About 9 kB of flash and half a kB of RAM, all in the batlog image: every other env is the size it was before the log, to
  the byte. The table is that matrix. The sync to upstream `50823ffa` (three keyboard and hint fixes, after the log) moved
  every CrossPoint env by upstream's own 18 to 68 B of flash and none of RAM; the log's footprint is now 9036 B of flash and
  536 B of RAM (`silkscreen-gh_release` 5795210 / 103904, `silkscreen-batlog` 5804246 / 104440), and
  [PORTING.md](PORTING.md#upstream-merges) has the sizes of every env. Since the 5% display step (2026-10-05) the
  release image carries about 28 B that this image leaves out (`silkscreen` 5838946 to 5838974 B), so the log's
  footprint over a release env built after it is about 28 B smaller than these figures; the batlog image itself did
  not change size.
- **OTA slot headroom** (the 6553600 B app slot less `firmware.bin`): CrossPoint `silkscreen` 709584 B,
  `silkscreen-gh_release` 753328 B, `silkscreen-batlog` 744304 B, `c3check` 754672 B; CrossInk `silkscreen` 934800 B,
  `silkscreen-batlog` 925984 B, `c3check` 224560 B. The batlog image fits with room in both readers. CrossInk's `c3check` is
  the tight one, and the log does not touch it.
- **The SD card:** one file, 2 MiB cap, about 230 B a row (about 9000 rows, about 15 months at 20 cycles a day). Eight
  weeks of use writes 0.15-0.2 MB.

## 2. Flashing, swapping and stopping

- **Flash** over USB with `pio run -e silkscreen-batlog -t upload` in `crosspoint-reader` or `crossink`. The version
  string (shown in Settings, and in the `# fw` lines of the file) ends in `-batlog` (CrossPoint) or `-silkscreen-batlog`
  (CrossInk).
- **Swap without USB.** Copy the `.bin` to the card and use Settings > System > SD Card Firmware Update: the image is
  `.pio/build/silkscreen-batlog/firmware.bin` (CrossInk also writes `firmware-silkscreen.bin` beside it). The update
  checks the board tag and the battery floor (it asks for USB power when the cell is too low to finish), not the version,
  so the swap works in both directions. Online OTA checks the same tag too (and compares only the leading `x.y.z`, which
  the suffix does not change), but the fork publishes no release asset, so there is nothing for it to fetch.
- **Stop logging** by flashing the release env. The file stays on the card; remove it with USB Drive if you want it gone.
- **A new image loses the record** (the build id is part of its check), so the first row after any flash is an orphan
  `k = B` row, preceded by a `# fw` line. Flash the batlog image once, at the start of a campaign, and do not reflash it
  in the middle: each flash starts a new segment.
- **Keep the release binary** from the same commit if you plan the bench A/B ([section 9](#9-bench-proof)).

## 3. Running a campaign

1. Flash the batlog image once ([section 2](#2-flashing-swapping-and-stopping)).
2. **Use the unit normally and switch nothing.** There is nothing to turn on. Read, use the light, join Wi-Fi, charge
   when you would. Note the days that were not normal ([section 8](#8-what-to-send-for-analysis)).
3. **Run it for weeks.** The counters (wake storms, tails, resets) answer from the first week. A sleep-current trend
   takes longer: at 500 mAh, ordinary use resolves about 1 mA within 30 days and about 500 µA after about 60 days
   ([section 6](#6-what-it-can-and-cannot-find)). Eight weeks is the recommended length.
4. **Do not disturb the record.** Removing the cell or a power loss erases it (the next row is an orphan and `row`
   restarts at 1); the EN button does the same. Do not edit the file in place. Swapping the card is fine: the writer
   recreates the folder and the header, and `row` carries on.
5. **Retrieve the file** every one to two weeks, or when asked ([section 4](#4-retrieving-the-file)).
6. **Stop** by flashing the release env.

**First-run check.** Before relying on the numbers, with the image freshly flashed: three real sleeps. `rtc_ms`
increases and `sleep_ms` matches the wall clock within a few percent. Two ghost wakes (tap the power button briefly; not
under the click-wake rule, `cfg` bit 1, where a tap boots fully) show `g_n` 2 and `rst` 8 on the next row. A silent
restart (open a Wi-Fi screen) leaves `boots` 2; one made within 2.5 s of a wake, before the row is written, leaves that
row's `sleep_ms` and `w_ui` as they were. A card removed for one wake and put back gives a cycle row with `path` `e`, empty
`cfg` and `tmo` (`fl_e` bit 9), and the next row shows `drop` 1 and a gap in `row`. On CrossPoint `utc` stays empty after a
power loss even though `time()` looks plausible, and fills from the row after the first Wi-Fi join (CrossInk: after a
KOReader sync). Ignore row 1: it is an orphan.

**Optional tests:**

- **Display noise floor** (the "83 to 82" question, [section 6](#the-83-to-82-question)): wake, wait for the page to paint,
  sleep, six times in ten minutes. A flip of the shown percent with no time elapsed proves display noise.
- **Bookend:** wake, let the page paint, sleep within 10 s, leave the unit untouched for three days at 80-90 %, wake.
  The analyzer's estimator C reads it. It shows that the cell fell and by how much, not whether the board or the cell
  caused it.

## 4. Retrieving the file

The file is `/silkscreen-battery/battery.csv`, in a visible folder on purpose (CrossInk's web and serial transfer block
dot-folders, and CrossPoint's web UI cannot delete them). Routes, keys only:

- **USB Drive:** Home, File Transfer, USB Drive (CrossInk's is on for the Silkscreen env,
  `CROSSINK_APP_CAP_USB_DRIVE=1`). The firmware syncs the card before the host takes it and ends with a restart to Home,
  which the record survives. USB is the power source during it, so the wake after it is a poor "rested" point; the rows
  carry the USB flags and the analyzer leaves those pairs out. The row of a wake that ends in USB Drive is written after
  the restart.
- **Web file manager:** Home, File Transfer, Join Network. `/silkscreen-battery/` is listed and downloadable.
- **Last resort:** take the card out (it sits behind the cell). Pulling the cell loses the record and writes an orphan row.

**Copy `battery.csv` off the card and work on the copy. Never edit it in place.** It is unquoted CSV with lowercase hex in
`fw`, `fl_e`, `fl_w`, `cfg` and `crc`. Excel and LibreOffice read a hex value of the form digits-e-digits as a number in
scientific notation (about 3 % of `crc` values and 1 % of `fw` values hit it): `12e4` becomes `120000`, and saving the file
back breaks that row's CRC for the analyzer. The `#` lines also show up as one-cell rows. To look at it by hand, import every
column as text (pandas: `dtype=str, comment='#'`) and never save it back. The analyzer reads the raw file, so give it the
copy as it came off the card.

**The cap.** At 2 MiB logging stops (the last row written carries `fl_w` bit 9) and the writer retries about once per 64
wakes. Move or delete the file with USB Drive to start again: the writer recreates it with a fresh header and the row
number continues. Several files can be merged by the analyzer.

## 5. Reading a row

### Framing

ASCII, comma-separated, LF-terminated, no quoting. The first line is the header (`v,row,k,...,crc`). **Every row and every
`#` line starts with its own LF**, so a torn final row can never glue onto the next. `crc` is the last column: CRC-16/CCITT-FALSE
(poly 0x1021, init 0xFFFF; Python `binascii.crc_hqx(data, 0xFFFF)`) over the row text from the first field through the last
comma, as four lowercase hex digits. A reader drops any line with the wrong column count or CRC. Columns are only ever
appended, never reordered; when they change, the header is written again before the next row.

Lines starting `#` carry no CRC: `# fw <build id> <app version> m<method id> w<boot wait ms> ser<0|1>`. It is written for a
new file, before an orphan row (a flash or a lost record) and after any lost row. `w250` marks an image that waits for USB
serial on every boot (a dev env). Never compare CrossPoint rows with CrossInk rows, nor a batlog image's rows with a dev
image's, without reading `fw` and `w_dt`.

Voltages are **P+** (the cell side of the divider) in mV with one decimal (the resolution of a mean, not its accuracy).
Times are ms unless stated. **An empty field means unknown; a counter is 0 only when it was measured as zero.**

### The columns

| # | Column | Meaning |
|---|---|---|
| 1 | `v` | Schema version (1). |
| 2 | `row` | Row number. Restarts at 1 after an RTC loss. A row that was lost still consumes its number, so a gap in `row` shows the loss. |
| 3 | `k` | `S` a completed cycle; `B` an orphan boot with no recorded entry (power-on, flash, a corrupt or lost record). An orphan row has only the wake half. |
| 4 | `fw` | The first 9 hex characters of this build's ELF hash. |
| 5 | `m` | Measurement-method id (1). Bumped whenever a burst or its placement changes; the analyzer drops rows of a minority method. |
| 6 | `rtc_ms` | RTC timer at the wake sample: the primary timeline. |
| 7 | `utc` | `time()` at the wake sample, **only if the clock was synced in this power-on domain** (CrossPoint: an SNTP sync at a Wi-Fi join; CrossInk: a KOReader sync); empty otherwise. CrossPoint's restored NVS floor is never written here. The analyzer back-fills wall time from it. |
| 8 | `sleep_ms` | Wake stamp minus entry stamp (includes the 0.1-0.3 s of ROM and bootloader). Empty if unknown. |
| 9 | `awake_ms` | The session: the entry stamp minus the previous wake stamp. Survives silent restarts. It runs to the head of the sleep tail, so it includes the sleep-screen refresh. |
| 10 | `path` | How the session ended: `m` user-ordered, `a` auto-sleep timeout, `k` awake ceiling, `i` input cap, `w` unattended wake (WK-02), `b` low-battery cutoff, `e` SD-error screen, `q` Quick Lock timeout (CrossInk), `x` unknown. |
| 11-13 | `e_mv`, `e_sd`, `e_rg` | Entry burst: mean, standard deviation and range (max minus min) of P+, mV. |
| 14-15 | `e_z`, `e_raw` | Zero conversions (the ADC's silent failure); the sum of the 16 raw conversions (calibration-independent). |
| 16 | `e_ui` | The cached battery value at the trigger, in 0.1 % (826 is 82.6 %): the value the screen's percentage is taken from, not the number drawn. This image's screen rounds it to 1% (the release image's shows 5% steps, but it writes no log). |
| 17-18 | `usb_e`, `chg_e` | USB present (0 or 1) and charge state at the trigger: `?` unknown, `b` on battery, `c` charging, `f` complete, `x` external idle, `n` no battery. USB reads unknown until the charge ladder has decoded. |
| 19 | `fl_e` | Entry flags, 4 hex (below). |
| 20 | `ent_ms` | Trigger to the SDK tail: the sleep-screen refresh, the saves, a plugin Wi-Fi join, the release wait. |
| 21-22 | `g_n`, `g_o` | Early re-sleeps inside this sleep: ghost wakes (early ghost, SD-fail ghost, late ghost) and boot-cutoff re-sleeps. Release wakes and reset-loop sleeps are **not** counted. |
| 23 | `g_ms` | Their summed `millis()` at the re-sleep decision. A lower bound: 0.1-0.3 s of ROM and bootloader per wake is not in it. |
| 24-25 | `g_mv`, `g_t` | P+ at the last early wake that took a sample (late ghost, SD-fail ghost, boot cutoff) and its time since the entry. Empty when there was none; an early ghost takes no sample. |
| 26-28 | `w_mv`, `w_sd`, `w_rg` | Wake burst, as `e_*`. A partly settled reading, not a rested one. |
| 29-30 | `w_z`, `w_raw` | As `e_z`, `e_raw`. |
| 31 | `w_ui` | The first battery percentage the UI computed after the wake, in 0.1 % (one fresh burst under render load), latched once per row. |
| 32 | `w_dt` | `millis()` at the wake sample: a proxy for how much of the boot-load step the sample has seen. Excludes the 0.1-0.3 s before `millis()` starts. |
| 33-34 | `usb_w`, `chg_w` | As the entry pair, read when the row is written (the ladder decodes 1.5-9.5 s after boot). |
| 35 | `fl_w` | Wake flags, 4 hex (below). |
| 36 | `rst` | `esp_reset_reason_t` of the wake: 8 deep sleep, 1 power-on, 3 software, 4 panic, 5-7 watchdogs, 9 brown-out, 11 USB, 12 JTAG. |
| 37 | `wk` | `h` the power key was held through the wake verify, `c` accepted with no hold under the click-wake rule, `o` not a deep-sleep power-key wake. |
| 38-39 | `ab`, `ab_r` | Abnormal resets (panic, watchdog, brown-out, lock-up) since the last row that reached the card, and the last reason. |
| 40 | `cfg` | Settings snapshot, 2 hex: bit 0 Restore Light on Wake, 1 the click-wake rule (ghost counts then mean something else), 2 touch fitted, 3 front light present, 4 Fading Fix. Empty when the settings were not loaded (`fl_e` bit 9). |
| 41 | `tmo` | Sleep timeout setting, minutes (0 is Never). Empty as `cfg`. |
| 42 | `idle_ms` | The idle tail at the trigger: the time since the last user input. |
| 43 | `boots` | Boot segments in the session (a silent restart adds one). |
| 44-45 | `lit_ms`, `lit_pct` | Front light on time and its time-weighted brightness, in percent (not duty: the analyzer applies the light's gamma curve). |
| 46 | `wifi_ms` | Radio on time. |
| 47 | `full_ms` | Time at the full CPU clock (the rest was 80 MHz). |
| 48 | `inp` | Loop passes with user input. |
| 49 | `rf` | Display refreshes. Not hooked in this build: always empty. |
| 50 | `usb_ms` | Time with USB present. |
| 51 | `sdw_ms` | Duration of the previous row's SD window. |
| 52 | `drop` | Rows consumed without reaching the card since the last good one. |
| 53 | `crc` | Row CRC. |

**`fl_e`** (bits, append only): 0 light lit at the trigger; 1 radio on at the trigger; 2 the touch controller did not
accept hibernate (touch fitted: the +165 µA Monitor-mode case); 3 I2C SDA still low after bus recovery (about 1.5 mA);
4 the power key was still held after the release wait, so a **release wake** happened inside this sleep; 5 the entry
burst had no valid conversion; 6 no battery ADC pin; 7 the session ledger is partial (a restart without carry, a panic, or
logging began mid-session); 8 the entry burst was skipped, the ADC lock was not obtained within 20 ms; 9 the settings
were not loaded at the trigger (the SD-error boot): `cfg` and `tmo` are empty.

**`fl_w`** (bits, append only): 0 the record was lost or new (`k = B`); 1 the RTC counter went backwards (`sleep_ms`
unknown); 2 the reset was not a deep-sleep wake (a flash or power-on, an aborted sleep entry, a crash inside it); 3 the
charge ladder was not decoded when the row was written; 4 the wake burst was not taken or had no valid conversion; 5 the
wake cause was not the power-key line; 6 the previous flush failed or took over 250 ms; 7 the wake burst was skipped, lock
timeout; 8 the SD back-off skipped flushes since the previous row (`drop` says how many); 9 the file reached its 2 MiB cap
with this row.

### An example row

Synthetic: CrossPoint after an NTP sync, a 25.6 min session ended by the 10-minute auto-sleep, a 9.0 h sleep with two early
ghost wakes. The SDK's compile-time test reproduces this exact string, CRC included.

```
v,row,k,fw,m,rtc_ms,utc,sleep_ms,awake_ms,path,e_mv,e_sd,e_rg,e_z,e_raw,e_ui,usb_e,chg_e,fl_e,ent_ms,g_n,g_o,g_ms,g_mv,g_t,w_mv,w_sd,w_rg,w_z,w_raw,w_ui,w_dt,usb_w,chg_w,fl_w,rst,wk,ab,ab_r,cfg,tmo,idle_ms,boots,lit_ms,lit_pct,wifi_ms,full_ms,inp,rf,usb_ms,sdw_ms,drop,crc
1,57,S,f3a9c01d2,1,412345678,1791234567,32412345,1536000,a,3997.0,5.2,24,0,36784,826,0,b,0000,2310,2,0,640,,,4006.5,4.7,22,0,36871,818,312,0,b,0000,8,h,0,,0d,10,600000,1,720000,40,0,410000,118,,0,41,0,c59d
```

The screen showed 83 at the trigger (`e_ui` 826) and 82 after the wake (`w_ui` 818), while the wake burst, 4006.5 mV, reads
83.3 % by the battery curve's notch table and was 9.5 mV above the loaded entry burst: the same cell, 83.3 % by the wake
sample and 81.8 % under the first paint's load. Ten minutes of that session were the idle tail and the light was on for
twelve. An orphan row (after a battery swap or a flash) has the wake half and the counters only.

### How ghost and early wakes appear

They never produce a row. Each hooked early exit adds to the counters of the pending sleep, and the row is written by the next
verified wake that stays up.

| Early exit | Folded as | Sample |
|---|---|---|
| Early ghost (the power key not held through the wake verify) | `g_n` + 1 | none, by design |
| Boot cutoff (battery below the cutoff at boot) | `g_o` + 1 | the wake sample, as `g_mv`, `g_t` |
| SD-fail ghost, late ghost (CrossPoint only) | `g_n` + 1 | the wake sample |
| Release wake (the key was held at sleep entry and its release wakes the board) | **not recorded**; `fl_e` bit 4 on the entry row says it happened | none |
| Reset-loop sleep (3 abnormal resets in a row) | **not recorded**; `ab` on the later row counts the crashes | none |
| Power-pin fault halt | not a re-sleep: it sleeps with no wake source, and the cycle is not recorded | none |

The release-wake and reset-loop exits call **no battery-log hook**: they run before the wake decision, where nothing new
may run (POWER.md WK-01), and the only log code they reach is the compare at the head of the sleep tail, which returns at
once because nothing is staged. A night with five pocket presses is one row with `g_n` 5. Under the click-wake rule (`cfg`
bit 1, `wk` `c`) every tap boots fully, so ghost counts are structurally zero.

### Things that look odd and are not

| You see | Meaning |
|---|---|
| `k = B` rows, `row` back at 1 | The record was lost: a flash, a power loss, the cell pulled, the EN button. `rst` tells flash from power loss. |
| A gap in `row`, `drop` above 0 | A row did not reach the card (SD trouble, the 2 MiB cap, a sleep that began with a row still pending). |
| Empty `utc` | No clock sync in this power-on domain yet; the analyzer back-fills from a later row. |
| Empty `cfg` and `tmo`, `fl_e` bit 9 | The SD-error boot: the settings were not loaded. |
| `rst` other than 8, `fl_w` bit 2, no `sleep_ms` | Not a deep-sleep wake: a flash or power-on (an orphan row), or an aborted sleep entry or a crash inside it (`rst` 3 or an abnormal reason). |
| `boots` above 1 | A silent restart (a low-heap restart, a network screen's restart) inside the session. |
| No row for the wake that ended in USB Drive | The host owns the card during it; the pending row is written after the restart that ends the session. |
| `fl_w` bits 6 and 8, `drop` above 0 | The SD back-off: two bad attempts in a row skip the next 63 wakes, then one probe. |

**Clocks.** `rtc_ms` and `sleep_ms` come from the RTC counter, which runs off the internal RC slow clock (about 136 kHz,
calibrated against the crystal at every boot and sleep entry; there is no 32 kHz crystal). A few percent of drift [est]
is fine for rates and ordering and not for a wall clock: 3 % of 9 h is about 16 minutes. The analyzer measures the real
drift from the step of `utc - rtc_ms/1000` between two synced rows.

## 6. What it can and cannot find

### The measurement chain

- IO8 reads P+/2 through 1 MΩ / 1 MΩ with a 1 µF capacitor: a 500 kΩ source, tau = 0.5 s. A load change on P+ reaches the
  pin low-passed: about 26 % of a step after 0.15 s, 45 % after 0.3 s, 75 % after 0.7 s.
- P+ sits behind 0.24-0.35 Ω of protection FETs, so it reads 12-14 mV low per 50 mA: about 7-14 mV at the idle 25-40 mA,
  about 30 mV with the light on.
- **E is a loaded reading.** It is taken within milliseconds of the 240 to 80 MHz switch, so the node still holds most of the
  render load: E reads about 11-25 mV low. **W is a partly settled reading**: taken 0.15-0.4 s after `millis()` starts on
  CrossPoint (0.4-0.7 s on CrossInk), it has seen 26-55 % of the boot-load step (55-75 % on CrossInk) and reads about
  2.6-15 mV low. Neither is a rested volt. The bias of W is nearly constant per path and per firmware, so it cancels in a
  pair of wake samples; `w_dt` tells how far the node had moved.
- The displayed percent is an average of the last 10-20 render-time reads, load-sagged and rounded; the row stores the
  stateless volts and, separately, what the screen showed.
- **So the analyzer uses wake-to-wake pairs only.** The loaded entry to wake step (`w_mv - e_mv`) is never a drain: a
  synthetic study gave the same +7.9 to +8.6 mV/h for every true drain from 58 µA to 1 mA.

### The "83 to 82" question

(That night was read on a screen with 1% steps, which this image keeps. On the release image the two screens would
show 80 and 80, and a one-percent fall shows only when it crosses a multiple of 5.)

A one-step drop in the displayed percent over a night is what a normal night produces on this cell; it is neither evidence of
a drain nor evidence against one. At 500 mAh one percent is 5 mAh and 8 mV at 83 %. Nine hours at the 111-127 µA floor is
1.0-1.14 mAh (0.20-0.23 %, 1.6-1.8 mV). The 10-minute awake tail before sleep is 4-6.5 mAh, 3.5-6.5 times larger. The two
displayed numbers are not the same kind of reading (a smoothed value against one fresh burst). And a front light on for one
of the two screens moves the display by 2-3 steps by itself. Exactly one step shows on about 57-61 % of such nights with
nothing wrong; a real 0.56 mA fault on top of the tail would mostly show two steps or more. The log answers it from the
row: `e_ui` and `w_ui` (what the screen showed), `e_mv` and `w_mv` (the volts), `idle_ms`, `path`, `lit_ms` and `wifi_ms`.

### Resolution (500 mAh cell)

Estimates, to be replaced by the real scatter once there are rows: a wake-pair sigma of 2.5 mV, an entry-to-wake sigma of
7 mV. One displayed percent is 8 mV in the 80-90 % band and 2.67 mV on the 20-50 % plateau. At 1000 mAh every current
below doubles.

**The smallest total fall that stands out from zero** (3 sigma: it detects a signal of exactly that size half the time;
95 % power needs 1.55 times these):

| Band | Stretch | Wake to wake (bookend) | Entry to wake, one sleep (E is loaded) |
|---|---|---|---|
| 80-90 % | 9 h | 0.52 mA | 1.46 mA |
| 80-90 % | 3 days | 65 µA | 182 µA |
| 80-90 % | 14 days | 14 µA | 39 µA |
| 20-50 % | 9 h | 1.56 mA | 4.4 mA |
| 20-50 % | 3 days | 195 µA | 547 µA |
| 20-50 % | 14 days | 42 µA | 117 µA |

This says the cell fell, not that it fell more than the floor: the 14-day rows lie below the unit's own 111-113 µA floor
and 14-21 µA of self-discharge, so they cannot attribute anything. Ordinary use has no uninterrupted 3- or 14-day sleep
(those rows are a holiday or a deliberate bookend).

**The smallest excess over the expected floor** (the question that matters). The expected total fall current is about
130 µA (113 µA plus 14-21 µA of self-discharge, at 2-3 % a month). The conversion from volts to charge uses the generic
notch table, whose error against this cell is taken as 30-50 %, and that scale error dominates after about 3 days:

| Stretch | Scale error 30 % | Scale error 50 % |
|---|---|---|
| 3 days | 134 µA | 206 µA |
| 14 days | 118 µA | 196 µA |

So one clean bookend can show an excess of about 130-210 µA, and waiting longer does not help. One meter reading (MEAS-01)
calibrates the scale term away; until then anything below about 120 µA of excess is invisible to the log.

**Ordinary use for 30-90 days** (regression over the whole log; a synthetic study with 2 reading sessions and 4 glances a
day, sleeps under 2 h excluded, no self-discharge, so add 14-21 µA to the expected estimate). Median estimate in µA, median
95 % half-width, and in how many of 24 simulated datasets the lower bound cleared zero:

| True sleep | 30 days | 60 days | 90 days |
|---|---|---|---|
| 113 µA | 95, ±248, 2/24 | 86, ±180, 4/24 | 51, ±140, 3/24 |
| 250 µA | 207, ±254, 6/24 | 213, ±245, 7/24 | 236, ±212, 12/24 |
| 500 µA | 463, ±367, 16/24 | 450, ±273, 20/24 | 465, ±245, 22/24 |
| 1 mA | 1010, ±427, 22/24 | 899, ±291, 24/24 | 912, ±247, 24/24 |

At 500 mAh ordinary use resolves 1 mA within 30 days and about 500 µA by 60 days; 250 µA only about half the time at 90
days; 113 µA never (the medians for 250 µA and below are biased low). HW-08's clamp (about 500 µA) and half of the SD-gate
range (200-2000 µA) sit in that range: expect their trend to show after about 60 days.

### What finds what

| Candidate (POWER.md) | Size | The log |
|---|---|---|
| Wake storm (IO18 bounce, held key), pocket press with Restore Light | 2-3 µAh per ghost; 1.3-5 mAh per relit pocket wake | **Finds it directly:** `g_n`, `g_ms`, `path` `w` with `lit_ms` |
| Long awake tails, ceiling and input-cap sleeps | 4-6.5 mAh per 10 min | **Finds it:** `awake_ms`, `idle_ms`, `path`, `full_ms`, `lit_ms`, `wifi_ms` |
| Slow sleep entry (plugin Wi-Fi join, touch retry) | 0.3-1 mAh | **Finds it:** `ent_ms` (and `fl_e` bit 1) |
| Touch left in Monitor mode | +165 µA | **Names it** by `fl_e` bit 2; voltage alone cannot |
| I2C SDA stuck low in sleep | about 1.5 mA | **Names it** by `fl_e` bit 3 |
| mA-class sleep fault (SD gate or bus left on, TP_RST low) | 0.2-2 mA | Finds the **trend** (days on a bookend, 30-60 days of ordinary use); does not name the pin |
| HW-08 clamp ("charging" on the cell) | about 500 µA | **Raises suspicion** (`usb_ms` close to `awake_ms`, `chg_*` `c`/`f` with no cable); the meter confirms |
| Cutoff and LDO dropout | +165-235 µA below 3.35 V | Sees the condition (`w_mv` below 3350), not the current |
| Brown-out, panic, watchdog | n/a | **Finds it:** `rst`, `ab`, `ab_r`, `fl_e` bit 7 |
| Power loss, flash, cell pulled | n/a | **Finds it:** `k = B` rows, `fl_w` bit 0, `row` back at 1 |
| HW-01 flash/PSRAM back-feed | 100-300 µA | **No.** Meter, J2 flex in/out, R5 test |
| IO8/IO9 mid-rail pad state, SD gate left on, panel not in deep sleep, U3 ground current | tens to hundreds of µA | **No.** A voltage log cannot see them |
| 58 vs 113 vs 150 µA hypotheses; self-discharge | tens of µA | **No.** Below voltage resolution, and indistinguishable from the cell's own self-discharge |

**The blunt limit:** the log cannot tell 58 µA from 113 µA on any practical timescale, nor board drain from the cell's
self-discharge, nor see an excess below about 120-200 µA until one meter run calibrates the scale term. A microammeter or a
PPK2-class logger on J5 is the only way to see the 58-130 µA class ([section 10](#10-measuring-the-sleep-current-with-a-dmm)),
and the log is not a substitute for it.

## 7. The analyzer

`scripts/silkscreen/battery_log_report.py` (Python 3, standard library only) does the same thing every time:

```bash
python scripts/silkscreen/battery_log_report.py battery.csv [more.csv ...] [--capacity-mah 500] [--out DIR]
```

- `--capacity-mah` defaults to **500** (the documented cell); `0` reports mV per day at the band instead of microamps.
- `--out DIR` also writes `battery_cycles.csv` (every cycle with its exclusion reason) and `battery_report.txt`.
- `--rows N` sets how many cycle rows to print (0 for all); `--resamples`, `--seed`, `--pair-sigma-mv`, `--early-ms-limit`,
  `--charge-hold-h` and `--sdk-dir` are for re-deriving the placeholders once there are rows.
- Several files are merged (for example one moved aside at the size cap). Exit status 0 with a report, 1 when no valid
  row was found, 2 on bad arguments. Run it on a copy ([section 4](#4-retrieving-the-file)).

The report has ten sections: file health (good, bad-CRC, torn, glued and duplicate lines), segments and the timeline,
the cycle summary and the last rows, display versus volts, burst quality, counter flags, exclusions, the drain estimates,
runs and flags by voltage, and a plain-language "what to do next". The drain estimates:

- **A, pooled wake-pair regression** (the trend detector): the charge consumed between consecutive eligible wake samples,
  regressed non-negatively on sleep hours, full-clock hours, low-clock hours, front-light hours at the duty curve, radio
  hours, a per-cycle constant and the early-wake count. The sleep coefficient is the estimate; the band is a bootstrap over
  days. It needs 12 eligible cycles, and gives no band over fewer than 7 days of data.
- **B, wake to wake inside one sleep**, when an early wake took a sample 2 h or more after the entry. Rare.
- **C, bookend and overnight pairs:** a sleep of 6 h or more after a short session. Only a **ceiling**: the session's own
  boot, paint and tail are inside the pair.
- **D, loaded entry to wake step:** a diagnostic, never a drain.

Each estimate carries a 95 % band and **the smallest current it could have resolved**, against zero and against the
expected floor of about 130 µA (the second limit includes the notch table's scale error). "Not resolved" is a result:
it says what the data could not have seen. Exclusion codes (`USB`, `CHG_UNSEEN`, `CHG_TAIL`, `SHORT`, `BOOT`, `RTC`, `METHOD`,
`HIGHSOC` at 4.06 V and above, `CUTOFF`, `GAP`, `LATE`, `PARTIAL`) say why a cycle did not form a pair. Counter flags need no
voltage inference: wake storms, ghost rate, long tails, ceiling and input-cap sleeps, pocket relights, abnormal resets, touch
and I2C flags, skipped bursts, SD trouble, and the HW-08 signature. A voltage flag of "RUN MEAS-01" appears when an
estimate's lower bound is above 150 µA.

The analyzer carries copies of the column table, the flag bits and the battery curve's notch table. When the SDK checkout is
next to it, it compares them with `BatteryLogCore.h` and `BatteryMonitor.cpp`; its tests do the same. Tests:

```bash
cd scripts/silkscreen && python -m unittest test_battery_log_report
```

## 8. What to send for analysis

- **The file:** `battery.csv`, a copy straight off the card, unedited. If a file was moved aside at the cap, send it too.
- **The cell:** the capacity (500 mAh is assumed; confirm it against the cell actually fitted, or send the part number), and
  how old it is.
- **The charges:** when, for how long, and whether the unit was on USB while awake. The analyzer holds back 12 h after a
  charge it can see; a charge it cannot see (a cable plugged and pulled inside one sleep) is caught by its "charged unseen"
  rule, and your note makes that certain.
- **The odd days:** light left on or used a lot, a long Wi-Fi or plugin session, a pocket press, a flash, a reset or a crash,
  the card removed, the cell pulled, a week away.
- **Which image:** the `# fw` lines in the file say it; add the reader (CrossPoint or CrossInk) if it changed.
- **A meter reading**, if you have one (MEAS-01 on a spare board, [section 10](#10-measuring-the-sleep-current-with-a-dmm)): it
  anchors the scale term and lets the excess limit fall from about 130-210 µA toward the noise floor.

## 9. Bench proof

The code review argues that the build adds no sleep current; only the board can show it. The comparison is **R**, the release
image (`silkscreen-gh_release` for CrossPoint, `silkscreen` for CrossInk), against **L**, the batlog image, on the same board and
the same cell. The release env at the same commit is the right R: every addition is inside `#if SILKSCREEN_BATLOG`.

**Set-up** (as MEAS-01, [POWER.md §4](POWER.md#4-measurement-and-bench-checks)): the cell or a 3.8 V source on J5 through a
µA meter or a PPK2-class logger ([section 10](#10-measuring-the-sleep-current-with-a-dmm)), USB and UART adapters off, card
fitted, light off, J6 empty. Wait 60 s after sleep entry, then average over 60 s. Same charge and temperature for paired
tests. Do not use a dev env: it waits 250 ms on every boot. Expected floor on this unit is about 110-127 µA (no DS3231 on it,
so the 30-minute trace should be flat: periodic structure is a finding). A bridged DMM is enough for the sleep floors (M1)
and the pin levels (M5); M2 to M4 need a logger that records the awake transients, such as a PPK2-class instrument.

| # | Measure | Compare | Pass |
|---|---|---|---|
| M1 | The sleep floor after each path: menu sleep, auto-sleep, WK-02 (3 min after a wake with no input), the 15-min ceiling, the cutoff, a ghost wake, a release wake (button taped down), the SD-error screen | R and L, three repeats | Within about 2 µA of R on every path; ghost and release equal R to noise. **This is the zero-drain proof**: it is the only test of the entry burst's sleep neutrality, and nothing below replaces it |
| M2 | Charge per cycle from the sleep trigger to floor + 5 µA, and its duration, **paired**: a scripted cycle (wake by key, 12 s on the first screen, sleep by key) in blocks of 5 cycles, alternating R and L (a reflash between blocks), at least 4 blocks per arm (20 cycles each) | L minus R: the mean difference with its 95 % interval | Expectation 0.2-1.3 µAh per cycle [est]. Gate: the mean under 5 µAh (at 20 cycles a day, 100 µAh a day or about 4 µA on average, 0.02 % of a 500 mAh cell). The estimate is *confirmed* only if the interval excludes 5 µAh and its upper edge is under 2 µAh; the meter's repeatability is reported with the result and decides which of the two the test supports |
| M3 | Ghost wake awake time and charge, with and without a card; a boot-cutoff wake (a bench supply at 3.40-3.45 V on J5, key wake: the wake burst, the 16-sample quick read and the cutoff samples) | R and L of the same firmware | CrossPoint: a tap awake at most 0.3 s (POWER.md §4). CrossInk: its own R baseline, about 0.4 s, because its wake verify holds until `millis()` reaches 400. L equals R within noise on the early ghost (no burst, RTC stores only); the cutoff path shows the wake burst, +5-9 ms [est] |
| M4 | A verified wake: time to the first paint and the charge to it | R and L | The wake burst adds +5-9 ms and 0.06-0.11 µAh [est]. Gate: +25 ms and +0.3 µAh [proposed]. No SD access before the first paint, so nothing else is expected |
| M5 | Pin levels after a sleep that wrote a row, and after the first row ever (the file is created): the list under "Pin levels in sleep" in POWER.md §4 (SD_VDD below 0.5 V, IO10 3.3 V, IO5-7 and IO15-17 0 V, IO40 and IO42 0 V, IO47, IO14, IO11, IO38, IO39 3.3 V); IO8 about P+/2 | Before and after | Unchanged |
| M6 | *Dropped.* A 24 h soak cannot resolve its own pass level: 10 µAh is 0.015-0.023 % of a 43-68 mAh day, and M1's repeatability of ±2 µA is 48 µAh a day. M2 replaces it | | |
| M7 | The release image leaves the card alone (what "off" means now). (a) With serial attached on L (USB present: not a current test), each verified wake prints one `BATLOG wake rst=8 state=3 row=... drop=... bad=... skip=... now=...` line and each append a `BATLOG Row written in N ms` line; a ghost chain prints neither. (b) On R, the card's file list and timestamps (read on a PC) are unchanged after five sleeps | R and L | (a) The lines appear as described, and `state` is 3 for a normal wake; (b) nothing under `/silkscreen-battery` changes under R. No claim about the SD clock at entry: it follows from the code and from (b) |
| M8 | Failure injection on L: pull the card; corrupt the file system; a very slow card; and a throw-away build (not committed) that holds the ADC lock for 100 ms across E and across W | L | The second bad attempt sets the back-off (`bad=2`, then `skip=63` in the wake line); the next wakes touch no SD; one probe is made after 63 wakes (use a build with the skip count reduced for the bench); the floor is unchanged afterwards. With the lock held, both bursts are skipped, `fl_e` bit 8 and `fl_w` bit 7 are set, the wake takes at most 20 ms more and the sleep proceeds |

Also run the sleep-wake guard (`scripts/check_sleep_wake_sources.sh`; CrossInk `scripts/check_sleep_wake_sources.py`), the
CrossInk drain checklist, and a grep of the new files for `esp_timer`, `xTaskCreate`, `pinMode`, `gpio_`,
`temperatureRead` and `esp_sleep` (only the read-only `esp_sleep_get_wakeup_cause` may appear).

## 10. Measuring the sleep current with a DMM

The log cannot see the microamp floor. A digital multimeter in series on J5 can, on a **spare board**, and it needs no
firmware change. This is MEAS-01 ([POWER.md §4](POWER.md#4-measurement-and-bench-checks)): expect about 58-73 µA without a
touch tail and about 113-130 µA with an FT6336U hibernated; above about 150 µA, A/B one change at a time. Measure R and L
the same way to compare them (M1).

**Recipe**

1. Use a spare board, not the one collecting log data. Flash the image to be measured. Take USB and any UART adapter off,
   fit a card, leave the light off and J6 empty.
2. Put the meter in series with the cell on J5 and **bridge the meter**: a short jumper, or better a switch, across the
   meter's terminals (the break in the battery lead), with the meter on a mA range.
3. Power the board and put it to sleep with the bridge closed.
4. Wait at least 60 s after sleep entry. Switch to a µA range while the bridge is still closed (a rotary switch breaks the
   circuit as it turns), then **open the bridge** and average the reading over at least 60 s.
5. **Close the bridge again before anything can wake the board:** a button press, a tap, a release wake or a ghost wake.

**Cautions**

- **The burden voltage can brown the board out at wake.** A µA range puts a shunt of 100 Ω to 1 kΩ in the supply path
  (typically 0.1-1 mV per µA: check the meter's manual). That is tens of millivolts at the sleep floor and far too much at a
  boot or wake, which draws tens of milliamps (40 mA through 100 Ω is 4 V): the supply sags below the brown-out level, the
  board resets mid-boot, and a meter without protection on that range can blow its fuse. Keep the bridge closed from
  power-on until the board is asleep, and close it again before any wake. A brown-out in the middle of an SD write can
  corrupt the card, so use a spare one.
- **Do not use the instrumented board to gather log data.** Its battery readings include the meter's burden voltage: P+ then
  sits below the cell's voltage by the awake current times the burden, which is different at E, at W and under each load, so
  `e_mv`, `w_mv`, the displayed percent and the low-battery cutoff are all wrong on it. A bench supply or a PPK2 in source
  mode has the same defect for another reason: P+ is then the supply, not the cell. Flash the batlog image on the metered
  board only for the bench tests of [section 9](#9-bench-proof), and discard any `battery.csv` it wrote.
- Check the meter's rating and fuse for the µA and mA inputs before connecting; do not exceed them.
- A reading from the spare board stands in for the logged board's floor only as far as the two are alike (the same touch
  tail, panel and card): say in the notes which board it came from.

## 11. Rules the log keeps

The drain rules are POWER.md **BL-01** (and OPT-01 for the RTC memory); CrossInk's is item 11 of its drain checklist. In short:

- No pin call, pull, hold, timer, task, wake source, I2C, Wi-Fi, NTP, light call or temperature read is added, and the front
  light is never lit or probed.
- The release-wake and reset-loop exits call no battery-log hook, so WK-01's "nothing new before the early decision" holds;
  the early-ghost exit stores into the RTC record only (one counter fold and one CRC rewrite).
- Every wait the log adds is bounded: the ADC lock with 20 ms, and nothing else waits before `esp_deep_sleep_start`.
- One SD attempt per verified wake, after the first paint, with the back-off kept in RTC memory; never after
  `Storage.prepareForDeepSleep()`, never a file left open, no NVS or flash write.
- After each upstream sync, the SDK's BatteryLog files and the apps' hooks are re-checked
  ([UPSTREAM-SYNC.md](UPSTREAM-SYNC.md)).
