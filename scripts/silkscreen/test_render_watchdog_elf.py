#!/usr/bin/env python3
"""Host regression checks for function ownership in the ELF guard."""
import importlib.util
import io
import unittest
from pathlib import Path
from unittest.mock import patch

CHECKER = Path(__file__).with_name("check_render_watchdog_elf.py")


class FunctionBoundsTest(unittest.TestCase):
    def analyze(self, text, extra=(), size=8, words=None):
        spec = importlib.util.spec_from_file_location("watchdog_check", CHECKER)
        guard = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(guard)
        analysis = object.__new__(guard.Analysis)
        analysis.syms = sorted([(0x1000, size, "setup()"), (0x2000, 8, "loop()"),
                                (0x3000, 4, "renderwatchdog::holdsLogs()"), *extra])
        analysis.calls = {}

        class Process:
            stdout = io.StringIO(text)

            def wait(self):
                return 0

        class Literals:
            def word(self, address):
                return (words or {}).get(address)

        with patch.object(guard, "tool", return_value="objdump"), \
                patch.object(guard.subprocess, "Popen", return_value=Process()):
            undecoded = analysis._disassemble(Path("unused.elf"), Literals())
        return analysis, undecoded

    def test_raw_function_does_not_borrow_decoded_padding(self):
        text = ("1000 <setup()>:\n1000:\t12345678 \t\n1004:\t12345678 \t\n"
                "1008:\t000005\tcall8\t3000 <renderwatchdog::holdsLogs()>\n"
                "2000 <loop()>:\n2000:\t000005\tcall8\t3000 <renderwatchdog::holdsLogs()>\n")
        analysis, undecoded = self.analyze(text)
        self.assertIn(0x1000, undecoded)
        self.assertEqual(analysis.called_by("setup()"), set())
        self.assertEqual(analysis.called_by("loop()"), {"renderwatchdog::holdsLogs()"})

    def test_direct_and_indirect_calls_after_end_are_ignored(self):
        text = ("1000 <setup()>:\n1000:\t000081\tl32r\ta8, 800\n"
                "1008:\t0008e0\tcallx8\ta8\n"
                "100b:\t000005\tcall8\t3000 <renderwatchdog::holdsLogs()>\n")
        analysis, _ = self.analyze(text, words={0x800: 0x3000})
        self.assertEqual(analysis.called_by("setup()"), set())

    def test_unknown_header_breaks_literal_and_call_ownership(self):
        text = ("1000 <setup()>:\n1000:\t000081\tl32r\ta8, 800\n"
                "1003 <untyped_label>:\n1003:\t0008e0\tcallx8\ta8\n")
        analysis, _ = self.analyze(text, words={0x800: 0x3000})
        self.assertEqual(analysis.called_by("setup()"), set())

    def test_aliases_share_only_the_shortest_nonzero_extent(self):
        text = ("1000 <setup_alias()>:\n1000:\t000005\tcall8\t3000 <renderwatchdog::holdsLogs()>\n"
                "1004:\t000005\tcall8\t2000 <loop()>\n")
        analysis, _ = self.analyze(text, extra=[(0x1000, 4, "setup_alias()")])
        self.assertEqual(analysis.called_by("setup()"), {"renderwatchdog::holdsLogs()"})
        self.assertEqual(analysis.called_by("setup_alias()"), {"renderwatchdog::holdsLogs()"})

    def test_zero_size_function_supplies_no_instruction_evidence(self):
        text = "1000 <setup()>:\n1000:\t000005\tcall8\t3000 <renderwatchdog::holdsLogs()>\n"
        analysis, _ = self.analyze(text, size=0)
        self.assertEqual(analysis.called_by("setup()"), set())


if __name__ == "__main__":
    unittest.main()
