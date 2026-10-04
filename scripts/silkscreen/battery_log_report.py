#!/usr/bin/env python3
"""Report on the battery.csv of a Silkscreen silkscreen-batlog image.

    python scripts/silkscreen/battery_log_report.py battery.csv [more.csv ...]
        [--capacity-mah 500] [--out DIR] [--rows 30] [--resamples 2000]

The log is /silkscreen-battery/battery.csv on the SD card: a schema line, "# fw" comment lines and one row per
completed sleep/wake cycle, each row framed by a leading LF and closed by a CRC-16 (design: BatteryLogCore.h in
freeink-sdk/libs/hardware/PowerManager/include). This script is stdlib only and does the same thing every time:

  1. Validates: splits header blocks, skips "#" lines, checks the column count and the CRC-16/CCITT-FALSE of every row,
     counts good, bad-CRC, torn and glued lines, recovers rows that a lost LF glued to a torn one, merges several
     files (a file moved aside at the 2 MiB cap) and drops exact duplicates.
  2. Segments the rows at every orphan boot (k = B), backwards rtc_ms or restarted row number; reports row gaps.
  3. Builds the timeline: wall time is back-filled from the rows that carry a utc (written only after a sync, so
     utc - rtc_ms/1000 is constant between two syncs) and the step of that offset between syncs is the RC clock drift.
  4. Prints a per-cycle table (and writes battery_cycles.csv with --out): sleep and awake length, how the session
     ended, how the unit woke, light/radio/full-clock time, USB, the wake-to-wake charge, an exclusion reason per row.
  5. Compares what the screen showed with the volts (the 83-to-82 question) and measures the display noise floor.
  6. Checks the quality of the ADC bursts.
  7. Estimates the sleep drain from wake-to-wake pairs (A: pooled non-negative regression with a day-block bootstrap,
     B: two wake samples inside one sleep, C: overnight pairs after a short session, which is only a ceiling because
     the session's own cost is inside the pair), each with a 95 % band and the smallest current it could have
     resolved, against zero and against the expected floor (not C). The loaded entry to wake
     sample step is shown as a diagnostic (D) with the smallest current an entry-to-wake pair could resolve, and never
     as a drain estimate.
  8. Counts what needs no voltage inference (wake storms, long tails, abnormal resets, pocket relights, ghost wakes).

What a voltage log cannot do, and this report does not pretend to: it cannot tell 58 uA from 113 uA of sleep current,
nor board drain from the cell's own self-discharge (2-3 % a month is 14-21 uA at 500 mAh), and it cannot see pad
states, back-feed or a release wake. The estimates carry the notch table's scale error (the generic 10 %-per-notch
curve is not this cell's curve). Only a microammeter in series on J5 sees the floor itself (MEAS-01, POWER.md).
The loaded entry reading (e_mv) is never used as a drain input: it sits under the render load and relaxes upward.

No third-party modules are needed. Exit status: 0 with a report, 1 when no valid row was found, 2 on bad arguments.
"""

from __future__ import annotations

import argparse
import binascii
import csv
import datetime
import math
import os
import random
import re
import statistics
import sys
from collections import Counter
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

# --- The schema ---------------------------------------------------------------------------------------------------
# The column table of BatteryLogCore.h, in order. test_battery_log_report.py compares it with that header's text.
COLUMNS: Tuple[str, ...] = (
    "v", "row", "k", "fw", "m", "rtc_ms", "utc", "sleep_ms", "awake_ms", "path",
    "e_mv", "e_sd", "e_rg", "e_z", "e_raw", "e_ui", "usb_e", "chg_e", "fl_e", "ent_ms",
    "g_n", "g_o", "g_ms", "g_mv", "g_t",
    "w_mv", "w_sd", "w_rg", "w_z", "w_raw", "w_ui", "w_dt", "usb_w", "chg_w", "fl_w",
    "rst", "wk", "ab", "ab_r", "cfg", "tmo", "idle_ms", "boots",
    "lit_ms", "lit_pct", "wifi_ms", "full_ms", "inp", "rf", "usb_ms", "sdw_ms", "drop", "crc",
)

SCHEMA_VERSION = 1
UTC_FLOOR = 1735689600  # 2025-01-01T00:00:00Z: an earlier time() is not a synced clock
FILE_CAP_BYTES = 2 * 1024 * 1024

TENTHS_COLS = frozenset(("e_mv", "e_sd", "g_mv", "w_mv", "w_sd"))  # P+ in mV with one decimal
HEX_COLS = frozenset(("fl_e", "fl_w", "cfg"))
CHAR_COLS = frozenset(("k", "path", "chg_e", "chg_w", "wk"))
STR_COLS = frozenset(("fw", "crc"))

# fl_e / fl_w bit numbers (BatteryLogCore.h, namespaces entryflag and wakeflag; the test compares them with the header).
ENTRY_FLAGS = {
    "LIT": 0, "RADIO_ON": 1, "TOUCH_NOT_ASLEEP": 2, "SDA_LOW": 3, "KEY_HELD": 4, "NO_VALID": 5, "NO_ADC_PIN": 6,
    "LEDGER_PARTIAL": 7, "LOCK_TIMEOUT": 8, "SETTINGS_NOT_LOADED": 9,
}
WAKE_FLAGS = {
    "ORPHAN": 0, "RTC_BACKWARDS": 1, "NOT_DEEP_SLEEP": 2, "CHARGE_UNDECODED": 3, "NO_VALID": 4, "NOT_EXT1": 5,
    "PREV_FLUSH_BAD": 6, "LOCK_TIMEOUT": 7, "BACKOFF_SKIPPED": 8, "FILE_CAP": 9,
}

PATH_NAMES = {
    "m": "manual", "a": "auto-sleep timeout", "k": "awake ceiling", "i": "input cap", "w": "unattended wake (WK-02)",
    "b": "low-battery cutoff", "e": "SD-error screen", "q": "Quick Lock timeout", "x": "unknown",
}
RESET_NAMES = {
    0: "unknown", 1: "power-on", 2: "external pin", 3: "software", 4: "panic", 5: "interrupt watchdog",
    6: "task watchdog", 7: "other watchdog", 8: "deep sleep", 9: "brown-out", 10: "SDIO", 11: "USB", 12: "JTAG",
    13: "eFuse", 14: "power glitch", 15: "CPU lock-up",
}
ABNORMAL_RESETS = frozenset((4, 5, 6, 7, 9, 15))

# --- Analysis constants (placeholders from the design's section 8; re-derive them from the first rows) --------------
SHORT_PAIR_MS = 30 * 60 * 1000          # a sleep shorter than this is no rested pair
SHORT_A_MS = 2 * 3600 * 1000            # estimator A leaves out sleeps shorter than this
HIGHSOC_MV = 4060.0                     # at or above: post-charge relaxation
CUTOFF_MV = 3400.0                      # below: the LDO dropout region
LATE_MS = 2000                          # a wake sample taken later than this is not a wake sample
CHG_RISE_MV = 15.0                      # a rise of the wake sample over the previous one with no USB: charged unseen
CHG_LOADED_STEP_MV = 45.0               # or this much from the loaded entry reading to the wake sample
OFFSET_TOL_S = 2.0                      # utc - rtc_ms/1000 steps by more than this at a sync
NOTCH_SCALE_ERRORS = (0.3, 0.5)         # the generic notch table's error against this cell
PAIR_SIGMA_MV = 2.5                     # assumed scatter of a wake-to-wake pair difference
MIN_A_CYCLES = 12
MIN_A_BLOCKS = 7                        # fewer day blocks and the bootstrap band is not trusted: no band is given
RUN_MIN_BLOCKS = 10                     # the same for estimator A on one run between charges
OPTIMISTIC_BLOCKS = 15                  # under this many day blocks the band is still optimistic
B_MIN_AFTER_ENTRY_MS = 2 * 3600 * 1000  # estimator B: both points at least this long after the entry
B_MIN_SPAN_MS = 3 * 3600 * 1000         # and at least this far apart
C_MIN_SLEEP_MS = 6 * 3600 * 1000        # estimator C: a sleep of at least 6 h ...
C_SESSION_MS = (10 * 1000, 5 * 60 * 1000)  # ... preceded by a session of 10 s to 5 min
FADE_GAMMA = 1.6554                     # percent to duty of the front light (FrontlightManager.cpp)
AWAKE_CEILING_MIN = 15                  # on battery the ceiling is the longer of this and the sleep timeout
LONG_TAIL_MS = 20 * 60 * 1000           # an idle tail this long is a runaway tail whatever the timeout says
ENTRY_PAIR_SIGMA_MV = 7.0               # scatter of an entry-to-wake pair (design table A; the entry is a loaded reading)

# Every exclusion reason code, in display order.
ALL_CODES = ("BOOT", "NOSAMPLE", "USB", "CHG_UNSEEN", "CHG_TAIL", "RTC", "SHORT", "SHORT_A", "METHOD", "HIGHSOC",
             "CUTOFF", "GAP", "LATE", "PARTIAL")
# A row is a trustworthy wake point when it has none of these ...
POINT_CODES = frozenset(("BOOT", "NOSAMPLE", "USB", "CHG_UNSEEN", "CHG_TAIL", "METHOD", "HIGHSOC", "CUTOFF", "LATE"))
# ... and its cycle (the session and sleep that end at it) is usable for a pair when it has none of these.
CYCLE_CODES = frozenset(("BOOT", "RTC", "USB", "SHORT", "GAP", "METHOD"))
CODE_TEXT = {
    "BOOT": "orphan or restart boot, not a deep-sleep wake",
    "NOSAMPLE": "no valid wake burst",
    "USB": "USB or charging seen at the entry, the wake or in the session",
    "CHG_UNSEEN": "the voltage rose with no USB flag: a charge nobody saw",
    "CHG_TAIL": "within the post-charge relaxation window after a charge",
    "RTC": "sleep length unknown (no stamp or the RTC counter went backwards)",
    "SHORT": "sleep under 30 min: no rested pair",
    "SHORT_A": "sleep under 2 h: left out of estimator A",
    "METHOD": "a different measurement method or build family than the run's majority",
    "HIGHSOC": "wake sample at or above 4060 mV: post-charge relaxation",
    "CUTOFF": "low-battery cutoff or wake sample under 3400 mV: LDO dropout region",
    "GAP": "a row is missing before it, or rows were dropped",
    "LATE": "wake sample taken more than 2 s after the boot began",
    "PARTIAL": "session ledger incomplete",
}


# --- The notch table (BatteryMonitor.cpp LIION_NOTCH_MV; the test compares this copy with that file) ---------------
NOTCH_MV: Tuple[int, ...] = (3450, 3680, 3740, 3770, 3790, 3820, 3870, 3920, 3980, 4060, 4200)


def percent(mv: float) -> float:
    """Percent of charge at a rested P+ voltage, linear between the 10 % notches (the firmware's tenths, unrounded)."""
    if mv >= NOTCH_MV[-1]:
        return 100.0
    for i in range(10, 0, -1):
        low = NOTCH_MV[i - 1]
        if mv >= low:
            return (i - 1) * 10.0 + (mv - low) * 10.0 / (NOTCH_MV[i] - low)
    return 0.0


def percent_tenths_fw(mv: int) -> int:
    """Exact replica of percentTenthsFromNotches() (whole millivolts, integer arithmetic)."""
    if mv >= NOTCH_MV[10]:
        return 1000
    for i in range(10, 0, -1):
        low = NOTCH_MV[i - 1]
        if mv >= low:
            return (i - 1) * 100 + (mv - low) * 100 // (NOTCH_MV[i] - low)
    return 0


def mv_per_percent(mv: float) -> float:
    """Millivolts per percent of charge in the notch segment holding `mv` (8 at 80-90 %, 3 on the 20-30 % plateau)."""
    i = 1
    for j in range(10, 0, -1):
        if mv >= NOTCH_MV[j - 1]:
            i = j
            break
    return (NOTCH_MV[i] - NOTCH_MV[i - 1]) / 10.0


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final xor; check value 0x29B1)."""
    return binascii.crc_hqx(data, crc)


# --- Source checks (BatteryLogCore.h and BatteryMonitor.cpp, when the SDK is next to this script) ------------------
def default_sdk_dir() -> Path:
    return Path(__file__).resolve().parents[2] / "freeink-sdk"


def core_header_path(sdk_dir: Path) -> Path:
    return sdk_dir / "libs" / "hardware" / "PowerManager" / "include" / "BatteryLogCore.h"


def battery_monitor_path(sdk_dir: Path) -> Path:
    return sdk_dir / "libs" / "hardware" / "BatteryMonitor" / "src" / "BatteryMonitor.cpp"


def parse_core_columns(text: str) -> List[str]:
    """Column names of `constexpr Column COLUMNS[] = {...};` in BatteryLogCore.h, in order."""
    m = re.search(r"constexpr\s+Column\s+COLUMNS\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        return []
    return re.findall(r'\{\s*"([A-Za-z0-9_]+)"\s*,', m.group(1))


def parse_core_flags(text: str, namespace: str) -> Dict[str, int]:
    """Bit numbers of `namespace <name> { constexpr uint16_t X = 1u << N; ... }` in BatteryLogCore.h."""
    m = re.search(r"namespace\s+%s\s*\{(.*?)\}\s*//\s*namespace\s+%s" % (namespace, namespace), text, re.S)
    if not m:
        return {}
    return {name: int(bit) for name, bit in re.findall(r"constexpr\s+uint16_t\s+([A-Z0-9_]+)\s*=\s*1u\s*<<\s*(\d+)",
                                                        m.group(1))}


def parse_core_constant(text: str, name: str) -> Optional[int]:
    m = re.search(r"constexpr\s+\w+\s+%s\s*=\s*([0-9]+)u?;" % re.escape(name), text)
    return int(m.group(1)) if m else None


def parse_notch_table(text: str) -> List[int]:
    """The 11 entries of `LIION_NOTCH_MV[11] = {...};` in BatteryMonitor.cpp."""
    m = re.search(r"LIION_NOTCH_MV\[11\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        return []
    return [int(v) for v in re.findall(r"(\d+)\s*,", m.group(1))]


def check_against_sources(sdk_dir: Optional[Path]) -> List[str]:
    """Differences between this script's copies and the firmware sources, or [] when they agree (or are not there)."""
    problems: List[str] = []
    if sdk_dir is None:
        return problems
    header = core_header_path(sdk_dir)
    if header.is_file():
        text = header.read_text(encoding="utf-8", errors="replace")
        cols = parse_core_columns(text)
        if cols and tuple(cols) != COLUMNS:
            problems.append("the column list differs from BatteryLogCore.h (the script is out of date)")
        for ns, mine in (("entryflag", ENTRY_FLAGS), ("wakeflag", WAKE_FLAGS)):
            theirs = parse_core_flags(text, ns)
            if theirs and theirs != mine:
                problems.append("the %s bits differ from BatteryLogCore.h" % ns)
        ver = parse_core_constant(text, "SCHEMA_VERSION")
        if ver is not None and ver != SCHEMA_VERSION:
            problems.append("the schema version is %d in BatteryLogCore.h, %d here" % (ver, SCHEMA_VERSION))
    monitor = battery_monitor_path(sdk_dir)
    if monitor.is_file():
        notch = parse_notch_table(monitor.read_text(encoding="utf-8", errors="replace"))
        if notch and tuple(notch) != NOTCH_MV:
            problems.append("the notch table differs from BatteryMonitor.cpp (the percent columns are off)")
    return problems


# --- Parsing ------------------------------------------------------------------------------------------------------
class Row:
    """One validated CSV row. `f` maps column name to a converted value (None for an empty field)."""

    def __init__(self, f: Dict[str, object], src: str, line_no: int, text: str) -> None:
        self.f = f
        self.src = src
        self.line_no = line_no
        self.text = text
        self.family: Optional[Tuple[str, int, int, int]] = None
        self.seg = -1
        self.idx = -1
        self.rtc_s: Optional[float] = None
        self.rel_s = 0.0
        self.wall: Optional[float] = None
        self.wall_src = "rel"
        self.excl: List[str] = []
        self.pair_prev: Optional["Row"] = None
        self.pair_dpct: Optional[float] = None
        self.pair_h: Optional[float] = None
        self.disp = ""

    def g(self, name: str):
        return self.f.get(name)

    def has_flag(self, column: str, bit: int) -> bool:
        return bool(((self.f.get(column) or 0) >> bit) & 1)

    @property
    def sleep_h(self) -> Optional[float]:
        v = self.f.get("sleep_ms")
        return None if v is None else v / 3.6e6

    @property
    def awake_min(self) -> Optional[float]:
        v = self.f.get("awake_ms")
        return None if v is None else v / 60000.0


@dataclass
class FwLine:
    sha: str
    version: str
    method: int
    wait_ms: int
    serial: int
    before_rows: int  # rows of the same file parsed before this line


@dataclass
class FileStats:
    lines: int = 0
    blank: int = 0
    comment: int = 0
    headers: int = 0
    bad_header: int = 0
    good: int = 0
    bad_crc: int = 0
    torn: int = 0
    torn_tail: int = 0
    glued_lines: int = 0
    glued_rows: int = 0
    bad_field: int = 0
    no_header: int = 0
    duplicates: int = 0

    def add(self, other: "FileStats") -> None:
        for name in self.__dataclass_fields__:
            setattr(self, name, getattr(self, name) + getattr(other, name))


@dataclass
class FileResult:
    path: str
    rows: List[Row]
    fw_lines: List[FwLine]
    stats: FileStats
    warnings: List[str]
    size: int


_FW_LINE = re.compile(r"^# fw (\S+) (\S+) m(\d+) w(\d+) ser([01])\s*$")
_NAME = re.compile(r"[a-z][a-z0-9_]*\Z")
_HEX = frozenset("0123456789abcdef")
_START = re.compile(r"(?=(\d{1,3},\d+,[SB],[0-9a-f]{9},))")


def check_row_text(text: str, ncols: int) -> Tuple[str, Optional[List[str]]]:
    """('ok' | 'columns' | 'crc', fields). The CRC covers the text up to and including the last comma."""
    fields = text.split(",")
    if len(fields) != ncols:
        return "columns", None
    crc_s = fields[-1]
    if len(crc_s) != 4 or any(c not in _HEX for c in crc_s):
        return "crc", fields
    body = text[: text.rfind(",") + 1]
    if crc16(body.encode("latin-1")) != int(crc_s, 16):
        return "crc", fields
    return "ok", fields


def split_glued(text: str, ncols: int, depth: int = 3) -> Tuple[List[str], int]:
    """Valid rows inside a line that failed validation (a lost LF glued a torn row to the next one), and the number of
    leftover fragments. A candidate row must start like one (version, row number, k, nine hex of fw) and pass its CRC."""
    status, _ = check_row_text(text, ncols)
    if status == "ok":
        return [text], 0
    if depth <= 0:
        return [], 1
    best: Tuple[List[str], int] = ([], 1)
    starts = [m.start() for m in _START.finditer(text) if m.start() > 0][:12]
    for i in starts:
        head_rows, head_frag = split_glued(text[:i], ncols, depth - 1)
        tail_rows, tail_frag = split_glued(text[i:], ncols, depth - 1)
        rows = head_rows + tail_rows
        frags = head_frag + tail_frag
        if len(rows) > len(best[0]) or (rows and len(rows) == len(best[0]) and frags < best[1]):
            best = (rows, frags)
    return best


def convert_fields(names: Sequence[str], values: Sequence[str]) -> Optional[Dict[str, object]]:
    """Typed fields of a validated row, or None when a field does not parse."""
    out: Dict[str, object] = {}
    for name, raw in zip(names, values):
        if raw == "":
            out[name] = None
            continue
        if name in TENTHS_COLS:
            if not re.match(r"\d+\.\d\Z", raw):
                return None
            out[name] = float(raw)
        elif name in HEX_COLS:
            if any(c not in _HEX for c in raw):
                return None
            out[name] = int(raw, 16)
        elif name in CHAR_COLS or name in STR_COLS:
            out[name] = raw
        elif name in COLUMNS:
            if not (raw.isascii() and raw.isdigit()):
                return None
            out[name] = int(raw)
        else:
            out[name] = raw  # a column a newer firmware appended: kept as text
    if out.get("k") not in ("S", "B") or len(str(out.get("k"))) != 1:
        return None
    return out


def parse_bytes(data: bytes, source: str = "<memory>") -> FileResult:
    """Parse one battery.csv. Never raises on damage: every line is classified."""
    if data.startswith(b"\xef\xbb\xbf"):
        data = data[3:]
    text = data.decode("latin-1")
    lines = text.split("\n")
    last = max((i for i, ln in enumerate(lines) if ln.strip()), default=-1)
    stats = FileStats()
    rows: List[Row] = []
    fw_lines: List[FwLine] = []
    warnings: List[str] = []
    names: List[str] = list(COLUMNS)
    have_header = False
    for i, raw in enumerate(lines):
        line = raw[:-1] if raw.endswith("\r") else raw
        stats.lines += 1
        if line.strip() == "":
            stats.blank += 1
            continue
        if line.startswith("#"):
            stats.comment += 1
            m = _FW_LINE.match(line)
            if m:
                fw_lines.append(FwLine(m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5)),
                                       len(rows)))
            continue
        if line.startswith("v,"):
            cols = line.split(",")
            if cols[-1] != "crc" or not all(_NAME.match(c) for c in cols):
                stats.bad_header += 1
                continue
            stats.headers += 1
            have_header = True
            names = cols
            extra = [c for c in cols if c not in COLUMNS]
            missing = [c for c in COLUMNS if c not in cols]
            if extra:
                warnings.append("%s: the header carries columns this script does not know: %s" % (source, ",".join(extra)))
            if missing:
                warnings.append("%s: the header lacks columns: %s" % (source, ",".join(missing)))
            continue
        if not have_header and stats.no_header == 0:
            stats.no_header = 1
            warnings.append("%s: rows before any header line; the built-in column list is assumed" % source)
        status, fields = check_row_text(line, len(names))
        if status == "ok":
            texts, frags = [line], 0
        elif status == "crc":
            stats.bad_crc += 1
            continue
        else:
            texts, frags = split_glued(line, len(names))
            if not texts:
                if i == last:
                    stats.torn_tail += 1
                else:
                    stats.torn += 1
                continue
            stats.glued_lines += 1
            stats.glued_rows += len(texts)
            stats.torn += frags
        for t in texts:
            _, flds = check_row_text(t, len(names))
            conv = convert_fields(names, flds or [])
            if conv is None:
                stats.bad_field += 1
                continue
            stats.good += 1
            rows.append(Row(conv, source, i + 1, t))
    return FileResult(source, rows, fw_lines, stats, warnings, len(data))


@dataclass
class Dataset:
    files: List[FileResult]
    rows: List[Row]
    stats: FileStats
    warnings: List[str]
    fw_lines: List[FwLine]
    families: Counter


def _file_start_wall(res: FileResult) -> Optional[float]:
    """Wall time of a file's first row from its first row with a utc (None without one)."""
    first = res.rows[0].g("rtc_ms") if res.rows else None
    for r in res.rows:
        utc, rtc = r.g("utc"), r.g("rtc_ms")
        if utc is not None and utc >= UTC_FLOOR and rtc is not None and first is not None:
            return utc - (rtc - first) / 1000.0
    return None


def merge_results(results: List[FileResult]) -> Dataset:
    """Order the files, drop exact duplicate rows, and attach each row's build family."""
    warnings: List[str] = []
    for r in results:
        warnings.extend(r.warnings)
    order = list(results)
    with_rows = [r for r in order if r.rows]
    if len(with_rows) > 1:
        keys = [_file_start_wall(r) for r in with_rows]
        if all(k is not None for k in keys):
            ranked = [r for _, r in sorted(zip(keys, with_rows), key=lambda p: p[0])]  # stable
            order = ranked + [r for r in order if not r.rows]
        else:
            warnings.append("files are taken in command-line order: not every file has a synced utc to order them by")
    fwmap: Dict[str, FwLine] = {}
    all_fw: List[FwLine] = []
    seen = set()
    rows: List[Row] = []
    stats = FileStats()
    for res in order:
        stats.add(res.stats)
        for fl in res.fw_lines:
            fwmap[fl.sha] = fl
            all_fw.append(fl)
        for pos, r in enumerate(res.rows):
            if r.text in seen:
                stats.duplicates += 1
                stats.good -= 1
                continue
            seen.add(r.text)
            fl = fwmap.get(r.g("fw") or "")
            if fl is None:
                before = [x for x in res.fw_lines if x.before_rows <= pos]
                fl = before[-1] if before else None
            if fl is not None:
                r.family = (fl.version, fl.method, fl.wait_ms, fl.serial)
            rows.append(r)
    families = Counter(r.family for r in rows if r.family is not None)
    return Dataset(order, rows, stats, warnings, all_fw, families)


def load_files(paths: Sequence[str]) -> Dataset:
    results = []
    for p in paths:
        with open(p, "rb") as fh:
            results.append(parse_bytes(fh.read(), str(p)))
    return merge_results(results)


# --- Segments and the timeline --------------------------------------------------------------------------------------
@dataclass
class Segment:
    id: int
    rows: List[Row] = field(default_factory=list)
    synced: bool = False
    epochs: List[Tuple[int, float]] = field(default_factory=list)  # (index of its first row, utc - rtc_s)
    drift: List[Tuple[float, float, float]] = field(default_factory=list)  # (step s, between h, ppm slow)
    bad_utc: int = 0
    reason: str = "first row"


def build_segments(rows: List[Row]) -> List[Segment]:
    """A new segment at every k = B, backwards rtc_ms or restarted row number."""
    segs: List[Segment] = []
    prev: Optional[Row] = None
    for i, r in enumerate(rows):
        r.idx = i
        rtc = r.g("rtc_ms")
        r.rtc_s = None if rtc is None else rtc / 1000.0
        reason = "first row" if prev is None else ("orphan boot (k = B)" if r.g("k") == "B" else "")
        if not reason:
            prtc, rrow, prow = prev.g("rtc_ms"), r.g("row"), prev.g("row")
            if rtc is not None and prtc is not None and rtc < prtc:
                reason = "rtc_ms went backwards"
            elif rrow is not None and prow is not None and rrow <= prow:
                reason = "row number restarted"
        if reason:
            segs.append(Segment(len(segs), reason=reason))
        r.seg = segs[-1].id
        segs[-1].rows.append(r)
        prev = r
    return segs


def fill_wall_time(seg: Segment) -> None:
    """Wall time of every row of a segment. A row with a utc is dated by it; the others by the offset utc - rtc_s of
    the nearest earlier row that has one (the first one's, back-filled, before any)."""
    rows = seg.rows
    base = next((r.rtc_s for r in rows if r.rtc_s is not None), 0.0)
    for r in rows:
        r.rel_s = (r.rtc_s - base) if r.rtc_s is not None else 0.0
    utc_rows: List[Tuple[int, Row, float]] = []
    for pos, r in enumerate(rows):
        utc = r.g("utc")
        if utc is None:
            continue
        if utc < UTC_FLOOR or r.rtc_s is None:
            seg.bad_utc += 1
            continue
        utc_rows.append((pos, r, utc - r.rtc_s))
    if not utc_rows:
        return
    seg.synced = True
    epochs: List[Tuple[int, float]] = []
    members: List[List[float]] = []
    for pos, _, off in utc_rows:
        if not epochs or abs(off - members[-1][0]) > OFFSET_TOL_S:
            epochs.append((pos, off))
            members.append([off])
        else:
            members[-1].append(off)
    seg.epochs = [(pos, statistics.median(m)) for (pos, _), m in zip(epochs, members)]
    cur = 0
    for pos, r in enumerate(rows):
        while cur + 1 < len(seg.epochs) and seg.epochs[cur + 1][0] <= pos:
            cur += 1
        if r.g("utc") is not None and r.g("utc") >= UTC_FLOOR and r.rtc_s is not None:
            r.wall = float(r.g("utc"))
            r.wall_src = "utc"
        elif r.rtc_s is not None:
            r.wall = r.rtc_s + seg.epochs[cur][1]
            r.wall_src = "fill"
    # The step of the offset at a sync is the RC clock's drift since the previous sync: positive when it ran slow.
    for k in range(1, len(seg.epochs)):
        a, b = seg.epochs[k - 1], seg.epochs[k]
        ra, rb = rows[a[0]], rows[b[0]]
        if ra.rtc_s is None or rb.rtc_s is None or rb.rtc_s <= ra.rtc_s:
            continue
        step = b[1] - a[1]
        elapsed = rb.rtc_s - ra.rtc_s
        seg.drift.append((step, elapsed / 3600.0, step / elapsed * 1e6))


# --- Derived values per row -----------------------------------------------------------------------------------------
def ui_pct(tenths: Optional[int]) -> Optional[int]:
    """What the screen showed, from the 0.1 % value: (tenths + 5) / 10."""
    return None if tenths is None else (tenths + 5) // 10


def touch_fitted(rows: Iterable[Row]) -> Optional[bool]:
    vals = [bool(r.g("cfg") & 4) for r in rows if r.g("cfg") is not None]
    if not vals:
        return None
    return sum(vals) * 2 >= len(vals)


def usb_evidence(r: Row) -> bool:
    if r.g("usb_e") == 1 or r.g("usb_w") == 1:
        return True
    if r.g("chg_e") in ("c", "f", "x", "n") or r.g("chg_w") in ("c", "f", "x", "n"):
        return True
    return (r.g("usb_ms") or 0) > 0


def classify(rows: List[Row], charge_hold_h: float = 12.0) -> None:
    """Fill `excl` of every row with the reason codes of design section 8, item 8."""
    methods = Counter(r.g("m") for r in rows if r.g("m") is not None)
    major_m = methods.most_common(1)[0][0] if methods else None
    major_family = None
    fams = Counter(r.family for r in rows if r.family is not None)
    if fams:
        major_family = fams.most_common(1)[0][0]
    prev: Optional[Row] = None
    charge_t: Optional[Tuple[int, float]] = None  # (segment, rtc_s of the last charge evidence)
    for r in rows:
        ex: List[str] = []
        w_mv, sleep_ms = r.g("w_mv"), r.g("sleep_ms")
        k, rst, path = r.g("k"), r.g("rst"), r.g("path")
        boot = k == "B" or (rst is not None and rst != 8) or r.has_flag("fl_w", 0) or r.has_flag("fl_w", 2)
        if boot:
            ex.append("BOOT")
        elif w_mv is None:
            ex.append("NOSAMPLE")
        usb = usb_evidence(r)
        if usb:
            ex.append("USB")
        same = prev is not None and prev.seg == r.seg
        unseen = False
        if not usb and not boot and w_mv is not None:
            pw = prev.g("w_mv") if same else None
            if pw is not None and w_mv - pw >= CHG_RISE_MV:
                unseen = True
            e_mv = r.g("e_mv")
            if e_mv is not None and w_mv - e_mv > CHG_LOADED_STEP_MV:
                unseen = True
        if unseen:
            ex.append("CHG_UNSEEN")
        elif not usb and charge_t is not None and charge_t[0] == r.seg and r.rtc_s is not None \
                and 0 <= r.rtc_s - charge_t[1] <= charge_hold_h * 3600.0:
            ex.append("CHG_TAIL")
        if usb or unseen:
            charge_t = (r.seg, r.rtc_s) if r.rtc_s is not None else None
        if k == "S" and (sleep_ms is None or r.has_flag("fl_w", 1)):
            ex.append("RTC")
        if sleep_ms is not None and not boot:
            if sleep_ms < SHORT_PAIR_MS:
                ex.append("SHORT")
            elif sleep_ms < SHORT_A_MS:
                ex.append("SHORT_A")
        if (major_m is not None and r.g("m") is not None and r.g("m") != major_m) or \
                (major_family is not None and r.family is not None and r.family != major_family):
            ex.append("METHOD")
        if w_mv is not None and w_mv >= HIGHSOC_MV:
            ex.append("HIGHSOC")
        if path == "b" or (w_mv is not None and w_mv < CUTOFF_MV):
            ex.append("CUTOFF")
        if same and not boot:
            rown, prow = r.g("row"), prev.g("row")
            if (rown is not None and prow is not None and rown != prow + 1) or (r.g("drop") or 0) > 0:
                ex.append("GAP")
        if k == "S" and (r.has_flag("fl_e", 7) or r.g("awake_ms") is None):
            ex.append("PARTIAL")
        if r.g("w_dt") is not None and r.g("w_dt") > LATE_MS and not boot:
            ex.append("LATE")
        r.excl = [c for c in ALL_CODES if c in ex]
        prev = r


def point_ok(r: Row) -> bool:
    return not any(c in POINT_CODES for c in r.excl)


def cycle_ok(r: Row) -> bool:
    return not any(c in CYCLE_CODES for c in r.excl)


def form_pairs(rows: List[Row]) -> None:
    """The wake-to-wake pairs: row n with the previous row's wake sample, when both are trustworthy wake points and the
    cycle between them is usable."""
    for i, r in enumerate(rows):
        r.pair_prev = None
        r.pair_dpct = None
        r.pair_h = None
        if i == 0:
            continue
        prev = rows[i - 1]
        if prev.seg != r.seg or r.g("k") != "S":
            continue
        if not (point_ok(prev) and point_ok(r) and cycle_ok(r)):
            continue
        if prev.rtc_s is None or r.rtc_s is None or r.rtc_s <= prev.rtc_s:
            continue
        r.pair_prev = prev
        r.pair_dpct = percent(prev.g("w_mv")) - percent(r.g("w_mv"))
        r.pair_h = (r.rtc_s - prev.rtc_s) / 3600.0


def prepare(ds: Dataset, charge_hold_h: float = 12.0) -> List[Segment]:
    segs = build_segments(ds.rows)
    for s in segs:
        fill_wall_time(s)
    classify(ds.rows, charge_hold_h)
    form_pairs(ds.rows)
    for r in ds.rows:
        classify_display(r)
    return segs


def classify_display(r: Row) -> None:
    """Display versus volts for one cycle: steady, rounding (the integer flipped on a sub-percent change) or a step."""
    e_ui, w_ui = r.g("e_ui"), r.g("w_ui")
    if r.g("k") != "S" or e_ui is None or w_ui is None:
        r.disp = ""
        return
    shown = ui_pct(w_ui) - ui_pct(e_ui)
    moved = (w_ui - e_ui) / 10.0
    if shown == 0:
        r.disp = "steady"
    elif abs(moved) < 1.0:
        r.disp = "rounding"
    else:
        r.disp = "step"


# --- Linear algebra: a small non-negative least squares on the normal equations --------------------------------------
def solve_linear(a: List[List[float]], b: List[float], idx: Sequence[int]) -> Optional[List[float]]:
    """Solve a[idx][idx] z = b[idx] by Gaussian elimination with partial pivoting; None when singular."""
    m = len(idx)
    mat = [[a[i][j] for j in idx] + [b[i]] for i in idx]
    for c in range(m):
        piv = max(range(c, m), key=lambda rr: abs(mat[rr][c]))
        if abs(mat[piv][c]) < 1e-12:
            return None
        mat[c], mat[piv] = mat[piv], mat[c]
        inv = 1.0 / mat[c][c]
        for rr in range(c + 1, m):
            f = mat[rr][c] * inv
            if f:
                for kk in range(c, m + 1):
                    mat[rr][kk] -= f * mat[c][kk]
    z = [0.0] * m
    for c in range(m - 1, -1, -1):
        z[c] = (mat[c][m] - sum(mat[c][kk] * z[kk] for kk in range(c + 1, m))) / mat[c][c]
    return z


def nnls_gram(gram: List[List[float]], xty: List[float]) -> List[float]:
    """Non-negative least squares (Lawson-Hanson) from X'X and X'y. Columns are scaled to unit length first."""
    p = len(xty)
    d = [1.0 / math.sqrt(gram[i][i]) if gram[i][i] > 0 else 0.0 for i in range(p)]
    g = [[gram[i][j] * d[i] * d[j] + (1e-9 if i == j else 0.0) for j in range(p)] for i in range(p)]
    h = [xty[i] * d[i] for i in range(p)]
    x = [0.0] * p
    passive: List[int] = []
    blocked = {i for i in range(p) if d[i] == 0.0}
    for _ in range(3 * p + 10):
        w = [h[i] - sum(g[i][j] * x[j] for j in passive) for i in range(p)]
        cand = [(w[i], i) for i in range(p) if i not in passive and i not in blocked and w[i] > 1e-10]
        if not cand:
            break
        t = max(cand)[1]
        passive.append(t)
        for _inner in range(p + 5):
            z = solve_linear(g, h, passive)
            if z is None:
                passive.remove(t)
                blocked.add(t)
                break
            if all(v > 1e-12 for v in z):
                for i, v in zip(passive, z):
                    x[i] = v
                break
            alpha = 1.0
            for i, v in zip(passive, z):
                if v <= 1e-12:
                    den = x[i] - v
                    alpha = min(alpha, x[i] / den if den > 1e-300 else 0.0)
            for i, v in zip(passive, z):
                x[i] += alpha * (v - x[i])
            for i in list(passive):
                if x[i] <= 1e-12:
                    x[i] = 0.0
                    passive.remove(i)
            if not passive:
                break
    return [x[i] * d[i] for i in range(p)]


def percentile(sorted_vals: Sequence[float], q: float) -> float:
    """q in [0, 100], linear between neighbours."""
    if not sorted_vals:
        raise ValueError("no values")
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    pos = (len(sorted_vals) - 1) * q / 100.0
    lo = int(math.floor(pos))
    hi = min(lo + 1, len(sorted_vals) - 1)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (pos - lo)


# --- Estimators ---------------------------------------------------------------------------------------------------
@dataclass
class Options:
    capacity_mah: float = 500.0
    pair_sigma_mv: float = PAIR_SIGMA_MV
    resamples: int = 2000
    seed: int = 20260930
    early_ms_limit: int = 300
    charge_hold_h: float = 12.0
    rows_shown: int = 30
    sdk_dir: Optional[Path] = None


def expected_floor_ua(capacity_mah: float, touch: bool) -> float:
    """Sleep floor (110-127 uA with the touch controller, about 58 without) plus self-discharge (14-21 uA at 500 mAh)."""
    return (113.0 if touch else 58.0) + 17.5 * capacity_mah / 500.0


def watch_threshold_ua(capacity_mah: float, touch: bool) -> float:
    return (130.0 if touch else 95.0) + 17.5 * (capacity_mah - 500.0) / 500.0


def meas_threshold_ua(capacity_mah: float) -> float:
    """Above this a lower bound says: run the meter (MEAS-01). Every threshold moves 17.5 uA per 500 mAh."""
    return 150.0 + 17.5 * (capacity_mah - 500.0) / 500.0


class Rates:
    """Percent per hour to microamps (with a capacity) or millivolts per day (without)."""

    def __init__(self, capacity_mah: float, mv_per_pct: float) -> None:
        self.capacity = capacity_mah
        self.mv_per_pct = mv_per_pct

    def ua(self, pct_per_h: float) -> Optional[float]:
        return pct_per_h * self.capacity * 10.0 if self.capacity > 0 else None

    def fmt(self, pct_per_h: float) -> str:
        ua = self.ua(pct_per_h)
        if ua is None:
            return "%.1f mV/day" % (pct_per_h * 24.0 * self.mv_per_pct)
        return fmt_ua(ua)


def fmt_ua(ua: float) -> str:
    return "%.2f mA" % (ua / 1000.0) if abs(ua) >= 1000.0 else "%.0f uA" % ua


@dataclass
class Estimate:
    key: str
    title: str
    status: str = "no data"           # 'ok' or 'no data'
    n: int = 0
    pct_h: Optional[float] = None     # the sleep drain in percent of the cell per hour
    lo: Optional[float] = None
    hi: Optional[float] = None
    se: Optional[float] = None
    lines: List[str] = field(default_factory=list)
    resolved_zero: bool = False
    resolved_floor: bool = False
    est_ua: Optional[float] = None
    lo_ua: Optional[float] = None
    hi_ua: Optional[float] = None
    min_zero_ua: Optional[float] = None
    min_floor_ua: Optional[Tuple[float, float]] = None
    terms: Dict[str, Tuple[float, float, float]] = field(default_factory=dict)
    residual_mv: Optional[float] = None
    head: str = ""
    ceiling: bool = False             # an upper bound only: never compared with zero or the floor, never a flag


A_TERMS = ("sleep_h", "full_h", "low_h", "lit_h", "wifi_h", "cycle", "early")


def duty(lit_pct: Optional[int]) -> float:
    if not lit_pct:
        return 0.0
    return (min(max(lit_pct, 0), 100) / 100.0) ** FADE_GAMMA


def a_ok(r: Row) -> bool:
    return r.pair_prev is not None and "SHORT_A" not in r.excl and "PARTIAL" not in r.excl and \
        r.g("awake_ms") is not None and r.g("sleep_ms") is not None


def design_vector(r: Row) -> List[float]:
    awake = r.g("awake_ms") or 0
    full = min(r.g("full_ms") or 0, awake)
    return [
        r.g("sleep_ms") / 3.6e6,
        full / 3.6e6,
        max(awake - full, 0) / 3.6e6,
        (r.g("lit_ms") or 0) / 3.6e6 * duty(r.g("lit_pct")),
        (r.g("wifi_ms") or 0) / 3.6e6,
        1.0,
        float((r.g("g_n") or 0) + (r.g("g_o") or 0)),
    ]


def block_key(r: Row) -> Tuple[int, int]:
    if r.wall is not None:
        return (-1, int(r.wall // 86400))
    return (r.seg, int(r.rel_s // 86400))


def _pack(xs: List[List[float]], ys: List[float], p: int) -> List[float]:
    """The block's X'X (upper triangle), X'y, y'y and n as one flat vector, so a resample is a sum of vectors."""
    out = [0.0] * (p * (p + 1) // 2 + p + 2)
    for x, y in zip(xs, ys):
        pos = 0
        for i in range(p):
            xi = x[i]
            for j in range(i, p):
                out[pos] += xi * x[j]
                pos += 1
        for i in range(p):
            out[pos + i] += x[i] * y
        out[pos + p] += y * y
        out[pos + p + 1] += 1.0
    return out


def _unpack(vec: List[float], p: int) -> Tuple[List[List[float]], List[float], float, float]:
    gram = [[0.0] * p for _ in range(p)]
    pos = 0
    for i in range(p):
        for j in range(i, p):
            gram[i][j] = gram[j][i] = vec[pos]
            pos += 1
    xty = vec[pos:pos + p]
    return gram, xty, vec[pos + p], vec[pos + p + 1]


def estimator_a(rows: List[Row], opts: Options, rates: Rates, touch: bool) -> Estimate:
    """Pooled non-negative regression of the wake-to-wake charge on the time spent in each state."""
    return fit_a([r for r in rows if a_ok(r)], opts, rates, touch, "A",
                 "A. Pooled wake-pair regression (the trend detector)")


def fit_a(sel: List[Row], opts: Options, rates: Rates, touch: bool, key: str, title: str,
          min_blocks: int = MIN_A_BLOCKS) -> Estimate:
    """The regression of estimator A on the given eligible rows, with a bootstrap over day blocks for the band."""
    est = Estimate(key, title)
    if len(sel) < MIN_A_CYCLES:
        est.n = len(sel)
        est.lines.append("not enough eligible cycles: %d, need %d" % (len(sel), MIN_A_CYCLES))
        return est
    xs = [design_vector(r) for r in sel]
    ys = [r.pair_dpct for r in sel]
    active = [j for j in range(len(A_TERMS)) if any(x[j] != 0.0 for x in xs)]
    if 0 not in active:
        est.lines.append("no sleep time among the eligible cycles")
        return est
    p = len(active)
    xs = [[x[j] for j in active] for x in xs]
    blocks: Dict[Tuple[int, int], List[int]] = {}
    for i, r in enumerate(sel):
        blocks.setdefault(block_key(r), []).append(i)
    vecs = [_pack([xs[i] for i in ix], [ys[i] for i in ix], p) for ix in blocks.values()]
    total = [sum(col) for col in zip(*vecs)]
    gram, xty, yty, n = _unpack(total, p)
    coef = nnls_gram(gram, xty)
    est.n = len(sel)
    sse = yty - 2.0 * sum(c * v for c, v in zip(coef, xty)) + sum(
        coef[i] * gram[i][j] * coef[j] for i in range(p) for j in range(p))
    dof = max(n - sum(1 for c in coef if c > 0), 1.0)
    resid_pct = math.sqrt(max(sse, 0.0) / dof)
    mvpp = statistics.median(mv_per_percent(r.g("w_mv")) for r in sel)
    rates.mv_per_pct = mvpp
    est.residual_mv = resid_pct * mvpp
    sleep_i = active.index(0)
    est.pct_h = coef[sleep_i]
    nb = len(vecs)
    boot: Optional[List[List[float]]] = None
    if nb >= min_blocks and opts.resamples > 0:
        rng = random.Random(opts.seed)
        boot = [[] for _ in range(p)]
        for _ in range(opts.resamples):
            pick = [vecs[rng.randrange(nb)] for _ in range(nb)]
            g2, h2, _, _ = _unpack([sum(col) for col in zip(*pick)], p)
            c2 = nnls_gram(g2, h2)
            for i in range(p):
                boot[i].append(c2[i])
        for i in range(p):
            boot[i].sort()
        s = boot[sleep_i]
        est.lo, est.hi = percentile(s, 2.5), percentile(s, 97.5)
        est.se = statistics.pstdev(s)
    for k, j in enumerate(active):
        name = A_TERMS[j]
        if boot is not None:
            est.terms[name] = (coef[k], percentile(boot[k], 2.5), percentile(boot[k], 97.5))
        else:
            est.terms[name] = (coef[k], float("nan"), float("nan"))
    est.status = "ok"
    span_h = sum(x[0] for x in xs)
    est.lines.append("%d cycles over %d day blocks, %.0f h of sleep, residual scatter %.1f mV per pair (assumed %.1f)"
                     % (est.n, nb, span_h, est.residual_mv, opts.pair_sigma_mv))
    _finish_rate(est, rates, opts, touch)
    if boot is None:
        est.lines.append("fewer than %d day blocks (or no resamples): no band, so nothing is resolved" % min_blocks)
    elif nb < OPTIMISTIC_BLOCKS:
        est.lines.append("only %d day blocks: the bootstrap band is optimistic below about %d" % (nb, OPTIMISTIC_BLOCKS))
    return est


def _finish_rate(est: Estimate, rates: Rates, opts: Options, touch: bool) -> None:
    """Express an estimate's rate, band, the status against zero and against the floor, and the resolution limits."""
    cap = opts.capacity_mah
    est.est_ua = rates.ua(est.pct_h) if est.pct_h is not None else None
    if est.lo is not None:
        est.lo_ua, est.hi_ua = rates.ua(est.lo), rates.ua(est.hi)
        est.resolved_zero = est.lo > 0.0 and not est.ceiling
    if est.ceiling and est.lo is not None:
        # The short session's own boot, paint and tail are inside every pair, so even the lower end says nothing about
        # the sleep current: shown as a ceiling, never compared with zero or the floor, and no flag comes from it.
        head = "sleep drain is at most %s (upper end of the 95%% band; the mean %s has the short sessions inside it)" % (
            rates.fmt(est.hi), rates.fmt(est.pct_h))
        est.head = head
        est.lines.append(head)
        est.lines.append("a ceiling only: it is not compared with zero or with the expected floor, and no flag is raised "
                         "from it")
        return
    head = "sleep drain %s" % rates.fmt(est.pct_h)
    if est.lo is not None:
        head += ", 95%% band [%s .. %s]" % (rates.fmt(est.lo), rates.fmt(est.hi))
    est.head = head
    est.lines.append(head)
    if est.lo is None:
        return
    est.lines.append("against zero: %s" % ("RESOLVED (lower bound above 0)" if est.resolved_zero
                                          else "not resolved (the band reaches 0)"))
    if cap > 0 and est.se is not None:
        se_ua = rates.ua(est.se)
        i0 = expected_floor_ua(cap, touch)
        lim = [3.0 * math.sqrt(se_ua ** 2 + (s * i0) ** 2) for s in NOTCH_SCALE_ERRORS]
        est.min_zero_ua = 3.0 * se_ua
        est.min_floor_ua = (lim[0], lim[1])
        mid = 0.4
        est.resolved_floor = est.lo_ua / (1.0 + mid) > i0
        est.lines.append("expected floor about %s (%s): the data %s an excess over it"
                         % (fmt_ua(i0), "touch fitted" if touch else "no touch", "RESOLVE" if est.resolved_floor
                            else "do not resolve"))
        est.lines.append("smallest it could have resolved (3 sigma): %s from zero; an excess over the floor only above "
                         "%s to %s (the notch table's %d-%d %% scale error is in that)"
                         % (fmt_ua(est.min_zero_ua), fmt_ua(lim[0]), fmt_ua(lim[1]),
                            round(NOTCH_SCALE_ERRORS[0] * 100), round(NOTCH_SCALE_ERRORS[1] * 100)))
    elif est.se is not None:
        est.lines.append("smallest it could have resolved (3 sigma): %s (no capacity given: no comparison with the floor)"
                         % rates.fmt(3.0 * est.se))


def estimator_b(rows: List[Row], opts: Options, rates: Rates, touch: bool) -> Estimate:
    """Two wake samples inside one sleep (a late ghost, SD-fail ghost or boot cutoff took the second)."""
    est = Estimate("B", "B. Wake to wake inside one sleep (g_mv to w_mv)")
    events = []
    for r in rows:
        g_mv, g_t, w_mv, sleep_ms = r.g("g_mv"), r.g("g_t"), r.g("w_mv"), r.g("sleep_ms")
        if r.g("k") != "S" or g_mv is None or g_t is None or w_mv is None or sleep_ms is None:
            continue
        if not (point_ok(r) and cycle_ok(r)) or g_mv >= HIGHSOC_MV or g_mv < CUTOFF_MV:
            continue
        if g_t < B_MIN_AFTER_ENTRY_MS or sleep_ms - g_t < B_MIN_SPAN_MS:
            continue
        span_h = (sleep_ms - g_t) / 3.6e6
        events.append((r, percent(g_mv) - percent(w_mv), span_h, mv_per_percent(w_mv)))
    est.n = len(events)
    if not events:
        est.lines.append("no event: it needs an early wake that took a sample at least 2 h after the entry and 3 h "
                         "before the wake (rare)")
        return est
    est.status = "ok"
    ests, ws = [], []
    for r, dp, span, mvpp in events:
        sig = opts.pair_sigma_mv / mvpp / span  # percent per hour
        ests.append(dp / span)
        ws.append(1.0 / (sig * sig))
    mean = sum(e * w for e, w in zip(ests, ws)) / sum(ws)
    se = math.sqrt(1.0 / sum(ws))
    rates.mv_per_pct = statistics.median(mv for _, _, _, mv in events)
    est.pct_h, est.se = mean, se
    est.lo, est.hi = mean - 1.96 * se, mean + 1.96 * se
    est.lines.append("%d event(s); both points are partly settled wake samples of different boot paths, so a bias that "
                     "does not cancel is possible; scatter assumed %.1f mV per pair" % (est.n, opts.pair_sigma_mv))
    for r, dp, span, _ in events[:5]:
        est.lines.append("  row %s: %.2f %% over %.1f h = %s" % (r.g("row"), dp, span, rates.fmt(dp / span)))
    _finish_rate(est, rates, opts, touch)
    return est


def estimator_c(rows: List[Row], opts: Options, rates: Rates, touch: bool) -> Estimate:
    """Overnight pairs: a sleep of 6 h or more after a short, dark, radio-off session. An upper bound, because the
    short session's own cost is inside the pair."""
    est = Estimate("C", "C. Bookend and overnight pairs (a ceiling on the sleep drain)", ceiling=True)
    sel = []
    for r in rows:
        if r.pair_prev is None or "PARTIAL" in r.excl:
            continue
        sleep_ms, awake_ms = r.g("sleep_ms"), r.g("awake_ms")
        if sleep_ms is None or awake_ms is None or sleep_ms < C_MIN_SLEEP_MS:
            continue
        if not (C_SESSION_MS[0] <= awake_ms <= C_SESSION_MS[1]):
            continue
        if (r.g("lit_ms") or 0) > 0 or (r.g("wifi_ms") or 0) > 0:
            continue
        sel.append(r)
    est.n = len(sel)
    if not sel:
        est.lines.append("no overnight pair: it needs a sleep of 6 h or more after a session of 10 s to 5 min with no "
                         "light and no radio")
        return est
    est.status = "ok"
    dpct = sum(r.pair_dpct for r in sel)
    hours = sum((r.g("sleep_ms") + r.g("awake_ms")) / 3.6e6 for r in sel)
    mean = dpct / hours
    rates.mv_per_pct = statistics.median(mv_per_percent(r.g("w_mv")) for r in sel)
    sig_pair = opts.pair_sigma_mv / rates.mv_per_pct
    se = sig_pair * math.sqrt(len(sel)) / hours
    if len(sel) >= 5:
        per = [r.pair_dpct / ((r.g("sleep_ms") + r.g("awake_ms")) / 3.6e6) for r in sel]
        se = max(se, statistics.stdev(per) / math.sqrt(len(sel)))
    est.pct_h, est.se = mean, se
    est.lo, est.hi = mean - 1.96 * se, mean + 1.96 * se
    est.lines.append("%d overnight pair(s), %.0f h in all; the short session's boot, paint and tail are inside the "
                     "number, so it is an upper bound on the sleep drain" % (est.n, hours))
    _finish_rate(est, rates, opts, touch)
    return est


def entry_to_wake_diagnostic(rows: List[Row], opts: Options, rates: Rates) -> List[str]:
    """The loaded-entry-to-wake-sample step over long clean sleeps, with a band and the smallest current an entry-to-wake
    pair could have resolved. A diagnostic only: the entry reading is under the render load, so the step is positive
    whatever the sleep current was and no drain can be read from it (design 2.2 and 8, item 7)."""
    sel = [r for r in rows if r.g("k") == "S" and r.g("e_mv") is not None and r.g("w_mv") is not None
           and (r.g("sleep_ms") or 0) >= 2 * 3600 * 1000 and point_ok(r) and cycle_ok(r)]
    lines: List[str] = []
    if len(sel) < 3:
        return ["fewer than 3 clean sleeps of 2 h or more with both readings: nothing to show"]
    steps = sorted(r.g("w_mv") - r.g("e_mv") for r in sel)
    med = statistics.median(steps)
    rng = random.Random(opts.seed)
    boots = sorted(statistics.median(rng.choices(steps, k=len(steps))) for _ in range(500))
    lines.append("%d clean sleeps of 2 h or more: the wake sample sits %+.1f mV from the loaded entry reading (median, "
                 "95%% band %+.1f .. %+.1f; 5th to 95th percentile %+.1f .. %+.1f)"
                 % (len(sel), med, percentile(boots, 2.5), percentile(boots, 97.5), percentile(steps, 5),
                    percentile(steps, 95)))
    mvpp = statistics.median(mv_per_percent(r.g("w_mv")) for r in sel)
    hours = statistics.median(r.sleep_h for r in sel)
    if opts.capacity_mah > 0:
        lim = 3.0 * ENTRY_PAIR_SIGMA_MV / mvpp / 100.0 * opts.capacity_mah * 1000.0 / hours
        lines.append("smallest current an entry-to-wake pair could have resolved (3 sigma of %.0f mV over the median %.1f h "
                     "sleep at %.0f mV per percent): %s, which is why it is not used" % (
                         ENTRY_PAIR_SIGMA_MV, hours, mvpp, fmt_ua(lim)))
    else:
        lines.append("smallest fall an entry-to-wake pair could have resolved (3 sigma): %.0f mV, which is why it is "
                     "not used" % (3.0 * ENTRY_PAIR_SIGMA_MV))
    lines.append("NOT a drain estimate: the entry sits 11-25 mV low under the render load and the wake sample is partly "
                 "settled, so the step is positive whatever the sleep current was (the design's synthetic study gave "
                 "the same step for every true drain from 58 uA to 1 mA). It only tells what 'loaded' costs, and its "
                 "spread is the number to re-derive the charged-unseen threshold from (now %.0f mV)." %
                 CHG_LOADED_STEP_MV)
    return lines


# --- The report -----------------------------------------------------------------------------------------------------
@dataclass
class Flag:
    code: str
    text: str
    rows: List[int] = field(default_factory=list)


def _rowlist(rows: Sequence[Row], limit: int = 6) -> str:
    ids = ["%s" % r.g("row") for r in rows[:limit]]
    more = len(rows) - limit
    return " rows " + ",".join(ids) + (" and %d more" % more if more > 0 else "") if rows else ""


def day_key(r: Row) -> Tuple[int, int]:
    return block_key(r)


def day_name(key: Tuple[int, int]) -> str:
    seg, day = key
    if seg == -1:
        return datetime.datetime.fromtimestamp(day * 86400, datetime.timezone.utc).strftime("%Y-%m-%d")
    return "segment %d day %d" % (seg, day)


def counter_flags(rows: List[Row], opts: Options) -> List[Flag]:
    """Everything that needs no voltage inference (design section 8, item 9)."""
    flags: List[Flag] = []
    s_rows = [r for r in rows if r.g("k") == "S"]

    def add(code: str, text: str, hit: Sequence[Row]) -> None:
        if hit:
            flags.append(Flag(code, text + _rowlist(hit), [r.g("row") for r in hit]))

    per_day: Dict[Tuple[int, int], int] = {}
    for r in rows:
        per_day[day_key(r)] = per_day.get(day_key(r), 0) + (r.g("g_n") or 0) + (r.g("g_o") or 0)
    storm_days = sorted((d for d, n in per_day.items() if n > 10), key=lambda d: -per_day[d])
    if storm_days:
        names = ", ".join("%s (%d)" % (day_name(d), per_day[d]) for d in storm_days[:3])
        flags.append(Flag("WAKE_STORM", "wake storm: %d day(s) with more than 10 early wakes, worst %s"
                          % (len(storm_days), names)))
    add("GHOST_RATE", "more than one ghost wake per sleep hour (sleeps of 30 min or more, 3 or more ghosts)",
        [r for r in s_rows if (r.g("g_n") or 0) >= 3 and (r.sleep_h or 0) >= 0.5 and r.g("g_n") > r.sleep_h])
    long_early = []
    for r in rows:
        n = (r.g("g_n") or 0) + (r.g("g_o") or 0)
        if n > 0 and r.g("g_ms") is not None and r.g("g_ms") / n > opts.early_ms_limit:
            long_early.append(r)
    add("EARLY_LONG", "mean early-wake awake time above %d ms (the P5 target; CrossInk holds up to 400 ms)"
        % opts.early_ms_limit, long_early)
    add("KEY_HELD", "a key was held at entry, so a release wake happened inside that sleep (not counted)",
        [r for r in s_rows if r.has_flag("fl_e", 4)])
    add("CEILING", "sleeps ended by the awake ceiling or the input cap: something held the screen awake",
        [r for r in s_rows if r.g("path") in ("k", "i")])
    add("POCKET", "unattended wake with the front light lit: a pocket relight",
        [r for r in s_rows if r.g("path") == "w" and (r.g("lit_ms") or 0) > 0])
    over = []
    for r in s_rows:
        tmo = r.g("tmo")
        ceiling_ms = max(AWAKE_CEILING_MIN, tmo or 0) * 60000
        if not usb_evidence(r) and (r.g("idle_ms") or 0) > ceiling_ms + 120000:
            over.append(r)
    add("CEILING_MISSED", "no input for longer than the awake ceiling before the sleep, on battery (the ceiling did "
        "not fire)", over)
    long_tail = []
    for r in s_rows:
        idle, tmo = r.g("idle_ms"), r.g("tmo")
        if idle is None or usb_evidence(r) or r.g("path") in ("k", "i"):
            continue
        if idle >= LONG_TAIL_MS or (tmo and r.g("path") in ("a", "w") and idle > tmo * 60000 + 120000):
            long_tail.append(r)
    if long_tail:
        hours = sum(r.g("idle_ms") for r in long_tail) / 3.6e6
        add("LONG_TAIL", "idle tail before the sleep longer than the sleep timeout or 20 min or more, %d time(s), "
            "%.1f h in all (a tail costs 4-6.5 mAh per 10 min)" % (len(long_tail), hours), long_tail)
    add("SLOW_ENTRY", "sleep entry took more than 2 s (a plugin Wi-Fi join, or the touch retry)",
        [r for r in s_rows if (r.g("ent_ms") or 0) > 2000])
    add("TOUCH_AWAKE", "the touch controller did not accept hibernate (the 165 uA Monitor-mode risk)",
        [r for r in s_rows if r.has_flag("fl_e", 2)])
    add("SDA_LOW", "I2C SDA still low after bus recovery (about 1.5 mA in sleep)",
        [r for r in s_rows if r.has_flag("fl_e", 3)])
    add("BURST_SKIPPED", "a burst was skipped: the ADC lock was held for 20 ms or more by something else",
        [r for r in rows if r.has_flag("fl_e", 8) or r.has_flag("fl_w", 7)])
    add("SD_TROUBLE", "a slow or failed flush, the SD back-off, or the file cap (fl_w bits 6, 8, 9)",
        [r for r in rows if r.has_flag("fl_w", 6) or r.has_flag("fl_w", 8) or r.has_flag("fl_w", 9)])
    add("FILE_CAP", "the file reached its 2 MiB cap: logging stopped, move or delete battery.csv",
        [r for r in rows if r.has_flag("fl_w", 9)])
    usb_heavy = []
    for r in s_rows:
        a, u = r.g("awake_ms") or 0, r.g("usb_ms") or 0
        if a >= 60000 and u >= 0.9 * a:
            usb_heavy.append(r)
    if len(usb_heavy) >= 5 and len(usb_heavy) * 2 >= len(s_rows):
        add("USB_ALL_SESSION", "USB present for nearly the whole session in half the cycles or more: if no cable was "
            "plugged in, this is the HW-08 signature (the clamp reads as a charger)", usb_heavy)
    add("ABNORMAL_RESET", "abnormal resets (panic, watchdog, brown-out, lock-up) since the previous row",
        [r for r in rows if (r.g("ab") or 0) > 0])
    add("ORPHAN", "orphan boot rows (k = B): a power loss, a flash or a lost record, no cycle for that sleep",
        [r for r in rows if r.g("k") == "B"])
    add("GAP", "missing rows or dropped rows (the SD back-off, a failed write, a lost record)",
        [r for r in rows if "GAP" in r.excl])
    sdw = sorted(r.g("sdw_ms") for r in rows if r.g("sdw_ms") is not None)
    if len(sdw) >= 5 and percentile(sdw, 95) > 100:
        flags.append(Flag("SDW_SLOW", "the 95th percentile of the SD window is %.0f ms (above 100 ms)"
                          % percentile(sdw, 95)))
    add("CUTOFF_LOW", "cutoff sleeps whose wake voltage is below 3500 mV",
        [r for r in s_rows if r.g("path") == "b" and r.g("w_mv") is not None and r.g("w_mv") < 3500.0])
    return flags


def flag_bit_counts(rows: List[Row]) -> List[str]:
    """How many rows carry each fl_e and fl_w bit."""
    out: List[str] = []
    for column, table, label in (("fl_e", ENTRY_FLAGS, "entry flags (fl_e)"), ("fl_w", WAKE_FLAGS, "wake flags (fl_w)")):
        hits = [(name, sum(1 for r in rows if r.has_flag(column, bit))) for name, bit in table.items()]
        hits = [(n, c) for n, c in hits if c]
        if hits:
            out.append("%s set on: %s" % (label, ", ".join("%s x%d" % h for h in hits)))
    return out


def burst_quality(rows: List[Row]) -> List[str]:
    out: List[str] = []
    e_sd = sorted(r.g("e_sd") for r in rows if r.g("e_sd") is not None)
    w_sd = sorted(r.g("w_sd") for r in rows if r.g("w_sd") is not None)
    for name, vals in (("entry", e_sd), ("wake", w_sd)):
        if vals:
            med = statistics.median(vals)
            out.append("%s burst: single-conversion sd median %.1f mV, 95th percentile %.1f mV at P+ (the mean of 64 "
                       "conversions is about %.2f mV)" % (name, med, percentile(vals, 95), med / 8.0))
    zeros = sum(1 for r in rows if (r.g("e_z") or 0) > 0 or (r.g("w_z") or 0) > 0)
    out.append("rows with zero conversions (the ADC's silent failure): %d" % zeros)
    pts: List[Tuple[float, float]] = []  # (pin mV, calibrated mV per raw count), in row order
    for r in rows:
        for mv, raw in ((r.g("e_mv"), r.g("e_raw")), (r.g("w_mv"), r.g("w_raw"))):
            if mv is not None and raw:
                pts.append((mv / 2.0, (mv / 2.0) / (raw / 16.0)))
    if len(pts) >= 6:
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        mx, my = statistics.mean(xs), statistics.mean(ys)
        sxx = sum((x - mx) ** 2 for x in xs)
        slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx if sxx > 0 else 0.0
        icpt = my - slope * mx
        resid = [(y - (icpt + slope * x)) / (icpt + slope * x) * 100.0 for x, y in pts]
        third = max(len(resid) // 3, 1)
        drift = statistics.mean(resid[-third:]) - statistics.mean(resid[:third])
        off = sum(1 for v in resid if abs(v) > 2.0)
        out.append("calibrated mV per raw count at the pin: median %.4f; with the trend over the voltage taken out the "
                   "scatter is %.2f %%, the last third differs from the first by %+.2f %% (calibration drift), %d "
                   "burst(s) are off by more than 2 %%" % (statistics.median(ys), statistics.pstdev(resid), drift, off))
    locks = sum(1 for r in rows if r.has_flag("fl_e", 8) or r.has_flag("fl_w", 7))
    out.append("bursts skipped for the ADC lock: %d row(s)" % locks)
    return out


def display_summary(rows: List[Row]) -> Tuple[List[str], Dict[str, int]]:
    """What the screen showed against what the volts say, and the display's own noise floor."""
    out: List[str] = []
    cyc = [r for r in rows if r.g("k") == "S" and r.disp]
    counts = Counter(r.disp for r in cyc)
    out.append("%d cycle(s) with both screen readings: %d steady, %d one-step flips on a change under 1 %% (rounding "
               "and load, not drain), %d real steps of 1 %% or more" % (len(cyc), counts["steady"], counts["rounding"],
                                                                        counts["step"]))
    bias = []
    for r in cyc:
        w_mv = r.g("w_mv")
        if w_mv is not None and r.g("w_ui") is not None:
            bias.append(r.g("w_ui") / 10.0 - percent(w_mv))
    if bias:
        out.append("the first screen after a wake reads %+.1f %% against the wake volts' table percent (median; the "
                   "render load sags the reading)" % statistics.median(bias))
    steps = []
    for r in cyc:
        if r.g("e_mv") is not None and r.g("w_mv") is not None and r.g("sleep_ms") is not None \
                and r.g("sleep_ms") >= 2 * 3600 * 1000 and "USB" not in r.excl and "BOOT" not in r.excl:
            steps.append(r.g("w_mv") - r.g("e_mv"))
    if steps:
        steps.sort()
        out.append("loaded entry to wake sample over %d sleeps of 2 h or more: median %+.1f mV (5th to 95th "
                   "percentile %+.1f to %+.1f). Display only, never drain: the entry reading sits under the render "
                   "load and relaxes upward, so no sleep current can be read from it (the design's table A gives the "
                   "entry-to-wake pair 7 mV of scatter, 21 mV at 3 sigma)"
                   % (len(steps), statistics.median(steps), percentile(steps, 5), percentile(steps, 95)))
    quick = [r for r in cyc if (r.g("sleep_ms") or 0) + (r.g("awake_ms") or 0) < 10 * 60 * 1000
             and (r.g("sleep_ms") is not None) and "USB" not in r.excl]
    if quick:
        flips = sum(1 for r in quick if ui_pct(r.g("w_ui")) != ui_pct(r.g("e_ui")))
        d = [(r.g("w_ui") - r.g("e_ui")) / 10.0 for r in quick]
        out.append("display noise floor: %d cycle(s) with sleep plus session under 10 min, the shown percent changed "
                   "in %d of them (standard deviation of the 0.1 %% change %.2f %%): a flip with no time elapsed is "
                   "display noise" % (len(quick), flips, statistics.pstdev(d)))
    else:
        out.append("display noise floor: no cycle with sleep plus session under 10 min (the six-sleeps-in-ten-minutes "
                   "test has not been logged)")
    return out, dict(counts)


def display_cases(rows: List[Row], limit: int = 8) -> List[str]:
    """The cycles where the shown percent changed, with the volts and what else happened in the session (the 83-to-82
    question): the most recent ones."""
    cases = [r for r in rows if r.disp in ("rounding", "step")]
    out: List[str] = []
    for r in cases[-limit:]:
        e_ui, w_ui = r.g("e_ui"), r.g("w_ui")
        text = "row %s: shown %d -> %d" % (r.g("row"), ui_pct(e_ui), ui_pct(w_ui))
        if r.g("e_mv") is not None and r.g("w_mv") is not None:
            text += ", volts table %.1f -> %.1f %% (%+.1f mV)" % (percent(r.g("e_mv")), percent(r.g("w_mv")),
                                                                   r.g("w_mv") - r.g("e_mv"))
        hints = []
        idle = r.g("idle_ms") or 0
        if idle >= 5 * 60000:
            hints.append("a %.0f min idle tail before the sleep" % (idle / 60000.0))
        lit = r.g("lit_ms") or 0
        if lit >= 60000:
            hints.append("light on %.0f min (it sags the reading 30-45 mV while on)" % (lit / 60000.0))
        elif r.has_flag("fl_e", 0):
            hints.append("light lit at the trigger")
        if (r.g("wifi_ms") or 0) >= 60000:
            hints.append("radio on %.0f min" % (r.g("wifi_ms") / 60000.0))
        hints.append("ended by %s" % PATH_NAMES.get(r.g("path") or "", "an unknown path"))
        change = (w_ui - e_ui) / 10.0
        if r.disp == "rounding":
            verdict = "one flip on a change of %.1f %%: rounding and load, not drain evidence" % abs(change)
        else:
            verdict = "a step of %+.1f %%" % change
        out.append("%s; %s; %s" % (text, "; ".join(hints), verdict))
    return out


def split_runs(rows: List[Row]) -> List[List[Row]]:
    """Runs between charges: a new run at a segment change and after every USB, charged-unseen or orphan row (which is
    in no run)."""
    runs: List[List[Row]] = []
    cur: List[Row] = []
    seg = None
    for r in rows:
        bad = any(c in r.excl for c in ("BOOT", "USB", "CHG_UNSEEN"))
        if (r.seg != seg or bad) and cur:
            runs.append(cur)
            cur = []
        seg = r.seg
        if not bad:
            cur.append(r)
    if cur:
        runs.append(cur)
    return runs


def run_summaries(rows: List[Row], opts: Options, rates: Rates, touch: bool = True,
                  shown: int = 8) -> Tuple[List[str], List[str]]:
    """The runs split at charges: the table percent at each end, the slope, and estimator A on a run that is long
    enough. Returns (lines, step lines): a step is two neighbouring runs whose sleep-drain bands do not overlap."""
    sub = replace(opts, resamples=min(opts.resamples, 300))
    entries = []
    for run in split_runs(rows):
        pts = [r for r in run if point_ok(r) and r.rtc_s is not None]
        if len(pts) < 3 or (pts[-1].rtc_s - pts[0].rtc_s) < 6 * 3600.0:
            continue
        hours = (pts[-1].rtc_s - pts[0].rtc_s) / 3600.0
        t = [(r.rtc_s - pts[0].rtc_s) / 86400.0 for r in pts]
        y = [percent(r.g("w_mv")) for r in pts]
        tm, ym = statistics.mean(t), statistics.mean(y)
        sxx = sum((a - tm) ** 2 for a in t)
        if sxx <= 0:
            continue
        slope = sum((a - tm) * (b - ym) for a, b in zip(t, y)) / sxx  # percent per day, negative when discharging
        est = fit_a([r for r in run if a_ok(r)], sub, Rates(opts.capacity_mah, 8.0), touch, "run", "",
                    min_blocks=RUN_MIN_BLOCKS)
        entries.append((pts, hours, y, slope, est if est.status == "ok" and est.lo is not None else None))
    if not entries:
        return ["no run long enough (3 clean wake samples over 6 h or more)"], []
    out: List[str] = []
    if len(entries) > shown:
        out.append("(%d earlier run(s) not listed)" % (len(entries) - shown))
    fmt = Rates(opts.capacity_mah, 8.0)
    for pts, hours, y, slope, est in entries[-shown:]:
        mah = "" if opts.capacity_mah <= 0 else " (%.1f mAh/day)" % (-slope / 100.0 * opts.capacity_mah)
        line = "run from row %s: %d wake samples over %.1f days, %.1f %% to %.1f %%, %+.2f %%/day%s" % (
            pts[0].g("row"), len(pts), hours / 24.0, y[0], y[-1], slope, mah)
        if est is not None:
            line += "; sleep estimate %s [%s .. %s]" % (fmt.fmt(est.pct_h), fmt.fmt(est.lo), fmt.fmt(est.hi))
        out.append(line)
    steps: List[str] = []
    for (pa, _, _, _, ea), (pb, _, _, _, eb) in zip(entries, entries[1:]):
        if ea is not None and eb is not None and (ea.hi < eb.lo or eb.hi < ea.lo):
            steps.append("STEP: the sleep estimate differs between the run from row %s and the run from row %s, the bands "
                         "do not overlap (only meaningful at the same temperature class; the log has no temperature)"
                         % (pa[0].g("row"), pb[0].g("row")))
    return out, steps


# --- The per-cycle table ----------------------------------------------------------------------------------------------
def time_str(r: Row) -> str:
    if r.wall is not None:
        stamp = datetime.datetime.fromtimestamp(r.wall, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        return stamp if r.wall_src == "utc" else stamp[:-1] + "~"
    days, rem = divmod(int(r.rel_s), 86400)
    return "s%d+%dd%02d:%02d" % (r.seg, days, rem // 3600, rem % 3600 // 60)


def woke_code(r: Row) -> str:
    rst, wk = r.g("rst"), r.g("wk")
    if rst == 8:
        return {"h": "held", "c": "click", "o": "other"}.get(wk or "", "?")
    return {1: "power", 3: "sw", 4: "panic", 5: "wdt", 6: "wdt", 7: "wdt", 9: "brown", 15: "lockup"}.get(
        rst, "rst%s" % rst)


CYCLE_COLUMNS = (
    "seg", "row", "k", "fw", "time", "time_src", "sleep_h", "awake_min", "path", "ended", "woke", "rst", "early_wakes",
    "early_ms", "lit_min", "lit_pct", "wifi_min", "full_min", "usb_min", "idle_min", "e_mv", "w_mv", "loaded_to_wake_mv",
    "ui_e", "ui_w", "ui_step", "table_e", "table_w", "display", "pair_dpct", "pair_mah", "pair_h", "mv_per_day",
    "excl", "flags",
)


def cycle_record(r: Row, capacity: float) -> Dict[str, object]:
    def mins(v):
        return "" if v is None else round(v / 60000.0, 2)

    e_mv, w_mv = r.g("e_mv"), r.g("w_mv")
    ui_e, ui_w = ui_pct(r.g("e_ui")), ui_pct(r.g("w_ui"))
    mvday = ""
    if r.pair_prev is not None and r.pair_h:
        mvday = round((r.pair_prev.g("w_mv") - w_mv) / r.pair_h * 24.0, 2)
    fl = []
    for name, bit in ENTRY_FLAGS.items():
        if r.has_flag("fl_e", bit):
            fl.append("e:" + name)
    for name, bit in WAKE_FLAGS.items():
        if r.has_flag("fl_w", bit):
            fl.append("w:" + name)
    return {
        "seg": r.seg, "row": r.g("row"), "k": r.g("k"), "fw": r.g("fw"), "time": time_str(r), "time_src": r.wall_src,
        "sleep_h": "" if r.sleep_h is None else round(r.sleep_h, 3),
        "awake_min": "" if r.awake_min is None else round(r.awake_min, 2),
        "path": r.g("path") or "", "ended": PATH_NAMES.get(r.g("path") or "", ""), "woke": woke_code(r),
        "rst": r.g("rst"), "early_wakes": (r.g("g_n") or 0) + (r.g("g_o") or 0), "early_ms": r.g("g_ms"),
        "lit_min": mins(r.g("lit_ms")), "lit_pct": r.g("lit_pct"), "wifi_min": mins(r.g("wifi_ms")),
        "full_min": mins(r.g("full_ms")), "usb_min": mins(r.g("usb_ms")), "idle_min": mins(r.g("idle_ms")),
        "e_mv": "" if e_mv is None else e_mv, "w_mv": "" if w_mv is None else w_mv,
        "loaded_to_wake_mv": "" if e_mv is None or w_mv is None else round(w_mv - e_mv, 1),
        "ui_e": "" if ui_e is None else ui_e, "ui_w": "" if ui_w is None else ui_w,
        "ui_step": "" if ui_e is None or ui_w is None else ui_w - ui_e,
        "table_e": "" if e_mv is None else round(percent(e_mv), 1),
        "table_w": "" if w_mv is None else round(percent(w_mv), 1), "display": r.disp,
        "pair_dpct": "" if r.pair_dpct is None else round(r.pair_dpct, 3),
        "pair_mah": "" if r.pair_dpct is None or capacity <= 0 else round(r.pair_dpct / 100.0 * capacity, 3),
        "pair_h": "" if r.pair_h is None else round(r.pair_h, 3), "mv_per_day": mvday,
        "excl": ";".join(r.excl), "flags": ";".join(fl),
    }


def write_cycles_csv(path: Path, rows: List[Row], capacity: float) -> None:
    with open(path, "w", newline="", encoding="ascii", errors="replace") as fh:
        wr = csv.DictWriter(fh, fieldnames=list(CYCLE_COLUMNS))
        wr.writeheader()
        for r in rows:
            wr.writerow(cycle_record(r, capacity))


def format_table(rows: List[Row], limit: int, capacity: float) -> List[str]:
    head = " seg   row %-21s %7s %7s %3s %-5s %3s %5s %5s %5s %9s %11s %6s %7s  %s" % (
        "time", "sleep_h", "awake_m", "end", "woke", "gh", "lit_m", "wifi", "usb_m", "ui e>w", "table% e>w", "dW_mV",
        "pair%", "excluded")
    out = [head]
    show = rows if limit <= 0 else rows[-limit:]
    if len(show) < len(rows):
        out.append("  (the last %d of %d rows; --rows 0 shows all, --out DIR writes every row)" % (len(show), len(rows)))
    for r in show:
        c = cycle_record(r, capacity)

        def f(v, fmt):
            return "-" if v == "" or v is None else fmt % v

        ui = "-" if c["ui_e"] == "" or c["ui_w"] == "" else "%s>%s" % (c["ui_e"], c["ui_w"])
        tb = "-" if c["table_e"] == "" else "%.1f>%s" % (c["table_e"], f(c["table_w"], "%.1f"))
        out.append("%4d %5s %-21s %7s %7s %3s %-5s %3s %5s %5s %5s %9s %11s %6s %7s  %s" % (
            r.seg, r.g("row"), time_str(r), f(c["sleep_h"], "%.2f"), f(c["awake_min"], "%.1f"), c["path"] or "-",
            c["woke"], c["early_wakes"], f(c["lit_min"], "%.1f"), f(c["wifi_min"], "%.1f"), f(c["usb_min"], "%.1f"),
            ui, tb, f(c["loaded_to_wake_mv"], "%+.1f"), f(c["pair_dpct"], "%+.2f"), " ".join(r.excl) or "ok"))
    return out


# --- The analysis and the text report ---------------------------------------------------------------------------------
@dataclass
class Analysis:
    opts: Options
    ds: Dataset
    segs: List[Segment]
    touch: Optional[bool]
    estimates: List[Estimate]
    flags: List[Flag]
    voltage_flags: List[str]
    source_problems: List[str]
    rates: Rates
    run_lines: List[str] = field(default_factory=list)
    diag_lines: List[str] = field(default_factory=list)


def analyze(paths: Sequence[str], opts: Optional[Options] = None) -> Analysis:
    opts = opts or Options()
    ds = load_files(paths)
    return analyze_dataset(ds, opts)


def analyze_dataset(ds: Dataset, opts: Options) -> Analysis:
    segs = prepare(ds, opts.charge_hold_h)
    touch = touch_fitted(ds.rows)
    rates = Rates(opts.capacity_mah, 8.0)
    t = True if touch is None else touch
    estimates = [estimator_a(ds.rows, opts, rates, t), estimator_b(ds.rows, opts, rates, t),
                 estimator_c(ds.rows, opts, rates, t)]
    flags = counter_flags(ds.rows, opts)
    vflags = voltage_flags(estimates, opts, t)
    run_lines, steps = run_summaries(ds.rows, opts, rates, t)
    vflags.extend(steps)
    diag = entry_to_wake_diagnostic(ds.rows, opts, rates)
    problems = check_against_sources(opts.sdk_dir)
    return Analysis(opts, ds, segs, touch, estimates, flags, vflags, problems, rates, run_lines, diag)


def voltage_flags(estimates: List[Estimate], opts: Options, touch: bool) -> List[str]:
    out: List[str] = []
    cap = opts.capacity_mah
    if cap <= 0:
        return out
    watch = watch_threshold_ua(cap, touch)
    for est in estimates:
        if est.lo_ua is None or est.ceiling:
            continue  # a ceiling's lower end is no evidence of a sleep current
        name = est.key
        if est.lo_ua > 1000.0:
            out.append("HIGH: estimator %s's lower bound is %s, above 1 mA" % (name, fmt_ua(est.lo_ua)))
        elif est.lo_ua > meas_threshold_ua(cap):
            out.append("RUN MEAS-01: estimator %s's lower bound is %s, above %s" % (name, fmt_ua(est.lo_ua),
                                                                                    fmt_ua(meas_threshold_ua(cap))))
        elif est.lo_ua > watch:
            out.append("WATCH: estimator %s's lower bound is %s, above %s (%s)"
                       % (name, fmt_ua(est.lo_ua), fmt_ua(watch), "touch fitted" if touch else "no touch"))
    return out


LIMITS_TEXT = (
    "What a voltage log cannot do: it cannot tell 58 uA from 113 uA of sleep current, nor board drain from the cell's "
    "own self-discharge (2-3 % a month is 14-21 uA at 500 mAh), and it cannot see pad states, back-feed or a release "
    "wake. Every band above carries the notch table's scale error. Only a microammeter in series on J5 (MEAS-01, "
    "POWER.md) sees the floor itself; on an instrumented board the meter's burden voltage corrupts the battery "
    "readings, so never log data on it.")


def summary_lines(a: Analysis) -> List[str]:
    """The plain-text summary at the top of the report."""
    rows = a.ds.rows
    st = a.ds.stats
    out: List[str] = []
    s_rows = [r for r in rows if r.g("k") == "S"]
    walls = [r.wall for r in rows if r.wall is not None]
    span = ""
    if walls:
        span = ", %s to %s" % (time_str(min(rows, key=lambda r: r.wall if r.wall is not None else 1e18)),
                               time_str(max(rows, key=lambda r: r.wall if r.wall is not None else -1.0)))
    out.append("%d rows (%d cycles, %d orphan boots) in %d segment(s)%s" % (len(rows), len(s_rows),
                                                                          len(rows) - len(s_rows), len(a.segs), span))
    damaged = st.bad_crc + st.torn + st.torn_tail + st.bad_field
    out.append("file: %s" % ("every line valid" if damaged == 0 else "%d damaged line(s) dropped, %d row(s) recovered "
                                                                      "from glued lines" % (damaged, st.glued_rows)))
    pairs = sum(1 for r in rows if r.pair_prev is not None)
    counts = Counter(c for r in rows for c in r.excl if c in POINT_CODES or c in CYCLE_CODES or c == "SHORT_A")
    top = ", ".join("%s %d" % (c, n) for c, n in counts.most_common(3))
    out.append("%d usable wake-to-wake pairs%s" % (pairs, "; most common exclusions: " + top if top else ""))
    for est in a.estimates:
        if est.status != "ok":
            out.append("estimator %s: no estimate (%s)" % (est.key, est.lines[0] if est.lines else "no data"))
            continue
        line = "estimator %s: %s" % (est.key, est.head)
        if est.lo is not None and not est.ceiling:
            line += "; vs zero %s" % ("resolved" if est.resolved_zero else "not resolved")
            if a.opts.capacity_mah > 0:
                line += ", vs the expected floor %s" % ("resolved" if est.resolved_floor else "not resolved")
        out.append(line)
    out.append("counter flags: %s" % (", ".join(f.code for f in a.flags) if a.flags else "none"))
    codes = Counter(v.split(":")[0] for v in a.voltage_flags)
    out.append("voltage flags: %s" % (", ".join("%s x%d" % (k, n) if n > 1 else k for k, n in codes.items())
                                       if codes else "none"))
    return out


def render_report(a: Analysis) -> str:
    o = a.opts
    ds = a.ds
    st = ds.stats
    out: List[str] = []
    w = out.append
    w("Silkscreen battery log report")
    w("=" * 30)
    w("files: " + ", ".join(os.path.basename(f.path) for f in ds.files))
    cap = "%.0f mAh" % o.capacity_mah if o.capacity_mah > 0 else "unknown (rates in mV per day at the band)"
    w("capacity: %s%s" % (cap, "" if o.capacity_mah != 500.0 else " (the default; --capacity-mah overrides)"))
    w("")
    if ds.rows:
        w("Summary")
        w("-" * 7)
        out.extend(summary_lines(a))
        w("")
    w("1. File health")
    w("-" * 14)
    w("rows: %d good, %d bad CRC, %d torn (%d at the end of a file), %d glued line(s) holding %d recovered row(s), "
      "%d with an unparseable field" % (st.good, st.bad_crc, st.torn + st.torn_tail, st.torn_tail, st.glued_lines,
                                         st.glued_rows, st.bad_field))
    w("lines: %d total, %d comment, %d header, %d blank; %d duplicate row(s) dropped"
      % (st.lines, st.comment, st.headers, st.blank, st.duplicates))
    for warn in ds.warnings:
        w("warning: " + warn)
    for prob in a.source_problems:
        w("WARNING: " + prob)
    if ds.families:
        fam = ", ".join("%s m%d w%d ser%d: %d rows" % (k[0], k[1], k[2], k[3], n) for k, n in ds.families.most_common())
        w("builds: " + fam)
    shas = Counter(r.g("fw") for r in ds.rows if r.g("fw"))
    if shas:
        w("distinct ELF builds in the rows: %d (%s)" % (len(shas), ", ".join(k for k, _ in shas.most_common(5))))
    size_top = max((f.size for f in ds.files), default=0)
    if size_top >= FILE_CAP_BYTES:
        w("a file is at the 2 MiB cap")
    rows = ds.rows
    if not rows:
        w("")
        w("no valid row: nothing to report. Copy battery.csv from the card again (USB Drive or the web file manager).")
        return "\n".join(out) + "\n"
    w("")
    w("2. Segments and the timeline")
    w("-" * 28)
    for s in a.segs:
        r0, r1 = s.rows[0], s.rows[-1]
        span = ((r1.rtc_s or 0) - (r0.rtc_s or 0)) / 3600.0
        kind = s.reason
        walls = ""
        if s.synced:
            walls = ", wall %s to %s" % (time_str(r0), time_str(r1))
        w("segment %d: %d rows, began at row %s (%s), %.1f h on the RTC axis%s%s"
          % (s.id, len(s.rows), r0.g("row"), kind, span, walls,
             "" if s.synced else ", no utc: relative times only"))
        for step, between_h, ppm in s.drift:
            w("  clock step %+.0f s between syncs %.1f h apart: the RC clock ran %s by about %.0f ppm (%.1f %%)"
              % (step, between_h, "slow" if step > 0 else "fast", abs(ppm), abs(ppm) / 1e4))
        if s.bad_utc:
            w("  %d utc value(s) ignored (before 2025 or no rtc_ms)" % s.bad_utc)
    gaps = sum(1 for r in rows if "GAP" in r.excl)
    w("segments break at orphan boots (k = B), a backwards rtc_ms or a restarted row number; no pair crosses one. "
      "row gaps or drops: %d" % gaps)
    w("")
    w("3. Cycles")
    w("-" * 9)
    s_rows = [r for r in rows if r.g("k") == "S"]
    w("%d cycle row(s), %d orphan boot row(s)" % (len(s_rows), len(rows) - len(s_rows)))
    sl = sorted(r.sleep_h for r in s_rows if r.sleep_h is not None)
    aw = sorted(r.awake_min for r in s_rows if r.awake_min is not None)
    if sl:
        w("sleep: median %.2f h, longest %.1f h, total %.1f h" % (statistics.median(sl), sl[-1], sum(sl)))
    if aw:
        w("session: median %.1f min, longest %.1f min, total %.1f h" % (statistics.median(aw), aw[-1], sum(aw) / 60.0))
    idle = sorted(r.g("idle_ms") / 60000.0 for r in s_rows if r.g("idle_ms") is not None)
    if idle:
        w("idle tail before the sleep: median %.1f min, longest %.1f min, total %.1f h" % (
            statistics.median(idle), idle[-1], sum(idle) / 60.0))
    ended = Counter(r.g("path") for r in s_rows if r.g("path"))
    w("ended by: " + ", ".join("%s %d" % (PATH_NAMES.get(k, k), n) for k, n in ended.most_common()))
    woke = Counter(woke_code(r) for r in rows)
    w("woke: " + ", ".join("%s %d" % (k, n) for k, n in woke.most_common()))
    early = sum((r.g("g_n") or 0) + (r.g("g_o") or 0) for r in rows)
    w("early wakes (ghost and cutoff re-sleeps) folded into the rows: %d" % early)
    w("front light %.1f h, radio %.1f h, full clock %.1f h, USB %.1f h over all sessions"
      % tuple(sum((r.g(c) or 0) for r in s_rows) / 3.6e6 for c in ("lit_ms", "wifi_ms", "full_ms", "usb_ms")))
    w("")
    w(("the last %d rows" % o.rows_shown) if o.rows_shown > 0 else "all rows")
    out.extend(format_table(rows, o.rows_shown, o.capacity_mah))
    w("")
    w("4. Display versus volts")
    w("-" * 22)
    dlines, _ = display_summary(rows)
    out.extend(dlines)
    cases = display_cases(rows)
    if cases:
        w("the most recent cycles where the shown percent changed:")
        out.extend("  " + c for c in cases)
    w("")
    w("5. Burst quality")
    w("-" * 16)
    out.extend(burst_quality(rows))
    w("")
    w("6. Counter flags (no voltage inference)")
    w("-" * 38)
    if a.flags:
        for fl in a.flags:
            w("- %s: %s" % (fl.code, fl.text))
    else:
        w("none")
    out.extend(flag_bit_counts(rows))
    w("")
    w("7. Exclusions")
    w("-" * 13)
    counts = Counter(c for r in rows for c in r.excl)
    clean = sum(1 for r in rows if r.pair_prev is not None)
    w("%d of %d rows form a usable wake-to-wake pair with the row before them" % (clean, len(rows)))
    for code in ALL_CODES:
        if counts[code]:
            w("  %-10s %5d  %s" % (code, counts[code], CODE_TEXT[code]))
    w("")
    w("8. Sleep-drain estimates")
    w("-" * 24)
    w("Wake-to-wake pairs only (the loaded entry reading is never a drain input). Each wake sample is a partly "
      "settled reading whose bias cancels in a pair from the same path and build. Assumed scatter of a pair: %.1f mV "
      "(replace it with the residual scatter below once there are rows)." % o.pair_sigma_mv)
    for est in a.estimates:
        w("")
        w(est.title + ("" if est.status == "ok" else "  [no estimate]"))
        for line in est.lines:
            w("   " + line)
        if est.key == "A" and est.terms:
            w("   other terms of the regression (95 % band from the same bootstrap):")
            for name, (c, lo, hi) in est.terms.items():
                w("     " + _term_line(name, c, lo, hi, o.capacity_mah))
    w("")
    w("D. Loaded entry to wake sample (a diagnostic, not a drain estimator)")
    out.extend("   " + line for line in a.diag_lines)
    w("")
    w("9. Runs and flags by voltage")
    w("-" * 28)
    out.extend(a.run_lines)
    if a.voltage_flags:
        for line in a.voltage_flags:
            w("- " + line)
    else:
        w("no voltage flag: no estimate's lower bound is above the expected floor (%s)" % (
            "set a capacity to compare" if o.capacity_mah <= 0 else "see section 8 for what could have been resolved"))
    w("")
    w("10. What to do next")
    w("-" * 19)
    out.extend("- " + line for line in next_steps(a))
    w("")
    w(LIMITS_TEXT)
    return "\n".join(out) + "\n"


def _term_line(name: str, c: float, lo: float, hi: float, capacity: float) -> str:
    band = "" if math.isnan(lo) else " [%s .. %s]" % (_term_fmt(name, lo, capacity), _term_fmt(name, hi, capacity))
    return "%-8s %s%s" % (name, _term_fmt(name, c, capacity), band)


def _term_fmt(name: str, v: float, capacity: float) -> str:
    if capacity <= 0:
        return "%.4f %%%s" % (v, "" if name in ("cycle", "early") else "/h")
    ua_per = capacity * 10.0
    if name == "sleep_h":
        return fmt_ua(v * ua_per)
    if name in ("cycle", "early"):
        return "%.0f uAh" % (v * ua_per)
    return "%.2f mA" % (v * ua_per / 1000.0)


def next_steps(a: Analysis) -> List[str]:
    steps: List[str] = []
    st = a.ds.stats
    if st.bad_crc or st.torn or st.torn_tail:
        steps.append("%d line(s) failed validation: copy battery.csv off the card again if the damage is not just a "
                     "torn last row after a power loss" % (st.bad_crc + st.torn + st.torn_tail))
    codes = {f.code for f in a.flags}
    if "WAKE_STORM" in codes or "GHOST_RATE" in codes:
        steps.append("wake storms: look for a bouncing IO18, a held key or a pocket press (POWER.md, WK-01 and WK-02)")
    if "CEILING" in codes or "CEILING_MISSED" in codes:
        steps.append("sleeps ended by the ceiling or the input cap: find what kept the screen awake (a network screen, "
                     "a plugin, a Never timeout)")
    if "TOUCH_AWAKE" in codes or "SDA_LOW" in codes:
        steps.append("the touch controller or the I2C bus was left up at sleep entry: that is a sleep-current fault by "
                     "itself, whatever the voltage says")
    if "ABNORMAL_RESET" in codes:
        steps.append("abnormal resets: capture the serial log of the next one (panic, watchdog, brown-out)")
    if "USB_ALL_SESSION" in codes:
        steps.append("USB present for whole sessions: if no cable was in, the HW-08 clamp is reading as a charger; "
                     "check with the meter")
    a_est = a.estimates[0]
    if a_est.status != "ok":
        steps.append("keep logging: estimator A needs %d eligible cycles over %d days" % (MIN_A_CYCLES, MIN_A_BLOCKS))
    elif not a_est.resolved_zero:
        steps.append("keep logging for 30 to 90 days in ordinary use, or run one deliberate bookend: wake, let the page "
                     "paint, sleep within 10 s, leave it untouched for 3 days at 80-90 %")
    if a_est.status == "ok" and not a_est.resolved_floor:
        steps.append("an excess over the expected floor smaller than about 130-210 uA is invisible to the log until "
                     "one meter run (MEAS-01) anchors the scale term: use the DMM recipe in BATTERY-LOG.md on a spare "
                     "board and do not log on it")
    if not any(r.pair_prev is not None for r in a.ds.rows):
        steps.append("no cycle formed a clean wake-to-wake pair: see the exclusion counts (USB, short sleeps and "
                     "orphan boots are the usual reasons)")
    if not steps:
        steps.append("nothing is flagged; keep logging")
    return steps


# --- Command line -----------------------------------------------------------------------------------------------------
def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description="Report on a Silkscreen battery.csv (silkscreen-batlog image).")
    ap.add_argument("files", nargs="+", help="battery.csv (several files are merged, e.g. one moved aside at the cap)")
    ap.add_argument("--capacity-mah", type=float, default=500.0,
                    help="cell capacity in mAh (default 500; 0 reports mV per day at the band instead of microamps)")
    ap.add_argument("--out", metavar="DIR", help="write battery_cycles.csv and battery_report.txt here")
    ap.add_argument("--rows", type=int, default=30, help="rows of the cycle table to print (0 = all; default 30)")
    ap.add_argument("--resamples", type=int, default=2000, help="bootstrap resamples for estimator A (default 2000)")
    ap.add_argument("--seed", type=int, default=20260930, help="bootstrap seed")
    ap.add_argument("--pair-sigma-mv", type=float, default=PAIR_SIGMA_MV,
                    help="assumed scatter of a wake-to-wake pair difference in mV (default %.1f)" % PAIR_SIGMA_MV)
    ap.add_argument("--early-ms-limit", type=int, default=300,
                    help="mean early-wake awake time that raises a flag (default 300; CrossInk holds up to 400)")
    ap.add_argument("--charge-hold-h", type=float, default=12.0,
                    help="hours after a charge during which wake samples are excluded (default 12)")
    ap.add_argument("--sdk-dir", help="freeink-sdk checkout to compare the column and notch tables with "
                                      "(default: the one next to this script, if present)")
    return ap


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if args.capacity_mah < 0 or args.resamples < 0 or args.pair_sigma_mv <= 0:
        print("error: --capacity-mah and --resamples must not be negative and --pair-sigma-mv must be positive",
              file=sys.stderr)
        return 2
    for p in args.files:
        if not os.path.isfile(p):
            print("error: no such file: %s" % p, file=sys.stderr)
            return 2
    sdk = Path(args.sdk_dir) if args.sdk_dir else default_sdk_dir()
    opts = Options(capacity_mah=args.capacity_mah, pair_sigma_mv=args.pair_sigma_mv, resamples=args.resamples,
                   seed=args.seed, early_ms_limit=args.early_ms_limit, charge_hold_h=args.charge_hold_h,
                   rows_shown=args.rows, sdk_dir=sdk if sdk.is_dir() else None)
    analysis = analyze(args.files, opts)
    text = render_report(analysis)
    sys.stdout.write(text)
    if args.out:
        out_dir = Path(args.out)
        out_dir.mkdir(parents=True, exist_ok=True)
        write_cycles_csv(out_dir / "battery_cycles.csv", analysis.ds.rows, opts.capacity_mah)
        (out_dir / "battery_report.txt").write_text(text, encoding="ascii", errors="replace")
    return 0 if analysis.ds.rows else 1


if __name__ == "__main__":
    sys.exit(main())
