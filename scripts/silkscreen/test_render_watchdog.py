#!/usr/bin/env python3
"""Host test of the Silkscreen render watchdog (docs/silkscreen/POWER.md, AW-04), and the source guards behind it.

Five parts:

1. The decision. scripts/silkscreen/render_watchdog_host_test.cpp is built against src/util/RenderWatchdog.h, the
   header the firmware compiles, and run: when a render, or a sleep entry, counts as hung (idle, the bound's edge, the
   wrap of millis(), USB, a stamp from the future, waiting for the render lock, a render that makes progress, the
   longer of two hung marks). The run must report checks that ran: a build that runs none fails.
2. The hooks. A merge that drops a hook builds every env and fails nothing, so the order the design stands on is
   read from the source: in ActivityManager::renderTaskLoop() the stamp is taken after the render lock is held and
   immediately before render(), and cleared immediately after; the task starts after the render task exists; enterDeepSleep()
   sets the sleep entry's mark first; main.cpp passes the loop's heartbeat, the early sleep after the watchdog's restart
   shares the reset loop's sequence, the reset loop spends a pending watchdog sleep, and the log ring is held from the
   top of setup() to the report. The long renders (a section build's chunks, the Home covers, a text index) re-stamp
   through progress() at the named places, and Logging keeps the ring while it is held.
3. The expiry path takes no lock. RenderWatchdog.cpp may call only what the proof in POWER.md section 3.3 allows after
   the point where the watchdog has decided: a task that is stuck holding the SPI, I2C, ADC, storage, log or
   front-light lock must not be able to stop it. The guard refuses the named APIs on FORBIDDEN, and every call the file
   makes must be on ALLOWED_CALLS (a new call is a new claim for the proof).
4. The numbers. The two bounds and the tick are pinned in the header's text, so that the source-only run, which has no
   compiler, still catches a changed bound.
5. (--control) Each of the above fails when it should.

--source-only runs parts 2, 3 and 4 alone (no compiler needed; scripts/silkscreen/build_matrix.sh runs it). --control
runs the negative controls: part 1 against copies of the header with a check removed, part 2 against copies of the
sources with a hook removed or moved, part 3 against a copy that takes a lock or makes an unlisted call, part 4 against
a changed bound. Each must fail; the command fails if one passes.

Compiler for part 1: $CXX, else c++, g++ or clang++ on PATH. On the Windows bench host there is no g++: use the MSVC
shim of scripts/silkscreen/msvc/README.md (MSYS_NO_PATHCONV=1 in Git Bash), e.g.
  CXX=C:/path/to/crosspoint-reader/scripts/silkscreen/msvc/cxx_msvc.bat python -I -X utf8 scripts/silkscreen/test_render_watchdog.py --control
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/util/RenderWatchdog.h"
SOURCE = ROOT / "src/util/RenderWatchdog.cpp"
ACTIVITY_MANAGER = ROOT / "src/activities/ActivityManager.cpp"
MAIN = ROOT / "src/main.cpp"
LOGGING_CPP = ROOT / "lib/Logging/Logging.cpp"
LOGGING_H = ROOT / "lib/Logging/Logging.h"
TEST = Path(__file__).resolve().parent / "render_watchdog_host_test.cpp"
GATE = "FREEINK_CAP_POWER_POLICY"
# The long renders that re-stamp (renderwatchdog::progress() through each file's watchdogProgress()), with the fewest
# call sites each file must hold.
PROGRESS_FILES = {
    "src/activities/reader/EpubReaderActivity.cpp": 5,
    "src/activities/home/HomeActivity.cpp": 1,
}
# The first section-build chunk of the render task in EpubReaderActivity.cpp, for the control that takes its re-stamp out.
CHUNK_MARKER = "buildSomeMore(BUILD_PAGES_PER_CHUNK)"
# The least number of checks a run of the host test must report (a test file that lost its cases fails).
MIN_CHECKS = 80


class GuardError(Exception):
    pass


# --- source reading ----------------------------------------------------------------------------------------------------


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8").replace("\r\n", "\n")


def strip_comments(text: str) -> str:
    """The text without // and /* */ comments (string literals kept), line structure unchanged."""
    out = []
    i = 0
    while i < len(text):
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = len(text) if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i)
            j = len(text) if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif text[i] == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def blank_strings(code: str) -> str:
    """The code with the inside of every string literal emptied, so that a word in a message is not a call."""
    return re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', code)


def function_body(text: str, signature: str) -> list[str]:
    """The lines of the function whose definition starts with `signature`, comments stripped."""
    code = strip_comments(text)
    start = code.find(signature)
    if start < 0 or code.count(signature) != 1:
        raise GuardError(f"expected exactly one `{signature}`")
    open_at = code.index("{", start)
    depth = 0
    i = open_at
    while i < len(code):
        if code[i] == '"':
            i += 1
            while code[i] != '"':
                i += 2 if code[i] == "\\" else 1
        elif code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return code[open_at : i + 1].split("\n")
        i += 1
    raise GuardError(f"unbalanced braces in `{signature}`")


def gated(lines: list[str], index: int, macro: str = GATE) -> bool:
    """Whether lines[index] sits inside an `#if <macro>` (the innermost conditional is that one)."""
    stack = []
    for line in lines[: index + 1]:
        m = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", line)
        if not m:
            continue
        kind, rest = m.group(1), m.group(2).strip()
        if kind in ("if", "ifdef", "ifndef"):
            stack.append(kind == "if" and rest == macro)
        elif kind in ("elif", "else") and stack:
            stack[-1] = False
        elif kind == "endif" and stack:
            stack.pop()
    return bool(stack) and stack[-1]


def significant(lines: list[str]) -> list[tuple[int, str]]:
    """(index, text) of the lines that hold code: not blank, not a preprocessor directive."""
    return [(n, l.strip()) for n, l in enumerate(lines) if l.strip() and not l.strip().startswith("#")]


# --- the hook guard ----------------------------------------------------------------------------------------------------


def check_hooks(activity_manager: str, main: str) -> list[str]:
    problems: list[str] = []

    def need(ok: bool, what: str) -> None:
        if not ok:
            problems.append(what)

    # ActivityManager::renderTaskLoop(): lock, then stamp, then render(), then clear; all behind the gate.
    try:
        body = function_body(activity_manager, "void ActivityManager::renderTaskLoop()")
    except GuardError as e:
        return [f"ActivityManager.cpp: {e}"]
    sig = significant(body)
    texts = [t for _, t in sig]
    render = [k for k, t in enumerate(texts) if "currentActivity->render(std::move(lock))" in t]
    need(len(render) == 1, "renderTaskLoop() must call currentActivity->render(std::move(lock)) exactly once")
    if len(render) == 1:
        k = render[0]
        begin = k - 1 if k > 0 and "renderwatchdog::beginRender(" in texts[k - 1] else -1
        end = k + 1 if k + 1 < len(texts) and "renderwatchdog::endRender()" in texts[k + 1] else -1
        need(begin >= 0, "renderwatchdog::beginRender(...) must be the statement immediately before render()")
        need(end >= 0, "renderwatchdog::endRender() must be the statement immediately after render()")
        lock = [j for j, t in enumerate(texts) if t.startswith("RenderLock lock;")]
        need(len(lock) == 1 and lock[0] < k, "the render lock must be acquired (`RenderLock lock;`) before render()")
        for what, at in (("beginRender", begin), ("endRender", end)):
            if at >= 0:
                need(gated(body, sig[at][0]), f"renderwatchdog::{what} must sit behind #if {GATE}")
        need(sum("renderwatchdog::" in t for t in texts) == 2, "renderTaskLoop() holds exactly the two stamp calls")

    # ActivityManager::begin(): the task starts after the render task is created.
    try:
        body = function_body(activity_manager, "void ActivityManager::begin(")
    except GuardError as e:
        problems.append(f"ActivityManager.cpp: {e}")
    else:
        sig = significant(body)
        texts = [t for _, t in sig]
        starts = [k for k, t in enumerate(texts) if "renderwatchdog::start()" in t]
        created = [k for k, t in enumerate(texts) if t.startswith("xTaskCreatePinnedToCore(")]
        need(len(starts) == 1, "ActivityManager::begin() must call renderwatchdog::start() exactly once")
        need(bool(created) and bool(starts) and starts[0] > created[0],
             "renderwatchdog::start() must come after the render task is created")
        if starts:
            need(gated(body, sig[starts[0]][0]), f"renderwatchdog::start() must sit behind #if {GATE}")
    need(bool(re.search(r'^\s*#\s*include\s+"util/RenderWatchdog.h"', activity_manager, re.M)),
         'ActivityManager.cpp must include "util/RenderWatchdog.h"')

    # main.cpp: the loop's heartbeat, the watchdog's early sleep, and the sequence it shares with the reset loop.
    code = strip_comments(main)
    try:
        loop = function_body(main, "void loop()")
    except GuardError as e:
        problems.append(f"main.cpp: {e}")
    else:
        sig = significant(loop)
        texts = [t for _, t in sig]
        beat = [k for k, t in enumerate(texts) if "renderwatchdog::noteLoopPass()" in t]
        update = [k for k, t in enumerate(texts) if "mappedInputManager.update()" in t]
        exclusive = [k for k, t in enumerate(texts) if "requiresExclusiveStorageLoop()" in t]
        need(len(beat) == 1, "loop() must call renderwatchdog::noteLoopPass() exactly once")
        need(bool(beat) and bool(update) and beat[0] > update[0],
             "noteLoopPass() must come after mappedInputManager.update() (which polls the charge ladder)")
        need(bool(beat) and bool(exclusive) and beat[0] < exclusive[0],
             "noteLoopPass() must come before the exclusive-storage branch, which returns early")
        if beat:
            need(gated(loop, sig[beat[0]][0]), f"noteLoopPass() must sit behind #if {GATE}")
    # enterDeepSleep(): the sleep entry's mark is the first statement. The sleep screen is drawn by the loop task, outside
    # the render stamp, so a sleep entry that hangs is covered by this mark and by nothing else. It is never cleared.
    try:
        sleep = function_body(main, "void enterDeepSleep(")
    except GuardError as e:
        problems.append(f"main.cpp: {e}")
    else:
        sig = significant(sleep)
        texts = [t for _, t in sig]
        mark = [k for k, t in enumerate(texts) if "renderwatchdog::beginSleepEntry()" in t]
        need(len(mark) == 1, "enterDeepSleep() must call renderwatchdog::beginSleepEntry() exactly once")
        need(mark == [1], "renderwatchdog::beginSleepEntry() must be the first statement of enterDeepSleep()")
        if mark:
            need(gated(sleep, sig[mark[0]][0]), f"renderwatchdog::beginSleepEntry() must sit behind #if {GATE}")
        need(not any("renderwatchdog::" in t and "beginSleepEntry" not in t for t in texts),
             "enterDeepSleep() holds no other watchdog call: its mark is never cleared (startDeepSleep() does not return)")
        # P9 (the hardware test firmware): its sleep-entry hang is the second statement, so that the mark covers it
        hang = [k for k, t in enumerate(texts) if "testsuite::watchdog::hangSleepEntryIfArmed()" in t]
        need(hang == [2], "testsuite::watchdog::hangSleepEntryIfArmed() must be the second statement of enterDeepSleep(), "
                          "right after the sleep-entry mark")
        if hang:
            need(gated(sleep, sig[hang[0]][0], "CROSSPOINT_TEST_SUITE"),
                 "the sleep-entry hang of P9 must sit behind #if CROSSPOINT_TEST_SUITE")
    try:
        shared = function_body(main, "static void sleepWithoutDrivers()")
    except GuardError as e:
        problems.append(f"main.cpp: {e}")
    else:
        texts = [t for _, t in significant(shared)]
        wanted = ["{", "resolvePanelController();",
                  'if (freeink::PowerManager::sleepPanelWithoutDriver()) LOG_DBG("PWR", "Panel reset and asleep");',
                  "gpio.resetTouchForSleep();", "powerManager.startDeepSleepMinimal(gpio);", "}"]
        need(texts == wanted, f"sleepWithoutDrivers() must be exactly, in order: {' '.join(wanted[1:-1])}")
    for name in ("static void sleepIfResetLoop()", "static void sleepIfRenderWatchdog()"):
        try:
            body = function_body(main, name)
        except GuardError as e:
            problems.append(f"main.cpp: {e}")
            continue
        sig = significant(body)
        if "RenderWatchdog" in name:
            # The whole function sits behind the gate (the definition, not a branch inside it).
            mine = [t for _, t in sig]
            others = []
            lines = strip_comments(main).split("\n")
            at = next((i for i, l in enumerate(lines) if name in l), -1)
            need(at >= 0 and gated(lines, at), f"`{name}` must be defined behind #if {GATE}")
        else:
            # Silkscreen's branch of the function, and the other boards' branch.
            mine = [t for n, t in sig if gated(body, n)]
            others = [t for n, t in sig if not gated(body, n)]
        need("sleepWithoutDrivers();" in mine, f"`{name}` must end in sleepWithoutDrivers() on Silkscreen")
        need("resolvePanelController();" not in mine, f"`{name}` must not carry its own copy of the sleep sequence")
        if "RenderWatchdog" in name:
            need(any("renderwatchdog::takeSleepRequest(" in t for t in mine),
                 "sleepIfRenderWatchdog() must ask renderwatchdog::takeSleepRequest()")
            need(any("renderwatchdog::describe(" in t for t in mine),
                 "sleepIfRenderWatchdog() must say what it found with renderwatchdog::describe()")
        else:
            # Boards without the power policy keep upstream's sequence in the function (the #else branch).
            need("resolvePanelController();" in others and "powerManager.startDeepSleepMinimal(gpio);" in others
                 and "gpio.resetTouchForSleep();" in others,
                 "sleepIfResetLoop() must keep upstream's own sleep sequence for the other boards (the #else branch)")
            if "sleepWithoutDrivers();" in mine:
                at = mine.index("sleepWithoutDrivers();")
                need(at > 0 and mine[at - 1] == "renderwatchdog::resetLoopSlept();",
                     "sleepIfResetLoop() must spend a pending watchdog sleep (renderwatchdog::resetLoopSlept();) in the "
                     "statement right before sleepWithoutDrivers() on Silkscreen")
    try:
        report = function_body(main, "static void logWatchdogReport(")
    except GuardError as e:
        problems.append(f"main.cpp: {e}")
    else:
        need(any("renderwatchdog::describe(" in t for _, t in significant(report)),
             "logWatchdogReport() must say what happened with renderwatchdog::describe()")
        need(any("getLastLogs()" in t for _, t in significant(report)),
             "logWatchdogReport() must print the held log ring (getLastLogs())")
    calls = [m.start() for m in re.finditer(r"^\s*sleepIfResetLoop\(\);", code, re.M)]
    watch = [m.start() for m in re.finditer(r"^\s*sleepIfRenderWatchdog\(\);", code, re.M)]
    need(len(calls) == 1 and len(watch) == 1 and calls[0] < watch[0] and "gpio.begin(" not in code[calls[0] : watch[0]],
         "setup() must call sleepIfRenderWatchdog() right after sleepIfResetLoop(), before gpio.begin()")
    need(bool(re.search(r'^\s*#\s*include\s+"util/RenderWatchdog.h"', main, re.M)),
         'main.cpp must include "util/RenderWatchdog.h"')
    # setup(): the log ring is held before HalSystem::begin() clears it, and released after the boot that stays up has
    # printed the report; the report itself is taken once. All behind the gate.
    try:
        setup = function_body(main, "void setup()")
    except GuardError as e:
        problems.append(f"main.cpp: {e}")
    else:
        sig = significant(setup)
        texts = [t for _, t in sig]

        def where(needle: str) -> list[int]:
            return [k for k, t in enumerate(texts) if needle in t]

        holds, hold_on, system_begin = where("renderwatchdog::holdsLogs()"), where("holdLastLogs(true)"), where("HalSystem::begin();")
        report, hold_off, cleared = where("renderwatchdog::takeReport("), where("holdLastLogs(false)"), where("clearLastLogs();")
        need(len(report) == 1, "setup() must call renderwatchdog::takeReport() exactly once (the boot after the sleep)")
        need(len(holds) == 1 and len(hold_on) == 1 and len(system_begin) == 1 and holds[0] < hold_on[0] < system_begin[0],
             "setup() must ask renderwatchdog::holdsLogs() and then holdLastLogs(true), both before HalSystem::begin() "
             "(which clears the log ring)")
        need(len(hold_off) == 1 and len(report) == 1 and hold_off[0] > report[0],
             "setup() must release the ring (holdLastLogs(false)) after renderwatchdog::takeReport()")
        need(len(cleared) == 1 and len(hold_off) == 1 and cleared[0] > hold_off[0],
             "setup() must clearLastLogs() after releasing the ring")
        for what, found in (("renderwatchdog::takeReport()", report), ("renderwatchdog::holdsLogs()", holds),
                            ("holdLastLogs(true)", hold_on), ("holdLastLogs(false)", hold_off),
                            ("clearLastLogs()", cleared)):
            if len(found) == 1:
                need(gated(setup, sig[found[0]][0]), f"{what} in setup() must sit behind #if {GATE}")
    return problems


# --- the progress guard ------------------------------------------------------------------------------------------------

HELPER = re.compile(r"inline void watchdogProgress\(\)\s*\{\s*#\s*if FREEINK_CAP_POWER_POLICY\s+renderwatchdog::progress\(\);"
                    r"\s*#\s*endif\s*\}")
INCLUDE = re.compile(r'#\s*if FREEINK_CAP_POWER_POLICY\s+#\s*include\s+"util/RenderWatchdog\.h"\s+#\s*endif')


def check_progress(files: dict[str, str]) -> list[str]:
    """The long renders re-stamp: every file has the gated helper, the gated include, the least number of calls, and
    every section-build chunk that the render task runs is followed by one."""
    problems: list[str] = []
    for path, least in PROGRESS_FILES.items():
        text = files.get(path)
        if text is None:
            problems.append(f"{path}: not found")
            continue
        if not HELPER.search(text):
            problems.append(f"{path}: watchdogProgress() must be `inline void` calling renderwatchdog::progress() behind "
                            f"#if {GATE}")
        if not INCLUDE.search(text):
            problems.append(f'{path}: must include "util/RenderWatchdog.h" behind #if {GATE}')
        code = strip_comments(text)
        sites = len(re.findall(r"^\s*watchdogProgress\(\);", code, re.M))
        if sites < least:
            problems.append(f"{path}: {sites} watchdogProgress() call(s), at least {least} expected")
        lines = code.split("\n")
        for n, line in enumerate(lines):
            if "buildSomeMore(" not in line or "BACKGROUND_BUILD_PAGES_PER_TICK" in line or "catchUpSucceeded =" in line:
                continue  # the loop task's background build and the sleep-page catch-up are not render-task work
            if not any("watchdogProgress();" in l for l in lines[n + 1 : n + 13]):
                problems.append(f"{path}:{n + 1}: a section-build chunk (buildSomeMore) without watchdogProgress() after it")
    epub = files.get("src/activities/reader/EpubReaderActivity.cpp", "")
    try:
        popup = function_body(epub, "void EpubReaderActivity::showBuildPopup(")
        first = [t for _, t in significant(popup)][1:2]
        if first != ["watchdogProgress();"]:
            problems.append("EpubReaderActivity::showBuildPopup() must start with watchdogProgress();")
    except GuardError as e:
        problems.append(f"EpubReaderActivity.cpp: {e}")
    if not re.search(r"const auto popupFn = \[this\]\(\) \{\s*watchdogProgress\(\);", strip_comments(epub)):
        problems.append("EpubReaderActivity.cpp: the popupFn of the full build must start with watchdogProgress();")
    return problems


# --- the logging guard -------------------------------------------------------------------------------------------------


def check_logging(cpp: str, header: str) -> list[str]:
    problems: list[str] = []
    if not re.search(r"void\s+holdLastLogs\s*\(\s*bool\b", header):
        problems.append("Logging.h must declare void holdLastLogs(bool)")
    code = strip_comments(cpp)
    define = re.search(r"void holdLastLogs\(", code)
    if not define:
        problems.append("Logging.cpp must define holdLastLogs()")
    else:
        lines = code.split("\n")
        at = code.count("\n", 0, define.start())
        if not gated(lines, at):
            problems.append(f"holdLastLogs() in Logging.cpp must sit behind #if {GATE}")
    for name, signature in (("addToLogRingBuffer", "void addToLogRingBuffer("), ("clearLastLogs", "void clearLastLogs()")):
        try:
            body = function_body(cpp, signature)
        except GuardError as e:
            problems.append(f"Logging.cpp: {e}")
            continue
        sig = significant(body)
        held = [n for n, t in sig if t == "if (logRingHeld) return;"]
        if len(held) != 1:
            problems.append(f"{name}() must return early while the ring is held (`if (logRingHeld) return;`)")
        elif not gated(body, held[0]):
            problems.append(f"the hold test in {name}() must sit behind #if {GATE}")
    return problems


# --- the numbers -------------------------------------------------------------------------------------------------------

PINNED = [
    (r"constexpr\s+uint32_t\s+BOUND_MS\s*=\s*5UL\s*\*\s*60UL\s*\*\s*1000UL;", "BOUND_MS must be 5UL * 60UL * 1000UL"),
    (r"constexpr\s+uint32_t\s+SLEEP_ENTRY_BOUND_MS\s*=\s*3UL\s*\*\s*60UL\s*\*\s*1000UL;",
     "SLEEP_ENTRY_BOUND_MS must be 3UL * 60UL * 1000UL"),
    (r"constexpr\s+uint32_t\s+TICK_MS\s*=\s*5000;", "TICK_MS must be 5000"),
]


def check_constants(header: str) -> list[str]:
    code = strip_comments(header)
    return [f"RenderWatchdog.h: {what} (the source-only guard pins it; change it here, in POWER.md and in the host test)"
            for pattern, what in PINNED if not re.search(pattern, code)]


# --- the lock guard ----------------------------------------------------------------------------------------------------

# What the task may NOT call. Anything on this list in RenderWatchdog.cpp is a call the proof of POWER.md 3.3 has not
# covered: it can wait for a lock the stuck task holds. These are the named APIs the guard refuses; the second list
# below is the other way round, and stops anything that is not on it.
FORBIDDEN = [
    (r"\bportMAX_DELAY\b", "an unbounded wait"),
    (r"\bxSemaphoreTake(Recursive)?\s*\(", "a mutex"),
    (r"\bstd::(mutex|recursive_mutex|lock_guard|unique_lock|condition_variable)\b", "a std lock"),
    (r"\bLOG_(ERR|INF|DBG)\b", "the project logger (its ring buffer and the serial lock)"),
    (r"\bESP_LOG[A-Z]*\b|\blog_[ewidv]\s*\(", "the IDF/Arduino logger"),
    (r"(?<![\w:])(printf|puts|fprintf|vprintf|fputs|putchar)\s*\(", "stdio (the stdout lock)"),
    (r"\b(Serial|logSerial)\b", "the serial port (HWCDC's lock)"),
    (r"\bWiFi\b|\besp_wifi_", "the Wi-Fi stack"),
    (r"\bWire1?\b", "the I2C driver (its bus lock)"),
    (r"\bSPI\b|\bspi[A-Z]\w*\(", "the SPI driver (its bus and transaction locks)"),
    (r"\bStorage\b|\bSdFat\b|\bHalStorage\b", "the storage mutex"),
    (r"\bnvs_\w+\s*\(|\bPreferences\b", "NVS"),
    (r"\bFrontlight\b|\bHalFrontlight\b|\bFrontlightManager\b", "the front-light manager's gate lock"),
    (r"\bpinMode\s*\(|\bdigitalWrite\s*\(|\bdigitalRead\s*\(",
     "an Arduino pin call (the pin-bus manager reaches the SPI lock)"),
    (r"\bRenderLock\b|\bactivityManager\b", "the render lock"),
    (r"\besp_restart\s*\(|\bESP\.restart\b", "the restart that runs the shutdown handlers (esp_wifi_stop, the light's)"),
    (r"\bdelay\s*\(", "delay() (use vTaskDelay or esp_rom_delay_us)"),
    (r"\bHalPowerManager\b|\bpowerManager\b", "the power manager's mode mutex"),
]
# Every free or qualified call RenderWatchdog.cpp makes (methods of objects are not listed: the file's own Stamp and
# atomics). The proof of POWER.md 3.3 covers the ones that leave the unit; the rest are the unit's own functions, which
# the same scan covers by their bodies. A call not here is a claim the proof has not looked at yet.
ALLOWED_CALLS = {
    # leaves the unit (the proof)
    "BoardConfig::hasChargeStatusLadder", "Decoder::classify", "analogReadMilliVolts", "esp_restart_noos",
    "esp_rom_delay_us", "esp_rom_printf", "gpio_set_direction", "gpio_set_level", "millis", "pdMS_TO_TICKS",
    "std::memcpy", "std::snprintf", "std::strncpy", "uxTaskGetStackHighWaterMark", "vTaskDelay", "vTaskSuspendAll",
    "xTaskCreatePinnedToCore", "xTaskGetCurrentTaskHandle",
    # the unit's own
    "beginRender", "beginSleepEntry", "checksum", "createTask", "currentBoundMs", "currentSleepEntryBoundMs",
    "darkenLight", "decide", "describe", "endRender", "holdsLogs", "loopIsStale", "noteLoopPass", "onBatteryNow",
    "progress", "reportedStackFree", "reportedThisBoot", "resetLoopSlept", "restartIntoSleep", "running",
    "setBoundForTest", "setSleepEntryBoundForTest", "start", "store", "take", "takeReport", "takeSleepRequest",
    "taskMain", "usbPresentNow",
}
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "catch", "alignas", "alignof", "decltype",
            "static_assert", "defined", "case"}
CALL = re.compile(r"(?<![\w.>])((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)\s*\(")


def check_lock_free(source: str) -> list[str]:
    code = blank_strings(strip_comments(source))
    problems = []
    for pattern, what in FORBIDDEN:
        for m in re.finditer(pattern, code):
            line = code.count("\n", 0, m.start()) + 1
            problems.append(f"RenderWatchdog.cpp:{line}: {m.group(0).strip()} is {what}")
    for m in CALL.finditer(code):
        name = m.group(1)
        if name in KEYWORDS or name in ALLOWED_CALLS:
            continue
        line = code.count("\n", 0, m.start()) + 1
        if name == "lock":
            text = code[code.rfind("\n", 0, m.start()) + 1 : code.find("\n", m.start())]
            if re.search(r"freeink::adc::Lock\s+lock\(ADC_WAIT_TICKS\)", text):
                continue  # the one lock the unit takes: the ADC lock, with the bounded wait; a held ADC lock means the cell
        problems.append(f"RenderWatchdog.cpp:{line}: {name}() is not on the guard's list of calls the proof of POWER.md "
                        "3.3 covers (add it there only with the proof)")
    if "esp_restart_noos()" not in code:
        problems.append("RenderWatchdog.cpp must end the expiry path in esp_restart_noos()")
    if not re.search(r"vTaskSuspendAll\(\);\s*esp_restart_noos\(\);", code):
        problems.append("RenderWatchdog.cpp must call vTaskSuspendAll() right before esp_restart_noos()")
    return problems


# --- the decision test ------------------------------------------------------------------------------------------------


def find_cxx() -> str:
    cxx = os.environ.get("CXX")
    if cxx:
        return cxx
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    sys.exit("test_render_watchdog: no C++ compiler (set CXX; on the Windows bench host see this script's docstring)")


def build_and_run(header_dir: Path, work: Path) -> tuple[bool, str]:
    exe = work / "render_watchdog_host_test.exe"
    cmd = [find_cxx(), "-std=c++17", "-I" + str(header_dir).replace("\\", "/"), "-o", str(exe).replace("\\", "/"),
           str(TEST).replace("\\", "/")]
    built = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    if built.returncode != 0 or not exe.exists():
        return False, "build failed:\n" + built.stdout + built.stderr
    ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
    output = ran.stdout + ran.stderr
    counted = re.search(r"(\d+) checks, (\d+) failed", output)
    if ran.returncode != 0:
        return False, output
    if not counted or int(counted.group(1)) < MIN_CHECKS:
        return False, output + f"\nthe host test reported fewer than {MIN_CHECKS} checks: it did not run its cases\n"
    return True, output


# --- the negative controls --------------------------------------------------------------------------------------------


def mutate(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        sys.exit(f"test_render_watchdog: the control's anchor is not in the source exactly once: {old!r}")
    return text.replace(old, new)


def mutate_re(text: str, pattern: str, new: str) -> str:
    done, count = re.subn(pattern, new, text)
    if count != 1:
        sys.exit(f"test_render_watchdog: the control's pattern matches {count} times, not once: {pattern!r}")
    return done


def drop_after(text: str, marker: str, target: str) -> str:
    """The text without the first `target` that follows the first `marker`."""
    at = text.find(marker)
    if at < 0:
        sys.exit(f"test_render_watchdog: the control's marker is not in the source: {marker!r}")
    hit = text.find(target, at)
    if hit < 0:
        sys.exit(f"test_render_watchdog: the control's target is not after its marker: {target!r}")
    return text[:hit] + text[hit + len(target) :]


def header_controls() -> int:
    bad = 0
    header = read(HEADER)
    guard = "if (!onBattery || stampMs == 0) return false;"
    variants = {
        # The bound check removed: nothing is ever hung.
        "no bound": mutate(header, "return runningMs >= boundMs && runningMs <= UINT32_MAX - FUTURE_GUARD_MS;", "return false;"),
        # The USB test removed: a hung render on a charger is acted on.
        "no USB test": mutate(header, guard, "if (stampMs == 0) return false;"),
        # The idle test removed: stamp 0 reads as a render that began at the epoch.
        "no idle test": mutate(header, guard, "if (!onBattery) return false;"),
        # The wrap guard removed: a stamp written after the clock was read is a render of 49 days.
        "no future guard": mutate(header, "runningMs >= boundMs && runningMs <= UINT32_MAX - FUTURE_GUARD_MS",
                                  "runningMs >= boundMs"),
        # A stamp of 0 stored for millis() == 0: the render looks idle.
        "zero stamp": mutate(header, "value_.store(nowMs != 0 ? nowMs : 1, std::memory_order_release);",
                             "value_.store(nowMs, std::memory_order_release);"),
        # The sleep entry's bound changed: the host test pins its value.
        "sleep bound changed": mutate(header, "SLEEP_ENTRY_BOUND_MS = 3UL * 60UL * 1000UL;",
                                      "SLEEP_ENTRY_BOUND_MS = 4UL * 60UL * 1000UL;"),
        # A refresh that wakes an idle mark: a render that ended leaves a stale mark behind.
        "refresh wakes an idle mark": mutate_re(header, r"while \(current != 0 &&\s+!value_\.compare_exchange_weak", "while (!value_.compare_exchange_weak"),
        # A refresh that never moves the mark: a long build is cut at the bound.
        "refresh moves nothing": mutate(header, "const uint32_t next = nowMs != 0 ? nowMs : 1;\n    while",
                                        "const uint32_t next = current;\n    while"),
        # The sleep mark ignored: a sleep entry that hangs is never decided.
        "sleep mark ignored": mutate(header, "const bool sleep = renderStuck(nowMs, sleepStampMs, sleepBoundMs, onBattery);",
                                     "const bool sleep = false;"),
        # The shorter-running mark wins: the cause of a double hang is named wrongly.
        "shorter mark wins": mutate(header, "sleepMs > renderMs", "sleepMs < renderMs"),
    }
    with tempfile.TemporaryDirectory(prefix="renderwd-") as tmp:
        work = Path(tmp)
        for name, text in variants.items():
            folder = work / name.replace(" ", "_")
            folder.mkdir()
            (folder / "RenderWatchdog.h").write_text(text, encoding="utf-8", newline="\n")
            ok, output = build_and_run(folder, work)
            if ok:
                print(f"CONTROL NOT CAUGHT: the host test passes with the header mutated as `{name}`")
                bad += 1
            else:
                last = output.strip().splitlines()[-1] if output.strip() else ""
                print(f"control `{name}`: the host test fails, as it must ({last})")
    return bad


def source_controls() -> int:
    bad = 0
    am = read(ACTIVITY_MANAGER)
    main = read(MAIN)
    stamp = "renderwatchdog::beginRender(currentActivity->name.c_str());\n"
    mark = "renderwatchdog::beginSleepEntry();\n"
    held = "  const bool watchdogLogsHeld = renderwatchdog::holdsLogs();\n  if (watchdogLogsHeld) holdLastLogs(true);\n"
    hook_controls = {
        "stamp removed": (mutate(am, stamp, ""), main),
        "stamp moved before the lock": (
            mutate(mutate(am, stamp, ""), "    RenderLock lock;\n    if (currentActivity) {",
                   '    renderwatchdog::beginRender("x");\n    RenderLock lock;\n    if (currentActivity) {'),
            main),
        "clear removed": (mutate(am, "renderwatchdog::endRender();\n", ""), main),
        "task start removed": (mutate(am, "renderwatchdog::start();\n", ""), main),
        "heartbeat removed": (am, mutate(main, "renderwatchdog::noteLoopPass();", "")),
        "early sleep call removed": (am, mutate(main, "sleepIfRenderWatchdog();\n", "")),
        "report not taken": (am, mutate(main, "renderwatchdog::takeReport(watchdogEvent)", "false")),
        "sleep-entry mark removed": (am, mutate(main, mark, "")),
        "sleep-entry mark below the lock": (
            am, mutate(mutate(main, mark, ""), "  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation\n",
                       "  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation\n"
                       "  renderwatchdog::beginSleepEntry();\n")),
        "sleep-entry mark ungated": (am, mutate(main, "#if FREEINK_CAP_POWER_POLICY\n  // POWER.md AW-04: the sleep entry has begun.",
                                                "#if 1\n  // POWER.md AW-04: the sleep entry has begun.")),
        "reset loop does not spend the request": (am, mutate(main, "renderwatchdog::resetLoopSlept();\n", "")),
        "ring not held": (am, mutate(main, held, "")),
        "ring held after HalSystem::begin": (
            am, mutate(mutate(main, held, ""), "  HalSystem::begin();\n",
                       "  HalSystem::begin();\n#if FREEINK_CAP_POWER_POLICY\n" + held + "#endif\n")),
        "ring not released": (am, mutate(main, "    holdLastLogs(false);\n", "")),
        "ring not cleared": (am, mutate_re(main, r"    clearLastLogs\(\);[^\n]*\n", "")),
        "report without a description": (am, mutate(main, "  renderwatchdog::describe(event, what, sizeof(what));\n  LOG_ERR(\"PWR\", \"The session",
                                                    "  LOG_ERR(\"PWR\", \"The session")),
        "own copy of the sleep sequence": (am, mutate(main, "  sleepWithoutDrivers();\n#else", "  resolvePanelController();\n#else")),
        "other boards' sequence changed": (
            am, mutate(main, "  gpio.resetTouchForSleep();\n  powerManager.startDeepSleepMinimal(gpio);\n#endif\n}",
                       "  powerManager.startDeepSleepMinimal(gpio);\n#endif\n}")),
        "P9 sleep-entry hang before the mark": (
            am, mutate(mutate(main, "#if CROSSPOINT_TEST_SUITE\n  testsuite::watchdog::hangSleepEntryIfArmed();", "#if 0\n  (void)0;"),
                       "  renderwatchdog::beginSleepEntry();\n",
                       "  testsuite::watchdog::hangSleepEntryIfArmed();\n  renderwatchdog::beginSleepEntry();\n")),
    }
    for name, (am_text, main_text) in hook_controls.items():
        problems = check_hooks(am_text, main_text)
        if problems:
            print(f"control `{name}`: the hook guard fails, as it must ({problems[0]})")
        else:
            print(f"CONTROL NOT CAUGHT: the hook guard passes with `{name}`")
            bad += 1
    # The long renders.
    files = {path: read(ROOT / path) for path in PROGRESS_FILES if (ROOT / path).exists()}
    epub = "src/activities/reader/EpubReaderActivity.cpp"
    home = "src/activities/home/HomeActivity.cpp"
    gate_block = "#if FREEINK_CAP_POWER_POLICY\n  renderwatchdog::progress();\n#endif\n"
    progress_controls = {
        "chunk loop without progress": {epub: drop_after(files[epub], CHUNK_MARKER, "watchdogProgress();\n")},
        "popup without progress": {epub: mutate_re(files[epub], r"(const auto popupFn = \[this\]\(\) \{\s*)watchdogProgress\(\);", r"\1")},
        "helper ungated": {epub: mutate(files[epub], gate_block, "  renderwatchdog::progress();\n")},
        "helper does not re-stamp": {home: mutate(files[home], gate_block, "")},
        "include ungated": {epub: mutate(files[epub], '#if FREEINK_CAP_POWER_POLICY\n#include "util/RenderWatchdog.h"\n#endif\n',
                                         '#include "util/RenderWatchdog.h"\n')},
        "cover loop without progress": {home: drop_after(files[home], "inline void watchdogProgress()", "watchdogProgress();")},
    }
    for name, changed in progress_controls.items():
        problems = check_progress({**files, **changed})
        if problems:
            print(f"control `{name}`: the progress guard fails, as it must ({problems[0]})")
        else:
            print(f"CONTROL NOT CAUGHT: the progress guard passes with `{name}`")
            bad += 1
    # Logging keeps the ring while it is held.
    log_cpp, log_h = read(LOGGING_CPP), read(LOGGING_H)
    hold_test = "#if FREEINK_CAP_POWER_POLICY\n  if (logRingHeld) return;\n#endif\n"
    log_controls = {
        "clear ignores the hold": (drop_after(log_cpp, "void clearLastLogs() {\n", hold_test), log_h),
        "add ignores the hold": (drop_after(log_cpp, "void addToLogRingBuffer(", hold_test), log_h),
        "hold undeclared": (log_cpp, mutate_re(log_h, r"void holdLastLogs\(bool held\);\n", "")),
    }
    for name, (cpp_text, h_text) in log_controls.items():
        problems = check_logging(cpp_text, h_text)
        if problems:
            print(f"control `{name}`: the logging guard fails, as it must ({problems[0]})")
        else:
            print(f"CONTROL NOT CAUGHT: the logging guard passes with `{name}`")
            bad += 1
    # The pinned numbers.
    header = read(HEADER)
    number_controls = {
        "render bound changed": mutate(header, "BOUND_MS = 5UL * 60UL * 1000UL;", "BOUND_MS = 10UL * 60UL * 1000UL;"),
        "sleep-entry bound changed": mutate(header, "SLEEP_ENTRY_BOUND_MS = 3UL * 60UL * 1000UL;", "SLEEP_ENTRY_BOUND_MS = 2UL * 60UL * 1000UL;"),
        "tick changed": mutate(header, "TICK_MS = 5000;", "TICK_MS = 10000;"),
    }
    for name, text in number_controls.items():
        problems = check_constants(text)
        if problems:
            print(f"control `{name}`: the source-only guard fails, as it must ({problems[0]})")
        else:
            print(f"CONTROL NOT CAUGHT: the source-only guard passes with `{name}`")
            bad += 1
    src = read(SOURCE)
    for name, extra in (("a logger call", '  LOG_ERR("RWD", "x");\n'), ("a mutex", "  xSemaphoreTake(m, portMAX_DELAY);\n"),
                        ("esp_restart", "  esp_restart();\n"), ("pinMode", "  pinMode(42, OUTPUT);\n"),
                        ("the front-light manager", "  Frontlight.prepareForSleep();\n"),
                        ("the IDF logger", '  ESP_LOGE("a", "b");\n'), ("stdio", '  printf("x");\n'),
                        ("a std lock", "  std::lock_guard<std::mutex> guard(m);\n"),
                        ("NVS", '  nvs_open("a", NVS_READWRITE, &handle);\n'),
                        ("a call that is not listed", "  somethingNew(1);\n")):
        problems = check_lock_free(src + "\nvoid x() {\n" + extra + "}\n")
        if problems:
            print(f"control `{name}`: the lock guard fails, as it must ({problems[0]})")
        else:
            print(f"CONTROL NOT CAUGHT: the lock guard passes with `{name}`")
            bad += 1
    return bad


def main_() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--control", action="store_true", help="run the negative controls; each must fail")
    parser.add_argument("--source-only", action="store_true", help="only the source guards (no compiler)")
    args = parser.parse_args()
    failed = 0
    if not args.source_only:
        with tempfile.TemporaryDirectory(prefix="renderwd-") as tmp:
            ok, output = build_and_run(HEADER.parent, Path(tmp))
        print(output.strip())
        failed += 0 if ok else 1
    for problem in check_hooks(read(ACTIVITY_MANAGER), read(MAIN)):
        print("HOOK GUARD:", problem)
        failed += 1
    files = {path: read(ROOT / path) for path in PROGRESS_FILES if (ROOT / path).exists()}
    for problem in check_progress(files):
        print("PROGRESS GUARD:", problem)
        failed += 1
    for problem in check_logging(read(LOGGING_CPP), read(LOGGING_H)):
        print("LOGGING GUARD:", problem)
        failed += 1
    for problem in check_constants(read(HEADER)):
        print("NUMBERS GUARD:", problem)
        failed += 1
    for problem in check_lock_free(read(SOURCE)):
        print("LOCK GUARD:", problem)
        failed += 1
    if failed == 0:
        print("render watchdog: " + ("hook, progress, logging, numbers and lock guards pass" if args.source_only
                                     else "decision test and the hook, progress, logging, numbers and lock guards pass"))
    if args.control:
        print("--- negative controls")
        failed += source_controls()
        if not args.source_only:
            failed += header_controls()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main_())
