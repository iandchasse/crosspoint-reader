#!/usr/bin/env python3
"""Tests of battery_log_report.py: python -m unittest discover -s scripts/silkscreen -p "test_battery_log_report.py".

The synthetic CSVs are built here in a temp dir with the firmware's framing (a schema line with no trailing LF, "# fw"
comment lines, every row preceded by its own LF and closed by its CRC-16). The anchor is the design's example row,
copied from the SDK's test_batterylog_core.cpp: its CRC must come out of the Python CRC, and when the SDK is next to
this script the test also reads that file and the header's column table and compares them with the analyzer's copies.
"""

import binascii
import contextlib
import copy
import io
import itertools
import random
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import battery_log_report as ba  # noqa: E402

SDK = Path(__file__).resolve().parents[2] / "freeink-sdk"
CORE_H = ba.core_header_path(SDK)
CORE_TEST = SDK / "libs" / "hardware" / "PowerManager" / "test" / "host" / "test_batterylog_core.cpp"
MONITOR_CPP = ba.battery_monitor_path(SDK)

# The design's example row exactly as test_batterylog_core.cpp's exampleStory() formats it (without the leading LF).
EXAMPLE_ROW = (
    "1,57,S,f3a9c01d2,1,412345678,1791234567,32412345,1536000,a,3997.0,5.2,24,0,36784,826,0,b,0000,"
    "2310,2,0,640,,,4006.5,4.7,22,0,36871,818,312,0,b,0000,8,h,0,,0d,10,600000,1,720000,40,0,410000,"
    "118,,0,41,0,c59d"
)
# The orphan row of the design's section 4.4 (an earlier draft of the layout, which still carried cfg and tmo): 53 fields.
SPEC_ORPHAN_ROW = "1,1,B,f3a9c01d2,1,5230,,,,,,,,,,,,,,,,,,,,,,,,,,,,,0001,1,,,,0d,10,,1,,,,,,,,,,ad9a"
FW = "f3a9c01d2"
FW_LINE = (FW, "1.6.5-batlog", 1, 0, 0)


def mv_at(pct):
    """The rested P+ voltage of the notch table at a percent of charge (the inverse of ba.percent)."""
    pct = min(max(pct, 0.0), 100.0)
    i = min(int(pct // 10), 9)
    return ba.NOTCH_MV[i] + (pct - 10 * i) / 10.0 * (ba.NOTCH_MV[i + 1] - ba.NOTCH_MV[i])


def row_text(fields):
    """A row in the firmware's format: the columns in order, an empty field for what is missing, the CRC-16."""
    vals = []
    for name in ba.COLUMNS[:-1]:
        v = fields.get(name, "")
        if v is None:
            v = ""
        elif name in ba.TENTHS_COLS and v != "":
            v = "%.1f" % v
        elif name in ("fl_e", "fl_w") and v != "":
            v = "%04x" % v
        elif name == "cfg" and v != "":
            v = "%02x" % v
        vals.append(str(v))
    body = ",".join(vals) + ","
    return body + "%04x" % binascii.crc_hqx(body.encode("ascii"), 0xFFFF)


def csv_text(rows, fw=FW_LINE, header=True):
    parts = [",".join(ba.COLUMNS)] if header else []
    if fw:
        parts.append("\n# fw %s %s m%d w%d ser%d" % fw)
    parts.extend("\n" + r for r in rows)
    return "".join(parts)


class Sim:
    """A cell and its log: each cycle() spends charge, then returns the row the firmware would have written."""

    def __init__(self, cap=500.0, soc=80.0, rtc_ms=60000, row=1, utc_base=None, fw=FW, noise_mv=0.0, seed=7):
        self.cap = cap
        self.soc = soc
        self.rtc_ms = rtc_ms
        self.row_no = row
        self.utc_base = utc_base
        self.fw = fw
        self.noise = noise_mv
        self.rng = random.Random(seed)
        self.rows = []

    def _n(self):
        return self.rng.gauss(0.0, self.noise) if self.noise else 0.0

    def cycle(self, sleep_h=9.0, awake_min=15.0, sleep_ua=113.0, lit_min=0.0, lit_pct=40, wifi_min=0.0, full_frac=0.2,
              path="a", usb=False, charge_to=None, jump_pct=0.0, g_n=0, g_o=0, g_mv=None, g_t=None, drop_after=None,
              fl_e=0, fl_w=0, w_dt=312, tmo=10, idle_ms=600000, extra=None, rst=8):
        awake_h = awake_min / 60.0
        full_h = awake_h * full_frac
        low_h = awake_h - full_h
        lit_h = lit_min / 60.0
        cons = (low_h * 20.0 + full_h * 60.0 + wifi_min / 60.0 * 50.0 + lit_h * ba.duty(lit_pct) * 25.0
                + sleep_ua / 1000.0 * sleep_h + 0.3 + 0.003 * (g_n + g_o))
        soc_e = self.soc - (low_h * 20.0 + full_h * 60.0) / self.cap * 100.0
        self.soc -= cons / self.cap * 100.0
        if charge_to is not None:
            self.soc = charge_to
            usb = True
        self.soc += jump_pct
        awake_ms = int(awake_min * 60000)
        sleep_ms = int(sleep_h * 3.6e6)
        self.rtc_ms += awake_ms + sleep_ms
        f = {
            "v": 1, "row": self.row_no, "k": "S", "fw": self.fw, "m": 1, "rtc_ms": self.rtc_ms,
            "utc": "" if self.utc_base is None else self.utc_base + self.rtc_ms // 1000,
            "sleep_ms": sleep_ms, "awake_ms": awake_ms, "path": path,
            "e_mv": mv_at(soc_e) - 12.0 + self._n(), "e_sd": 5.2, "e_rg": 24, "e_z": 0, "e_raw": 36784,
            "e_ui": int(round(soc_e * 10)), "usb_e": 1 if usb else 0, "chg_e": "c" if usb else "b", "fl_e": fl_e,
            "ent_ms": 2000, "g_n": g_n, "g_o": g_o, "g_ms": 320 * (g_n + g_o), "g_mv": "" if g_mv is None else g_mv,
            "g_t": "" if g_t is None else g_t,
            "w_mv": mv_at(self.soc) + self._n(), "w_sd": 4.7, "w_rg": 22, "w_z": 0, "w_raw": 36871,
            "w_ui": int(round((self.soc - 0.8) * 10)), "w_dt": w_dt, "usb_w": 1 if usb else 0,
            "chg_w": "c" if usb else "b", "fl_w": fl_w, "rst": rst, "wk": "h", "ab": 0, "ab_r": "", "cfg": 0x0D,
            "tmo": tmo, "idle_ms": idle_ms, "boots": 1, "lit_ms": int(lit_min * 60000), "lit_pct": lit_pct if lit_min else 0,
            "wifi_ms": int(wifi_min * 60000), "full_ms": int(full_h * 3.6e6), "inp": 20, "rf": "",
            "usb_ms": awake_ms if usb else 0, "sdw_ms": 41, "drop": 0,
        }
        if extra:
            f.update(extra)
        self.row_no += 1
        text = row_text(f)
        self.rows.append(text)
        return text

    def skip_rows(self, n):
        """n rows consumed without reaching the card: a gap in the row numbers."""
        self.row_no += n

    def orphan(self, rtc_ms=5230):
        """A power loss: the RTC counter restarts, the clock sync is gone, the next row is a k = B orphan."""
        self.rtc_ms = rtc_ms
        self.utc_base = None
        self.row_no = 1
        f = {"v": 1, "row": 1, "k": "B", "fw": self.fw, "m": 1, "rtc_ms": rtc_ms, "usb_w": 0, "chg_w": "b",
             "fl_w": 0x0005, "rst": 1, "wk": "o", "ab": 0, "sdw_ms": "", "drop": 0}
        self.row_no = 2
        text = row_text(f)
        self.rows.append(text)
        return text


def campaign(sim, days, sleep_ua, seed=3, recharge_below=45.0, per_day=3):
    """A few weeks of ordinary use: three cycles a day of varying length, a USB charge when the cell runs low."""
    rng = random.Random(seed)
    bases = (5.0, 10.0, 9.0)
    for _ in range(days):
        for k in range(per_day):
            kw = dict(sleep_h=bases[k] * rng.uniform(0.7, 1.3), awake_min=rng.uniform(5, 40), sleep_ua=sleep_ua,
                      full_frac=rng.uniform(0.1, 0.5))
            if rng.random() < 0.3:
                kw["lit_min"] = rng.uniform(3, 15)
                kw["lit_pct"] = rng.choice((20, 40, 60))
            if rng.random() < 0.15:
                kw["wifi_min"] = rng.uniform(2, 8)
            if sim.soc < recharge_below:
                kw["charge_to"] = 88.0
            sim.cycle(**kw)


class TempCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, name, text):
        p = self.tmp / name
        p.write_bytes(text.encode("latin-1"))
        return str(p)

    def analyze(self, text, **kw):
        kw.setdefault("resamples", 150)
        return ba.analyze([self.write("battery.csv", text)], ba.Options(**kw))


# --- The anchor: the SDK test's example row -------------------------------------------------------------------------
class ExampleRow(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(ba.crc16(b"123456789"), 0x29B1)
        self.assertEqual(ba.crc16(b""), 0xFFFF)
        self.assertEqual(ba.crc16(b"A"), 0xB915)

    def test_example_row_crc_in_python(self):
        status, fields = ba.check_row_text(EXAMPLE_ROW, 53)
        self.assertEqual(status, "ok")
        self.assertEqual(len(fields), 53)
        body = EXAMPLE_ROW[: EXAMPLE_ROW.rfind(",") + 1]
        self.assertEqual(ba.crc16(body.encode("ascii")), 0xC59D)
        self.assertEqual(fields[-1], "c59d")
        self.assertEqual(len(EXAMPLE_ROW), 205)  # the SDK test: 205 bytes of row

    def test_example_row_parses(self):
        res = ba.parse_bytes(csv_text([EXAMPLE_ROW]).encode("ascii"))
        self.assertEqual((res.stats.good, res.stats.bad_crc, res.stats.torn), (1, 0, 0))
        r = res.rows[0]
        self.assertEqual(r.g("row"), 57)
        self.assertEqual(r.g("k"), "S")
        self.assertEqual(r.g("fw"), FW)
        self.assertEqual(r.g("rtc_ms"), 412345678)
        self.assertEqual(r.g("utc"), 1791234567)
        self.assertEqual(r.g("sleep_ms"), 32412345)
        self.assertEqual(r.g("awake_ms"), 1536000)
        self.assertEqual(r.g("path"), "a")
        self.assertEqual((r.g("e_mv"), r.g("e_sd"), r.g("e_rg"), r.g("e_z"), r.g("e_raw")), (3997.0, 5.2, 24, 0, 36784))
        self.assertEqual((r.g("w_mv"), r.g("w_sd"), r.g("w_rg"), r.g("w_z"), r.g("w_raw")), (4006.5, 4.7, 22, 0, 36871))
        self.assertEqual((r.g("e_ui"), r.g("w_ui")), (826, 818))
        self.assertEqual((r.g("g_n"), r.g("g_o"), r.g("g_ms")), (2, 0, 640))
        self.assertIsNone(r.g("g_mv"))
        self.assertIsNone(r.g("g_t"))
        self.assertEqual((r.g("fl_e"), r.g("fl_w"), r.g("cfg")), (0, 0, 0x0D))
        self.assertEqual((r.g("rst"), r.g("wk"), r.g("tmo"), r.g("idle_ms")), (8, "h", 10, 600000))
        self.assertEqual((r.g("lit_ms"), r.g("lit_pct"), r.g("wifi_ms"), r.g("full_ms")), (720000, 40, 0, 410000))
        self.assertEqual((r.g("inp"), r.g("sdw_ms"), r.g("drop")), (118, 41, 0))
        self.assertIsNone(r.g("rf"))

    def test_the_designs_orphan_row(self):
        self.assertEqual(ba.check_row_text(SPEC_ORPHAN_ROW, 53)[0], "ok")
        res = ba.parse_bytes(csv_text([SPEC_ORPHAN_ROW]).encode("ascii"))
        self.assertEqual(res.stats.good, 1)
        r = res.rows[0]
        self.assertEqual((r.g("k"), r.g("row"), r.g("rtc_ms"), r.g("rst"), r.g("fl_w")), ("B", 1, 5230, 1, 1))
        self.assertIsNone(r.g("sleep_ms"))
        a = ba.analyze_dataset(ba.merge_results([res]), ba.Options(resamples=5))
        self.assertIn("BOOT", a.ds.rows[0].excl)
        self.assertEqual(len(a.segs), 1)
        self.assertEqual(a.segs[0].reason, "first row")

    def test_example_row_reads_as_the_design_says(self):
        # 83 at the trigger, 82 after the wake, while the wake burst reads 83.3 % by the table and sits 9.5 mV above the
        # loaded entry burst: one flipped integer on a change under 1 %, not drain evidence.
        res = ba.parse_bytes(csv_text([EXAMPLE_ROW]).encode("ascii"))
        r = res.rows[0]
        self.assertEqual((ba.ui_pct(r.g("e_ui")), ba.ui_pct(r.g("w_ui"))), (83, 82))
        self.assertAlmostEqual(ba.percent(r.g("w_mv")), 83.3125, places=3)
        self.assertAlmostEqual(r.g("w_mv") - r.g("e_mv"), 9.5, places=6)
        ba.classify_display(r)
        self.assertEqual(r.disp, "rounding")

    def test_same_row_as_the_sdk_test_wrote_it(self):
        if not CORE_TEST.is_file():
            self.skipTest("the SDK test file is not next to this script")
        src = CORE_TEST.read_text(encoding="utf-8")
        start = src.index("constexpr int exampleStory()")
        start = src.index("CHECK(textIs(t,", start)
        lits = []
        pos = src.index('"', start)
        while True:
            m = re.compile(r'\s*"((?:[^"\\]|\\.)*)"').match(src, pos)
            if not m:
                break
            lits.append(m.group(1))
            pos = m.end()
        text = "".join(lits).replace("\\n", "\n")
        self.assertTrue(text.startswith("\n"))
        self.assertEqual(text[1:], EXAMPLE_ROW)

    def test_header_text_as_the_sdk_test_has_it(self):
        if not CORE_TEST.is_file():
            self.skipTest("the SDK test file is not next to this script")
        src = CORE_TEST.read_text(encoding="utf-8")
        start = src.index("constexpr int headerStory()")
        start = src.index("CHECK(bufIs(t.c, t.n,", start)
        lits = []
        pos = src.index('"', start)
        while True:
            m = re.compile(r'\s*"((?:[^"\\]|\\.)*)"').match(src, pos)
            if not m:
                break
            lits.append(m.group(1))
            pos = m.end()
        self.assertEqual("".join(lits), ",".join(ba.COLUMNS))


# --- The tables the analyzer carries, against the firmware sources ---------------------------------------------------
class SourceTables(unittest.TestCase):
    def test_column_table(self):
        self.assertEqual(len(ba.COLUMNS), 53)
        self.assertEqual(len(set(ba.COLUMNS)), 53)
        self.assertEqual(ba.COLUMNS[0], "v")
        self.assertEqual(ba.COLUMNS[-1], "crc")
        if not CORE_H.is_file():
            self.skipTest("BatteryLogCore.h is not next to this script")
        text = CORE_H.read_text(encoding="utf-8")
        self.assertEqual(tuple(ba.parse_core_columns(text)), ba.COLUMNS)

    def test_flag_bits(self):
        if not CORE_H.is_file():
            self.skipTest("BatteryLogCore.h is not next to this script")
        text = CORE_H.read_text(encoding="utf-8")
        self.assertEqual(ba.parse_core_flags(text, "entryflag"), ba.ENTRY_FLAGS)
        self.assertEqual(ba.parse_core_flags(text, "wakeflag"), ba.WAKE_FLAGS)

    def test_schema_constants(self):
        if not CORE_H.is_file():
            self.skipTest("BatteryLogCore.h is not next to this script")
        text = CORE_H.read_text(encoding="utf-8")
        self.assertEqual(ba.parse_core_constant(text, "SCHEMA_VERSION"), ba.SCHEMA_VERSION)
        self.assertEqual(ba.parse_core_constant(text, "UTC_FLOOR"), ba.UTC_FLOOR)
        self.assertEqual(ba.parse_core_constant(text, "FILE_CAP_BYTES") or ba.FILE_CAP_BYTES, ba.FILE_CAP_BYTES)

    def test_notch_table_against_the_firmware_text(self):
        if not MONITOR_CPP.is_file():
            self.skipTest("BatteryMonitor.cpp is not next to this script")
        self.assertEqual(tuple(ba.parse_notch_table(MONITOR_CPP.read_text(encoding="utf-8"))), ba.NOTCH_MV)

    def test_percent_matches_the_firmware_static_asserts(self):
        # BatteryMonitor.cpp percentTenthsFromNotches() and its static_asserts.
        t = ba.percent_tenths_fw
        self.assertEqual((t(0), t(3000), t(3450)), (0, 0, 0))
        self.assertEqual((t(3680), t(3820), t(4060)), (100, 500, 900))
        self.assertEqual((t(3665), t(3755), t(4130)), (93, 250, 950))
        self.assertEqual((t(4199), t(4200), t(4350)), (999, 1000, 1000))
        for mv in range(3300, 4300, 7):
            self.assertAlmostEqual(ba.percent(float(mv)) * 10.0, t(mv), delta=1.0)  # the firmware floors the tenths

    def test_percent_and_slope(self):
        self.assertEqual(ba.percent(3000.0), 0.0)
        self.assertEqual(ba.percent(4300.0), 100.0)
        self.assertAlmostEqual(ba.percent(4020.0), 85.0)
        self.assertEqual(ba.mv_per_percent(4000.0), 8.0)  # the 80-90 % band
        self.assertEqual(ba.mv_per_percent(3750.0), 3.0)  # the 20-30 % plateau
        for pct in (5.0, 25.0, 55.5, 83.3, 99.0):
            self.assertAlmostEqual(ba.percent(mv_at(pct)), pct, places=6)

    def test_checker_is_quiet_on_the_real_tree(self):
        if not CORE_H.is_file() or not MONITOR_CPP.is_file():
            self.skipTest("the SDK is not next to this script")
        self.assertEqual(ba.check_against_sources(SDK), [])


# --- Parsing and damage ----------------------------------------------------------------------------------------------
class Damage(TempCase):
    def rows(self, n=8):
        sim = Sim(noise_mv=0.2)
        for _ in range(n):
            sim.cycle(sleep_h=8.0)
        return sim.rows

    def test_clean_file(self):
        res = ba.parse_bytes(csv_text(self.rows()).encode("ascii"))
        self.assertEqual((res.stats.good, res.stats.bad_crc, res.stats.torn, res.stats.torn_tail), (8, 0, 0, 0))
        self.assertEqual((res.stats.headers, res.stats.comment), (1, 1))
        self.assertEqual(len(res.fw_lines), 1)
        fl = res.fw_lines[0]
        self.assertEqual((fl.sha, fl.version, fl.method, fl.wait_ms, fl.serial), (FW, "1.6.5-batlog", 1, 0, 0))

    def test_torn_last_row(self):
        rows = self.rows()
        text = csv_text(rows[:-1]) + "\n" + rows[-1][:70]
        res = ba.parse_bytes(text.encode("ascii"))
        self.assertEqual((res.stats.good, res.stats.torn_tail, res.stats.torn), (7, 1, 0))

    def test_torn_row_in_the_middle_does_not_glue(self):
        # The firmware writes every row as LF plus the row, so a torn row stays on its own line.
        rows = self.rows()
        text = csv_text(rows[:3]) + "\n" + rows[3][:90] + "".join("\n" + r for r in rows[4:])
        res = ba.parse_bytes(text.encode("ascii"))
        self.assertEqual((res.stats.good, res.stats.torn, res.stats.torn_tail, res.stats.glued_lines), (7, 1, 0, 0))
        self.assertEqual([r.g("row") for r in res.rows], [1, 2, 3, 5, 6, 7, 8])

    def test_glued_rows_are_recovered(self):
        # A torn row followed by the next row with no LF between them: the next row is found by its CRC.
        rows = self.rows()
        text = csv_text(rows[:3]) + "\n" + rows[3][:90] + rows[4] + "".join("\n" + r for r in rows[5:])
        res = ba.parse_bytes(text.encode("ascii"))
        self.assertEqual(res.stats.glued_lines, 1)
        self.assertEqual(res.stats.glued_rows, 1)
        self.assertEqual(res.stats.good, 7)
        self.assertEqual([r.g("row") for r in res.rows], [1, 2, 3, 5, 6, 7, 8])
        self.assertEqual(res.rows[3].text, rows[4])

    def test_two_whole_rows_glued(self):
        rows = self.rows()
        text = csv_text(rows[:2]) + "\n" + rows[2] + rows[3] + "".join("\n" + r for r in rows[4:])
        res = ba.parse_bytes(text.encode("ascii"))
        self.assertEqual(res.stats.glued_lines, 1)
        self.assertEqual(res.stats.glued_rows, 2)
        self.assertEqual([r.g("row") for r in res.rows], [1, 2, 3, 4, 5, 6, 7, 8])

    def test_whole_row_then_torn_row_glued(self):
        rows = self.rows()
        text = csv_text(rows[:2]) + "\n" + rows[2] + rows[3][:80] + "".join("\n" + r for r in rows[4:])
        res = ba.parse_bytes(text.encode("ascii"))
        self.assertEqual([r.g("row") for r in res.rows], [1, 2, 3, 5, 6, 7, 8])
        self.assertEqual(res.stats.glued_lines, 1)

    def test_bit_error_in_a_row(self):
        rows = self.rows()
        bad = list(rows[3])
        i = bad.index("S") + 20  # inside the fixed part of the row
        bad[i] = "7" if bad[i] != "7" else "8"
        text = csv_text(rows[:3] + ["".join(bad)] + rows[4:])
        res = ba.parse_bytes(text.encode("ascii"))
        self.assertEqual((res.stats.good, res.stats.bad_crc), (7, 1))

    def test_bit_error_in_the_crc_field_and_in_a_comma(self):
        rows = self.rows()
        flip = rows[2][:-1] + ("0" if rows[2][-1] != "0" else "1")
        comma = rows[5].replace(",", "9", 1)  # one fewer column
        text = csv_text(rows[:2] + [flip] + rows[3:5] + [comma] + rows[6:])
        res = ba.parse_bytes(text.encode("ascii"))
        self.assertEqual(res.stats.bad_crc, 1)
        self.assertEqual(res.stats.torn, 1)
        self.assertEqual(res.stats.good, 6)

    def test_high_bytes_and_crlf_and_bom(self):
        rows = self.rows(4)
        text = csv_text(rows).replace("\n", "\r\n")
        res = ba.parse_bytes(b"\xef\xbb\xbf" + text.encode("ascii"))
        self.assertEqual((res.stats.good, res.stats.bad_crc), (4, 0))
        res = ba.parse_bytes(csv_text(rows[:2]).encode("ascii") + b"\n\xff\xfe1,3,S,garbage")
        self.assertEqual(res.stats.good, 2)
        self.assertEqual(res.stats.torn_tail, 1)

    def test_comment_lines_and_a_second_header_block(self):
        rows = self.rows(6)
        text = (csv_text(rows[:3]) + "\n# a note typed by hand\n# fw 0123456789 1.7.0-batlog m1 w250 ser1\n"
                + ",".join(ba.COLUMNS) + "".join("\n" + r for r in rows[3:]))
        res = ba.parse_bytes(text.encode("ascii"))
        self.assertEqual((res.stats.good, res.stats.comment, res.stats.headers), (6, 3, 2))
        self.assertEqual(len(res.fw_lines), 2)
        self.assertEqual(res.fw_lines[1].wait_ms, 250)

    def test_header_with_an_appended_column_is_read_by_name(self):
        names = list(ba.COLUMNS[:-1]) + ["newcol", "crc"]
        fields = ["1", "5", "S", FW, "1", "1000"] + [""] * (len(names) - 7) + ["X"]
        fields[names.index("newcol")] = "42"
        fields[names.index("crc")] = ""
        body = ",".join(fields[:-1]) + ","
        row = body + "%04x" % ba.crc16(body.encode("ascii"))
        res = ba.parse_bytes((",".join(names) + "\n" + row).encode("ascii"))
        self.assertEqual(res.stats.good, 1)
        self.assertEqual(res.rows[0].g("newcol"), "42")
        self.assertTrue(any("newcol" in w for w in res.warnings))

    def test_rows_without_a_header_use_the_built_in_columns(self):
        res = ba.parse_bytes(csv_text(self.rows(3), header=False).encode("ascii"))
        self.assertEqual(res.stats.good, 3)
        self.assertEqual(res.stats.no_header, 1)

    def test_damaged_header_keeps_the_previous_columns(self):
        rows = self.rows(3)
        res = ba.parse_bytes(("v,row,k,fw,GARBAGE!!\n" + rows[0] + "\n" + rows[1]).encode("ascii"))
        self.assertEqual(res.stats.bad_header, 1)
        self.assertEqual(res.stats.good, 2)

    def test_empty_and_header_only(self):
        self.assertEqual(ba.parse_bytes(b"").rows, [])
        self.assertEqual(ba.parse_bytes(",".join(ba.COLUMNS).encode("ascii")).rows, [])

    def test_merge_ignores_an_empty_file_when_ordering(self):
        sim = Sim(utc_base=1790000000)
        for _ in range(4):
            sim.cycle(sleep_h=8.0)
        old = self.write("old.csv", csv_text(sim.rows[:2]))
        new = self.write("new.csv", csv_text(sim.rows[2:]))
        empty = self.write("empty.csv", ",".join(ba.COLUMNS))
        ds = ba.load_files([new, empty, old])
        self.assertEqual([r.g("row") for r in ds.rows], [1, 2, 3, 4])
        self.assertEqual(ds.warnings, [])

    def test_merge_orders_and_drops_duplicates(self):
        sim = Sim(utc_base=1790000000)
        for _ in range(6):
            sim.cycle(sleep_h=8.0)
        old = self.write("old.csv", csv_text(sim.rows[:4]))
        new = self.write("new.csv", csv_text(sim.rows[2:]))  # overlaps the older copy by two rows
        ds = ba.load_files([new, old])  # given the wrong way round: the synced utc orders them
        self.assertEqual([r.g("row") for r in ds.rows], [1, 2, 3, 4, 5, 6])
        self.assertEqual(ds.stats.duplicates, 2)
        self.assertEqual(ds.stats.good, 6)


# --- Segments, the timeline and the clock ----------------------------------------------------------------------------
class Timeline(TempCase):
    def test_segments_break_at_orphans_backwards_rtc_and_row_restarts(self):
        sim = Sim()
        for _ in range(4):
            sim.cycle(sleep_h=8.0)
        sim.orphan()  # seg 1 starts: k = B
        for _ in range(3):
            sim.cycle(sleep_h=8.0)
        sim.rtc_ms = 1000  # the counter went backwards, row numbers go on: seg 2
        for _ in range(2):
            sim.cycle(sleep_h=8.0, sleep_ua=113)
        sim.row_no = 1  # restarted row numbers with a larger rtc: seg 3
        sim.rtc_ms = 10 ** 9
        sim.cycle(sleep_h=8.0)
        a = self.analyze(csv_text(sim.rows))
        self.assertEqual([len(s.rows) for s in a.segs], [4, 4, 2, 1])
        self.assertEqual([s.reason for s in a.segs], ["first row", "orphan boot (k = B)", "rtc_ms went backwards",
                                                      "row number restarted"])
        self.assertIn("(rtc_ms went backwards)", ba.render_report(a))
        # no pair crosses a segment boundary
        for s in a.segs:
            self.assertIsNone(s.rows[0].pair_prev)
        self.assertGreater(sum(1 for r in a.ds.rows if r.pair_prev is not None), 4)

    def test_segment_without_utc_then_a_synced_one(self):
        sim = Sim()
        for _ in range(5):
            sim.cycle(sleep_h=8.0)  # no utc anywhere: a restored floor never reaches the column
        sim.orphan()
        for _ in range(3):
            sim.cycle(sleep_h=8.0)  # after the power loss, still unsynced
        sim.utc_base = 1791000000 - sim.rtc_ms // 1000  # a Wi-Fi join in this session: the next wake has a utc
        for _ in range(4):
            sim.cycle(sleep_h=8.0)
        a = self.analyze(csv_text(sim.rows))
        s0, s1 = a.segs
        self.assertFalse(s0.synced)
        self.assertTrue(all(r.wall is None and r.wall_src == "rel" for r in s0.rows))
        self.assertTrue(s1.synced)
        srcs = [r.wall_src for r in s1.rows]
        self.assertEqual(srcs, ["fill"] * 4 + ["utc"] * 4)  # the orphan row and the unsynced cycles are back-filled
        for r in s1.rows:
            self.assertAlmostEqual(r.wall - r.rtc_s, 1791000000 - (s1.rows[3].rtc_s), delta=1.0) if r.wall_src == "fill" \
                else self.assertEqual(r.wall, r.g("utc"))
        self.assertTrue(ba.time_str(s0.rows[0]).startswith("s0+0d"))
        self.assertTrue(ba.time_str(s1.rows[0]).endswith("~"))  # a back-filled time is marked
        self.assertTrue(ba.time_str(s1.rows[-1]).endswith("Z"))
        text = ba.render_report(a)
        self.assertIn("no utc: relative times only", text)

    def test_clock_drift_from_the_step_between_syncs(self):
        sim = Sim(utc_base=1791000000)
        for _ in range(4):
            sim.cycle(sleep_h=24.0)
        t_first = sim.rtc_ms
        sim.utc_base += 36  # the RC clock ran 36 s slow over the days since the previous sync
        for _ in range(4):
            sim.cycle(sleep_h=24.0)
        a = self.analyze(csv_text(sim.rows))
        seg = a.segs[0]
        self.assertEqual(len(seg.epochs), 2)
        self.assertEqual(len(seg.drift), 1)
        step, hours, ppm = seg.drift[0]
        self.assertAlmostEqual(step, 36.0, delta=1.0)
        self.assertAlmostEqual(hours, 4 * 24.25, delta=0.6)
        self.assertAlmostEqual(ppm, 36.0 / (hours * 3600) * 1e6, delta=1.0)
        self.assertGreater(ppm, 0)
        self.assertIn("ran slow", ba.render_report(a))
        self.assertGreater(t_first, 0)

    def test_utc_before_2025_is_not_a_clock(self):
        sim = Sim(utc_base=100)  # a floor-less epoch time
        for _ in range(3):
            sim.cycle(sleep_h=8.0)
        a = self.analyze(csv_text(sim.rows))
        self.assertFalse(a.segs[0].synced)
        self.assertEqual(a.segs[0].bad_utc, 3)

    def test_row_gaps_and_drops(self):
        sim = Sim()
        for _ in range(3):
            sim.cycle(sleep_h=8.0)
        sim.skip_rows(2)
        for _ in range(3):
            sim.cycle(sleep_h=8.0)
        a = self.analyze(csv_text(sim.rows))
        gap = [r for r in a.ds.rows if "GAP" in r.excl]
        self.assertEqual([r.g("row") for r in gap], [6])
        self.assertIsNone(gap[0].pair_prev)
        self.assertIsNotNone(a.ds.rows[4].pair_prev)  # the cycle after the gap pairs with its predecessor again
        self.assertIn("GAP", {f.code for f in a.flags})


# --- Exclusion rules --------------------------------------------------------------------------------------------------
class Exclusions(TempCase):
    def test_usb_spans_are_excluded_at_both_ends(self):
        sim = Sim()
        for _ in range(3):
            sim.cycle(sleep_h=8.0)
        sim.cycle(sleep_h=8.0, usb=True)  # USB present at the trigger and the wake
        sim.cycle(sleep_h=8.0, extra={"usb_ms": 90000})  # a cable for a minute in the session, no flag at the ends
        sim.cycle(sleep_h=8.0, extra={"chg_w": "f"})  # charge complete at the wake
        for _ in range(3):
            sim.cycle(sleep_h=8.0)
        a = self.analyze(csv_text(sim.rows), charge_hold_h=0.0)
        ex = [r.excl for r in a.ds.rows]
        self.assertEqual(ex[3].count("USB"), 1)
        self.assertIn("USB", ex[4])
        self.assertIn("USB", ex[5])
        self.assertNotIn("USB", ex[2])
        pairs = [r.pair_prev is not None for r in a.ds.rows]
        # rows 1 and 2 pair; the three USB rows and the row after them (its previous wake point is bad) do not
        self.assertEqual(pairs, [False, True, True, False, False, False, False, True, True])

    def test_charged_unseen_and_its_window(self):
        sim = Sim(soc=60.0)
        for _ in range(3):
            sim.cycle(sleep_h=8.0)
        sim.cycle(sleep_h=8.0, jump_pct=25.0)  # the cell rose with no USB flag
        sim.cycle(sleep_h=2.0)  # inside 12 h of it
        sim.cycle(sleep_h=2.0)
        sim.cycle(sleep_h=8.0)  # more than 12 h later
        sim.cycle(sleep_h=8.0)
        a = self.analyze(csv_text(sim.rows))
        ex = [r.excl for r in a.ds.rows]
        self.assertIn("CHG_UNSEEN", ex[3])
        self.assertNotIn("USB", ex[3])
        self.assertIn("CHG_TAIL", ex[4])
        self.assertIn("CHG_TAIL", ex[5])
        self.assertNotIn("CHG_TAIL", ex[6])
        self.assertEqual(ex[7], [])
        self.assertIsNone(a.ds.rows[3].pair_prev)
        self.assertIsNone(a.ds.rows[4].pair_prev)  # its previous wake point is the charged one
        self.assertIsNotNone(a.ds.rows[7].pair_prev)

    def test_loaded_step_rule(self):
        sim = Sim()
        sim.cycle(sleep_h=8.0)
        sim.cycle(sleep_h=8.0, extra={"e_mv": mv_at(sim.soc) - 90.0})  # entry 90 mV below the wake sample
        a = self.analyze(csv_text(sim.rows))
        self.assertIn("CHG_UNSEEN", a.ds.rows[1].excl)

    def test_short_sleeps_boot_rows_cutoff_highsoc_late_method_partial(self):
        sim = Sim(soc=88.0)
        sim.cycle(sleep_h=8.0)
        sim.cycle(sleep_h=0.2)  # SHORT
        sim.cycle(sleep_h=1.0)  # SHORT_A: usable pair, left out of estimator A
        sim.cycle(sleep_h=8.0, w_dt=2500)  # LATE
        sim.cycle(sleep_h=8.0, fl_e=1 << 7)  # PARTIAL
        sim.cycle(sleep_h=8.0, rst=3, fl_w=0x0004)  # a software restart, not a deep-sleep wake: BOOT
        sim.cycle(sleep_h=8.0, extra={"sleep_ms": ""}, fl_w=0x0002)  # RTC
        sim.cycle(sleep_h=8.0, path="b")  # CUTOFF by the path
        sim.cycle(sleep_h=8.0, extra={"m": 2})  # a different method id
        a = self.analyze(csv_text(sim.rows))
        ex = {r.g("row"): r.excl for r in a.ds.rows}
        self.assertIn("SHORT", ex[2])
        self.assertIn("SHORT_A", ex[3])
        self.assertNotIn("SHORT", ex[3])
        self.assertIn("LATE", ex[4])
        self.assertIn("PARTIAL", ex[5])
        self.assertIn("BOOT", ex[6])
        self.assertIn("RTC", ex[7])
        self.assertIn("CUTOFF", ex[8])
        self.assertIn("METHOD", ex[9])
        self.assertIsNone(a.ds.rows[1].pair_prev)  # a sleep under 30 min is no rested pair
        self.assertIsNotNone(a.ds.rows[2].pair_prev)  # 1 h is a usable pair ...
        self.assertFalse(ba.a_ok(a.ds.rows[2]))  # ... that estimator A leaves out
        self.assertFalse(ba.a_ok(a.ds.rows[0]))  # the first row has no previous wake sample

    def test_highsoc_and_cutoff_by_voltage(self):
        sim = Sim(soc=97.0)
        sim.cycle(sleep_h=8.0)
        sim.cycle(sleep_h=8.0)
        a = self.analyze(csv_text(sim.rows))
        self.assertTrue(all("HIGHSOC" in r.excl for r in a.ds.rows))
        self.assertTrue(all(r.pair_prev is None for r in a.ds.rows))
        sim = Sim(soc=30.0)
        sim.cycle(sleep_h=8.0, extra={"w_mv": 3390.0})  # in the LDO dropout region
        a = self.analyze(csv_text(sim.rows))
        self.assertIn("CUTOFF", a.ds.rows[0].excl)

    def test_method_family_minority_is_excluded(self):
        sim = Sim()
        for _ in range(5):
            sim.cycle(sleep_h=8.0)
        other = Sim(fw="aaaaaaaaa", rtc_ms=sim.rtc_ms, row=sim.row_no, soc=sim.soc)
        for _ in range(2):
            other.cycle(sleep_h=8.0)
        text = (csv_text(sim.rows) + "\n# fw aaaaaaaaa 1.6.5-batlog m1 w250 ser1"
                + "".join("\n" + r for r in other.rows))
        a = self.analyze(text)
        self.assertEqual([("METHOD" in r.excl) for r in a.ds.rows], [False] * 5 + [True] * 2)
        self.assertEqual(len(a.ds.families), 2)

    def test_orphan_rows_are_boot_and_never_a_pair_end(self):
        sim = Sim()
        for _ in range(3):
            sim.cycle(sleep_h=8.0)
        sim.orphan()
        for _ in range(3):
            sim.cycle(sleep_h=8.0)
        a = self.analyze(csv_text(sim.rows))
        b = a.ds.rows[3]
        self.assertEqual(b.g("k"), "B")
        self.assertIn("BOOT", b.excl)
        self.assertIsNone(b.pair_prev)
        self.assertIsNone(a.ds.rows[4].pair_prev)  # its previous point is the orphan boot


# --- The numerics -----------------------------------------------------------------------------------------------------
class Numerics(unittest.TestCase):
    def test_solve_linear(self):
        a = [[2.0, 1.0, 0.0], [1.0, 3.0, 1.0], [0.0, 1.0, 4.0]]
        z = ba.solve_linear(a, [3.0, 5.0, 5.0], [0, 1, 2])
        self.assertAlmostEqual(z[0], 1.0)
        self.assertAlmostEqual(z[1], 1.0)
        self.assertAlmostEqual(z[2], 1.0)
        self.assertIsNone(ba.solve_linear([[1.0, 2.0], [2.0, 4.0]], [1.0, 2.0], [0, 1]))
        sub = ba.solve_linear(a, [3.0, 5.0, 5.0], [0, 2])
        self.assertAlmostEqual(sub[0], 1.5)

    @staticmethod
    def gram(xs, ys):
        p = len(xs[0])
        g = [[sum(x[i] * x[j] for x in xs) for j in range(p)] for i in range(p)]
        h = [sum(x[i] * y for x, y in zip(xs, ys)) for i in range(p)]
        return g, h

    def test_nnls_recovers_non_negative_truth(self):
        rng = random.Random(5)
        xs = [[rng.uniform(0, 10), rng.uniform(0, 3), 1.0, rng.uniform(0, 5)] for _ in range(60)]
        truth = [0.2, 1.5, 0.05, 0.4]
        ys = [sum(t * v for t, v in zip(truth, x)) for x in xs]
        g, h = self.gram(xs, ys)
        for got, want in zip(ba.nnls_gram(g, h), truth):
            self.assertAlmostEqual(got, want, places=5)

    def test_nnls_matches_brute_force_when_a_coefficient_wants_to_be_negative(self):
        rng = random.Random(11)
        for trial in range(25):
            p = 4
            xs = [[rng.uniform(0, 5) for _ in range(p)] for _ in range(30)]
            truth = [rng.uniform(-1.0, 1.5) for _ in range(p)]
            ys = [sum(t * v for t, v in zip(truth, x)) + rng.gauss(0, 0.3) for x in xs]
            g, h = self.gram(xs, ys)
            got = ba.nnls_gram(g, h)
            self.assertTrue(all(c >= 0.0 for c in got))

            def sse(c):
                return sum((y - sum(ci * xi for ci, xi in zip(c, x))) ** 2 for x, y in zip(xs, ys))

            best = float("inf")
            for r in range(1, p + 1):
                for sub in itertools.combinations(range(p), r):
                    z = ba.solve_linear(g, h, sub)
                    if z is None or any(v < 0 for v in z):
                        continue
                    c = [0.0] * p
                    for i, v in zip(sub, z):
                        c[i] = v
                    best = min(best, sse(c))
            self.assertLessEqual(sse(got), best + 1e-6 * max(1.0, best), "trial %d" % trial)

    def test_nnls_ignores_an_empty_column_and_a_duplicate(self):
        xs = [[float(i), 0.0, float(i)] for i in range(1, 20)]
        ys = [2.0 * x[0] for x in xs]
        g, h = self.gram(xs, ys)
        c = ba.nnls_gram(g, h)
        self.assertEqual(c[1], 0.0)
        self.assertAlmostEqual(c[0] + c[2], 2.0, places=4)

    def test_percentile(self):
        self.assertEqual(ba.percentile([1.0, 2.0, 3.0, 4.0, 5.0], 50), 3.0)
        self.assertEqual(ba.percentile([1.0, 2.0, 3.0, 4.0, 5.0], 0), 1.0)
        self.assertEqual(ba.percentile([1.0, 2.0, 3.0, 4.0, 5.0], 100), 5.0)
        self.assertAlmostEqual(ba.percentile([1.0, 2.0], 25), 1.25)
        self.assertEqual(ba.percentile([7.0], 95), 7.0)

    def test_meas_threshold_moves_with_capacity(self):
        self.assertAlmostEqual(ba.meas_threshold_ua(500.0), 150.0)
        self.assertAlmostEqual(ba.meas_threshold_ua(1000.0), 167.5)

    def test_floor_and_thresholds(self):
        self.assertAlmostEqual(ba.expected_floor_ua(500.0, True), 130.5)
        self.assertAlmostEqual(ba.expected_floor_ua(1000.0, True), 148.0)
        self.assertAlmostEqual(ba.watch_threshold_ua(500.0, True), 130.0)
        self.assertAlmostEqual(ba.watch_threshold_ua(500.0, False), 95.0)
        self.assertAlmostEqual(ba.watch_threshold_ua(1000.0, True), 147.5)

    def test_resolution_limits_are_the_design_table_b(self):
        # Table B of the design: a 3-day bookend has 22 uA of voltage noise at 500 mAh; 3 sigma with a 30 % and a 50 %
        # scale error of the 130 uA floor gives 134 and 206 uA.
        opts = ba.Options(capacity_mah=500.0)
        rates = ba.Rates(500.0, 8.0)
        est = ba.Estimate("X", "x")
        se_pct = 22.0 / (500.0 * 10.0)
        est.pct_h, est.lo, est.hi, est.se = 0.01, 0.0, 0.02, se_pct
        ba._finish_rate(est, rates, opts, True)
        self.assertAlmostEqual(est.min_zero_ua, 66.0, places=3)
        self.assertAlmostEqual(est.min_floor_ua[0], 134.7, delta=1.0)
        self.assertAlmostEqual(est.min_floor_ua[1], 206.7, delta=2.0)
        self.assertFalse(est.resolved_zero)
        self.assertFalse(est.resolved_floor)
        text = "\n".join(est.lines)
        self.assertIn("not resolved", text)
        self.assertIn("smallest it could have resolved", text)


# --- The estimators ---------------------------------------------------------------------------------------------------
class Estimators(TempCase):
    def test_pooled_regression_recovers_a_known_drain_with_a_band(self):
        sim = Sim(noise_mv=0.3)
        campaign(sim, days=40, sleep_ua=800.0)
        a = self.analyze(csv_text(sim.rows), resamples=300)
        est = a.estimates[0]
        self.assertEqual(est.status, "ok")
        self.assertGreater(est.n, 80)
        self.assertAlmostEqual(est.est_ua, 800.0, delta=120.0)
        self.assertLess(est.lo_ua, 800.0)
        self.assertGreater(est.hi_ua, 800.0)
        self.assertTrue(est.resolved_zero)
        self.assertGreater(est.min_zero_ua, 0.0)
        self.assertGreater(est.min_floor_ua[1], est.min_floor_ua[0])
        self.assertLess(est.residual_mv, 3.0)
        self.assertTrue(est.resolved_floor)  # 800 uA is well over the 130 uA floor
        self.assertTrue(any(v.startswith(("RUN MEAS-01", "WATCH", "HIGH")) for v in a.voltage_flags))
        text = ba.render_report(a)
        self.assertIn("A. Pooled wake-pair regression", text)
        self.assertIn("RESOLVED", text)
        self.assertIn("smallest it could have resolved", text)

    def test_the_floor_itself_is_not_resolved(self):
        # The honest part: 113 uA over a few weeks of use cannot be told from zero, and nothing is said to be found.
        sim = Sim(noise_mv=0.3)
        campaign(sim, days=30, sleep_ua=113.0)
        a = self.analyze(csv_text(sim.rows), resamples=300)
        est = a.estimates[0]
        self.assertEqual(est.status, "ok")
        self.assertFalse(est.resolved_floor)
        self.assertEqual(a.voltage_flags, [])
        self.assertIn("do not resolve", "\n".join(est.lines))
        self.assertGreater(est.min_floor_ua[0], 100.0)  # it could not have seen an excess below that

    def test_capacity_override_scales_the_microamps(self):
        sim = Sim(noise_mv=0.3)
        campaign(sim, days=30, sleep_ua=600.0)
        text = csv_text(sim.rows)
        a500 = self.analyze(text, resamples=100)
        a1000 = self.analyze(text, resamples=100, capacity_mah=1000.0)
        e5, e10 = a500.estimates[0], a1000.estimates[0]
        self.assertAlmostEqual(e10.est_ua / e5.est_ua, 2.0, places=6)
        if e5.lo_ua:
            self.assertAlmostEqual(e10.lo_ua / e5.lo_ua, 2.0, places=6)
        self.assertIn("1000 mAh", ba.render_report(a1000))
        self.assertIn("500 mAh", ba.render_report(a500))

    def test_no_capacity_reports_millivolts_per_day(self):
        sim = Sim(noise_mv=0.3)
        campaign(sim, days=30, sleep_ua=600.0)
        a = self.analyze(csv_text(sim.rows), resamples=100, capacity_mah=0.0)
        text = ba.render_report(a)
        self.assertIn("mV/day", text)
        self.assertNotIn(" uA, 95%", text)
        self.assertIsNone(a.estimates[0].est_ua)

    def test_too_little_data_is_said_so(self):
        sim = Sim()
        for _ in range(4):
            sim.cycle(sleep_h=8.0)
        a = self.analyze(csv_text(sim.rows))
        self.assertEqual([e.status for e in a.estimates], ["no data"] * 3)
        text = ba.render_report(a)
        self.assertIn("not enough eligible cycles", text)
        self.assertIn("keep logging", text)

    def test_few_day_blocks_give_no_band(self):
        sim = Sim()
        for i in range(14):
            sim.cycle(sleep_h=2.5 + (i % 5), awake_min=5.0 + i % 3, sleep_ua=500.0)
        a = self.analyze(csv_text(sim.rows))
        est = a.estimates[0]
        self.assertEqual(est.status, "ok")
        self.assertIsNone(est.lo)
        self.assertFalse(est.resolved_zero)
        self.assertIn("fewer than 7 day blocks", " ".join(est.lines))

    def test_a_short_run_of_days_gets_a_band_with_a_warning(self):
        sim = Sim(noise_mv=0.3)
        campaign(sim, days=9, sleep_ua=900.0, recharge_below=0.0, per_day=3)
        a = self.analyze(csv_text(sim.rows), resamples=100)
        est = a.estimates[0]
        self.assertIsNotNone(est.lo)
        self.assertIn("only 9 day blocks: the bootstrap band is optimistic", " ".join(est.lines))

    def test_estimator_b_wake_to_wake_inside_one_sleep(self):
        sim = Sim(soc=85.0)
        sim.cycle(sleep_h=8.0)
        # A 20 h sleep at 400 uA with an early wake sample at 5 h: the two samples are 15 h apart.
        probe = copy.deepcopy(sim)
        probe.cycle(sleep_h=20.0, sleep_ua=400.0, awake_min=1.0, g_n=1)
        g_soc = probe.soc + 0.4 * 15.0 / sim.cap * 100.0  # 15 h of 400 uA above the wake sample
        row = sim.cycle(sleep_h=20.0, sleep_ua=400.0, awake_min=1.0, g_n=1, g_mv=mv_at(g_soc), g_t=5 * 3600 * 1000)
        a = self.analyze(csv_text(sim.rows[:1] + [row]))
        b = a.estimates[1]
        self.assertEqual(b.status, "ok")
        self.assertEqual(b.n, 1)
        self.assertAlmostEqual(b.est_ua, 400.0, delta=20.0)
        self.assertLess(b.lo_ua, 400.0)
        self.assertGreater(b.hi_ua, 400.0)
        self.assertIn("B. Wake to wake inside one sleep", ba.render_report(a))

    def test_estimator_b_needs_two_hours_and_three_hours_of_span(self):
        for g_t, sleep_h in ((1.0, 20.0), (5.0, 7.0)):
            sim = Sim(soc=85.0)
            sim.cycle(sleep_h=8.0)
            row = sim.cycle(sleep_h=sleep_h, awake_min=1.0, g_n=1, g_mv=mv_at(sim.soc), g_t=int(g_t * 3.6e6))
            a = self.analyze(csv_text(sim.rows[:1] + [row]))
            self.assertEqual(a.estimates[1].status, "no data")

    def test_estimator_c_overnight_pairs(self):
        sim = Sim(noise_mv=0.1, soc=90.0)
        sim.cycle(sleep_h=8.0)
        for i in range(8):
            if sim.soc < 45:
                sim.cycle(sleep_h=8.0, charge_to=88.0)
            sim.cycle(sleep_h=10.0 + 0.2 * i, awake_min=1.0, sleep_ua=1000.0)
        a = self.analyze(csv_text(sim.rows))
        c = a.estimates[2]
        self.assertEqual(c.status, "ok")
        self.assertGreaterEqual(c.n, 6)
        # an upper bound: the sleep drain plus the short session's share (0.3 mAh a cycle over 10 h is 30 uA)
        self.assertGreaterEqual(c.est_ua, 1000.0 - 40.0)
        self.assertLess(c.est_ua, 1000.0 + 120.0)
        self.assertIn("upper bound", " ".join(c.lines))

    def test_estimator_c_skips_a_lit_or_radio_session(self):
        sim = Sim()
        sim.cycle(sleep_h=8.0)
        sim.cycle(sleep_h=9.0, awake_min=1.0, lit_min=0.5)
        sim.cycle(sleep_h=9.0, awake_min=1.0, wifi_min=0.5)
        sim.cycle(sleep_h=9.0, awake_min=20.0)  # too long a session
        a = self.analyze(csv_text(sim.rows))
        self.assertEqual(a.estimates[2].status, "no data")


# --- What the screen showed, the counters, the bursts -----------------------------------------------------------------
class Summaries(TempCase):
    def test_display_flip_with_no_time_elapsed_is_noise(self):
        sim = Sim(soc=83.4)
        sim.cycle(sleep_h=2.0, extra={"e_ui": 830, "w_ui": 830})
        flips = 0
        for i in range(6):  # the six sleeps in ten minutes
            sim.cycle(sleep_h=1.0 / 60.0, awake_min=0.4, sleep_ua=113.0,
                      extra={"e_ui": 826, "w_ui": 818 if i % 2 == 0 else 826})
            flips += 1 if i % 2 == 0 else 0
        a = self.analyze(csv_text(sim.rows))
        lines, counts = ba.display_summary(a.ds.rows)
        text = "\n".join(lines)
        self.assertIn("display noise floor: 6 cycle(s)", text)
        self.assertIn("changed in %d of them" % flips, text)
        self.assertEqual(counts["rounding"], 3)

    def test_display_classes(self):
        sim = Sim()
        sim.cycle(sleep_h=8.0, extra={"e_ui": 830, "w_ui": 830})  # steady
        sim.cycle(sleep_h=8.0, extra={"e_ui": 826, "w_ui": 818})  # 83 to 82 on 0.8 %
        sim.cycle(sleep_h=8.0, extra={"e_ui": 850, "w_ui": 820})  # a real step of 3 %
        a = self.analyze(csv_text(sim.rows))
        self.assertEqual([r.disp for r in a.ds.rows], ["steady", "rounding", "step"])

    def test_counter_flags(self):
        sim = Sim(soc=80.0)
        sim.cycle(sleep_h=8.0)
        sim.cycle(sleep_h=2.0, g_n=4)  # more than one ghost per sleep hour
        sim.cycle(sleep_h=8.0, g_n=3, extra={"g_ms": 3 * 520})  # early wakes too long
        sim.cycle(sleep_h=8.0, path="k", fl_e=1 << 4)  # ceiling, and a key held at entry
        sim.cycle(sleep_h=8.0, path="w", lit_min=2.0)  # a pocket relight
        sim.cycle(sleep_h=8.0, fl_e=(1 << 2) | (1 << 3))  # touch awake, SDA low
        sim.cycle(sleep_h=8.0, fl_e=1 << 8, fl_w=(1 << 7) | (1 << 6))  # burst skipped, slow flush
        sim.cycle(sleep_h=8.0, extra={"ent_ms": 4000, "ab": 2, "ab_r": 4})
        sim.cycle(sleep_h=8.0, awake_min=40.0, idle_ms=40 * 60000, tmo=10)  # idle past the ceiling
        sim.cycle(sleep_h=8.0, path="b", extra={"w_mv": 3480.0})
        a = self.analyze(csv_text(sim.rows))
        codes = {f.code for f in a.flags}
        for want in ("GHOST_RATE", "EARLY_LONG", "KEY_HELD", "CEILING", "POCKET", "CEILING_MISSED", "SLOW_ENTRY",
                     "TOUCH_AWAKE", "SDA_LOW", "BURST_SKIPPED", "SD_TROUBLE", "ABNORMAL_RESET", "CUTOFF_LOW"):
            self.assertIn(want, codes, want)
        text = ba.render_report(a)
        self.assertIn("6. Counter flags", text)
        self.assertIn("pocket relight", text)

    def test_long_idle_tails(self):
        sim = Sim(soc=85.0)
        sim.cycle(sleep_h=8.0)  # a 10 min tail on a 10 min timeout: normal
        sim.cycle(sleep_h=8.0, awake_min=35.0, idle_ms=30 * 60000, tmo=10)  # 30 min on a 10 min timeout
        sim.cycle(sleep_h=8.0, awake_min=30.0, idle_ms=25 * 60000, tmo=0)  # 25 min with no timeout (Never)
        sim.cycle(sleep_h=8.0, awake_min=20.0, idle_ms=15 * 60000, tmo=0, path="k")  # the ceiling: its own flag
        a = self.analyze(csv_text(sim.rows))
        tail = [f for f in a.flags if f.code == "LONG_TAIL"]
        self.assertEqual(len(tail), 1)
        self.assertEqual(tail[0].rows, [2, 3])
        self.assertIn("2 time(s), 0.9 h in all", tail[0].text)
        self.assertIn("idle tail before the sleep: median", ba.render_report(a))

    def test_entry_to_wake_is_shown_but_never_a_drain_estimate(self):
        # Table A of the design: 3 sigma of 7 mV over a 9 h sleep at 8 mV per percent and 500 mAh is 1.46 mA.
        sim = Sim(soc=85.0, noise_mv=1.0)
        for _ in range(6):
            sim.cycle(sleep_h=9.0, awake_min=3.0, sleep_ua=113.0)
        a = self.analyze(csv_text(sim.rows))
        text = "\n".join(a.diag_lines)
        self.assertRegex(text, r"6 clean sleeps of 2 h or more: the wake sample sits \+\d+\.\d mV")
        m = re.search(r"resolved \(3 sigma of 7 mV over the median 9\.0 h sleep at (\d+) mV per percent\): "
                      r"([\d.]+) mA", text)
        self.assertIsNotNone(m, text)
        self.assertEqual(m.group(1), "8")
        self.assertAlmostEqual(float(m.group(2)), 1.46, delta=0.02)
        self.assertIn("NOT a drain estimate", text)
        report = ba.render_report(a)
        self.assertIn("D. Loaded entry to wake sample (a diagnostic, not a drain estimator)", report)
        self.assertEqual([e.key for e in a.estimates], ["A", "B", "C"])  # no estimator D feeds the drain flags

    def test_entry_to_wake_with_too_few_sleeps(self):
        sim = Sim()
        sim.cycle(sleep_h=9.0)
        a = self.analyze(csv_text(sim.rows))
        self.assertIn("nothing to show", " ".join(a.diag_lines))

    def test_wake_storm_by_the_day(self):
        sim = Sim(soc=90.0, utc_base=1791000000)
        sim.cycle(sleep_h=6.0)
        sim.cycle(sleep_h=6.0, g_n=11)
        sim.cycle(sleep_h=6.0, g_n=2)
        a = self.analyze(csv_text(sim.rows))
        self.assertIn("WAKE_STORM", {f.code for f in a.flags})

    def test_no_flags_on_a_quiet_log(self):
        sim = Sim(soc=90.0)
        for _ in range(6):
            sim.cycle(sleep_h=8.0)
        a = self.analyze(csv_text(sim.rows))
        self.assertEqual(a.flags, [])
        self.assertIn("none", ba.render_report(a))

    def test_display_cases_name_the_cause(self):
        sim = Sim(soc=83.4)
        sim.cycle(sleep_h=9.0, awake_min=25.0, lit_min=12.0, lit_pct=40, idle_ms=600000,
                  extra={"e_ui": 826, "w_ui": 818})  # the design's example: 83 to 82
        a = self.analyze(csv_text(sim.rows))
        lines = ba.display_cases(a.ds.rows)
        self.assertEqual(len(lines), 1)
        for want in ("shown 83 -> 82", "10 min idle tail", "light on 12 min", "ended by auto-sleep timeout",
                     "rounding and load, not drain evidence"):
            self.assertIn(want, lines[0])
        self.assertIn("the most recent cycles where the shown percent changed", ba.render_report(a))

    def test_wake_storm_names_the_day(self):
        sim = Sim(soc=90.0, utc_base=1791000000)
        sim.cycle(sleep_h=6.0)
        sim.cycle(sleep_h=6.0, g_n=11)
        a = self.analyze(csv_text(sim.rows))
        text = " ".join(f.text for f in a.flags if f.code == "WAKE_STORM")
        self.assertRegex(text, r"20\d\d-\d\d-\d\d \(11\)")

    def test_usb_all_session_needs_a_pattern_not_one_charge(self):
        sim = Sim(soc=60.0)
        for _ in range(8):
            sim.cycle(sleep_h=8.0)
        sim.cycle(sleep_h=8.0, awake_min=30.0, charge_to=80.0)  # one real charge
        a = self.analyze(csv_text(sim.rows))
        self.assertNotIn("USB_ALL_SESSION", {f.code for f in a.flags})
        sim = Sim(soc=60.0)
        for _ in range(8):
            sim.cycle(sleep_h=8.0, awake_min=30.0, charge_to=sim.soc)  # a cable that is always "in"
        a = self.analyze(csv_text(sim.rows))
        self.assertIn("USB_ALL_SESSION", {f.code for f in a.flags})
        self.assertEqual(sum(1 for r in a.ds.rows if r.pair_prev is not None), 0)

    def test_burst_quality_calibration_drift(self):
        sim = Sim(soc=80.0)
        for i in range(12):
            raw = int(16 * 2000 / 0.8693 * (1.0 + (0.004 if i >= 8 else 0.0)))  # the last third reads 0.4 % higher
            sim.cycle(sleep_h=8.0, extra={"e_raw": raw, "w_raw": raw, "e_mv": 4000.0, "w_mv": 4000.0})
        a = self.analyze(csv_text(sim.rows))
        text = "\n".join(ba.burst_quality(a.ds.rows))
        self.assertIn("calibration drift", text)
        self.assertRegex(text, r"differs from the first by -0\.\d\d %")

    def test_burst_quality(self):
        sim = Sim(soc=90.0)
        for _ in range(6):
            sim.cycle(sleep_h=8.0)
        sim.cycle(sleep_h=8.0, extra={"e_z": 3, "e_raw": 41000, "fl_w": 1 << 7})
        a = self.analyze(csv_text(sim.rows))
        text = "\n".join(ba.burst_quality(a.ds.rows))
        self.assertIn("entry burst", text)
        self.assertIn("wake burst", text)
        self.assertIn("zero conversions (the ADC's silent failure): 1", text)
        self.assertIn("burst(s) are off by more than 2 %", text)

    def test_runs_are_split_at_charges(self):
        sim = Sim(soc=80.0, noise_mv=0.1)
        for _ in range(6):
            sim.cycle(sleep_h=9.0, sleep_ua=900.0)
        sim.cycle(sleep_h=2.0, charge_to=90.0)
        for _ in range(6):
            sim.cycle(sleep_h=9.0, sleep_ua=900.0)
        a = self.analyze(csv_text(sim.rows))
        lines, steps = ba.run_summaries(a.ds.rows, a.opts, a.rates)
        runs = [ln for ln in lines if ln.startswith("run from row")]
        self.assertEqual(len(runs), 2)
        self.assertTrue(all("%/day" in ln for ln in runs))
        self.assertEqual(steps, [])  # too few cycles in a run for an estimate of its own
        self.assertEqual([len(r) for r in ba.split_runs(a.ds.rows)], [6, 6])  # the charge row is in neither
        self.assertEqual(a.run_lines, lines)

    def test_a_step_in_the_sleep_estimate_between_two_runs(self):
        # Two long runs with the same use and a tenfold different sleep drain: the bands do not overlap.
        sim = Sim(noise_mv=0.2, cap=2000.0)
        campaign(sim, days=12, sleep_ua=150.0, seed=4, recharge_below=0.0)
        sim.cycle(sleep_h=2.0, charge_to=88.0)
        campaign(sim, days=12, sleep_ua=1500.0, seed=4, recharge_below=0.0)
        a = self.analyze(csv_text(sim.rows), resamples=200, capacity_mah=2000.0)
        self.assertEqual(len(ba.split_runs(a.ds.rows)), 2)
        self.assertTrue(any(v.startswith("STEP:") for v in a.voltage_flags), a.run_lines)
        self.assertIn("STEP", [ln for ln in ba.summary_lines(a) if ln.startswith("voltage flags")][0])
        self.assertTrue(any("sleep estimate" in ln for ln in a.run_lines))


# --- The whole thing --------------------------------------------------------------------------------------------------
class EndToEnd(TempCase):
    def run_main(self, argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = ba.main(argv)
        return code, out.getvalue()

    def test_cli_report_and_outputs(self):
        sim = Sim(noise_mv=0.3, utc_base=1791000000)
        campaign(sim, days=20, sleep_ua=700.0)
        sim.rows.insert(5, sim.rows[5][:60])  # a torn row in the middle
        path = self.write("battery.csv", csv_text(sim.rows))
        outdir = self.tmp / "out"
        code, text = self.run_main([path, "--out", str(outdir), "--resamples", "60", "--rows", "10"])
        self.assertEqual(code, 0)
        for heading in ("Silkscreen battery log report", "Summary", "1. File health",
                        "2. Segments and the timeline", "3. Cycles",
                        "4. Display versus volts", "5. Burst quality", "6. Counter flags", "7. Exclusions",
                        "8. Sleep-drain estimates", "9. Runs and flags by voltage", "10. What to do next",
                        "What a voltage log cannot do"):
            self.assertIn(heading, text)
        self.assertIn("capacity: 500 mAh", text)
        self.assertRegex(text, r"rows: \d+ good, 0 bad CRC, 1 torn")
        self.assertIn("58 uA from 113 uA", text)
        self.assertTrue((outdir / "battery_report.txt").is_file())
        cyc = (outdir / "battery_cycles.csv").read_text(encoding="ascii").splitlines()
        self.assertEqual(cyc[0].split(","), list(ba.CYCLE_COLUMNS))
        self.assertEqual(len(cyc) - 1, 60)
        text.encode("ascii")  # the report is plain ASCII
        self.assertEqual((outdir / "battery_report.txt").read_text(encoding="ascii"), text)

    def test_cli_capacity_option(self):
        sim = Sim(noise_mv=0.3)
        campaign(sim, days=15, sleep_ua=700.0)
        path = self.write("battery.csv", csv_text(sim.rows))
        code, text = self.run_main([path, "--capacity-mah", "1000", "--resamples", "40", "--rows", "3"])
        self.assertEqual(code, 0)
        self.assertIn("capacity: 1000 mAh", text)
        code, text = self.run_main([path, "--capacity-mah", "0", "--resamples", "40", "--rows", "3"])
        self.assertIn("unknown (rates in mV per day", text)

    def test_cli_exit_codes(self):
        empty = self.write("empty.csv", ",".join(ba.COLUMNS))
        code, text = self.run_main([empty])
        self.assertEqual(code, 1)
        self.assertIn("no valid row", text)
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(ba.main([str(self.tmp / "missing.csv")]), 2)
            self.assertEqual(ba.main([empty, "--capacity-mah", "-5"]), 2)
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                ba.main([])

    def test_report_of_a_single_example_row(self):
        path = self.write("battery.csv", csv_text([EXAMPLE_ROW]))
        code, text = self.run_main([path, "--resamples", "10"])
        self.assertEqual(code, 0)
        self.assertIn("1 good", text)

    def test_damaged_files_never_raise_and_never_accept_a_bad_row(self):
        # Random byte damage to a file with every feature in it: the parser classifies, the analysis and the report
        # run, and every row that got through passes its CRC.
        sim = Sim(noise_mv=0.3, utc_base=1791000000)
        campaign(sim, days=4, sleep_ua=300.0, seed=2)
        sim.orphan()
        campaign(sim, days=3, sleep_ua=300.0, seed=5)
        sim.cycle(sleep_h=2.0, g_n=2, g_mv=3900.0, g_t=3600000, usb=True)
        clean = csv_text(sim.rows).encode("ascii")
        rng = random.Random(1234)
        for trial in range(60):
            data = bytearray(clean)
            for _ in range(rng.randint(1, 12)):
                kind = rng.random()
                pos = rng.randrange(len(data))
                if kind < 0.4:
                    data[pos] = rng.randrange(256)
                elif kind < 0.6:
                    del data[pos:pos + rng.randint(1, 30)]
                elif kind < 0.8:
                    data[pos:pos] = bytes(rng.randrange(256) for _ in range(rng.randint(1, 8)))
                else:
                    del data[rng.randrange(len(data)):]
            res = ba.parse_bytes(bytes(data))
            for r in res.rows:
                self.assertEqual(ba.check_row_text(r.text, 53)[0], "ok")
            path = self.write("fuzz%d.csv" % trial, data.decode("latin-1"))
            a = ba.analyze([path], ba.Options(resamples=15))
            text = ba.render_report(a)
            text.encode("ascii")

    def test_example_row_wall_time(self):
        a = ba.analyze([self.write("battery.csv", csv_text([EXAMPLE_ROW]))], ba.Options(resamples=10))
        r = a.ds.rows[0]
        self.assertEqual(r.wall, 1791234567.0)
        self.assertEqual(ba.time_str(r), "2026-10-05T21:09:27Z")


if __name__ == "__main__":
    unittest.main()
