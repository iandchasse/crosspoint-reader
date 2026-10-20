#!/usr/bin/env python3
"""Host test of the Silkscreen render watchdog (docs/silkscreen/POWER.md, AW-04), and the source guards behind it.

Three parts:

1. The decision. scripts/silkscreen/render_watchdog_host_test.cpp is built against src/util/RenderWatchdog.h, the
   header the firmware compiles, and run: when a render counts as hung (idle, the bound's edge, the wrap of
   millis(), USB, a stamp from the future, waiting for the render lock).
2. The hooks. A merge that drops a hook builds every env and fails nothing, so the order the design stands on is
   read from the source: in ActivityManager::renderTaskLoop() the stamp is taken after the render lock is held and
   immediately before render(), and cleared immediately after; the task starts after the render task exists; main.cpp
   passes the loop's heartbeat, and the early sleep after the watchdog's restart shares the reset loop's sequence.
3. The expiry path takes no lock. RenderWatchdog.cpp may call only what the proof in POWER.md section 3.3 allows after
   the point where the watchdog has decided: a task that is stuck holding the SPI, I2C, ADC, storage, log or
   front-light lock must not be able to stop it.

--source-only runs parts 2 and 3 alone (no compiler needed; scripts/silkscreen/build_matrix.sh runs it). --control
runs the negative controls: part 1 against copies of the header with a check removed, part 2 against copies of the
sources with a hook removed or moved, part 3 against a copy that takes a lock. Each must fail; the command fails if
one passes.

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
TEST = Path(__file__).resolve().parent / "render_watchdog_host_test.cpp"
GATE = "FREEINK_CAP_POWER_POLICY"


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


def gated(lines: list[str], index: int) -> bool:
    """Whether lines[index] sits inside an `#if FREEINK_CAP_POWER_POLICY` (the innermost conditional is that one)."""
    stack = []
    for line in lines[: index + 1]:
        m = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", line)
        if not m:
            continue
        kind, rest = m.group(1), m.group(2).strip()
        if kind in ("if", "ifdef", "ifndef"):
            stack.append(kind == "if" and rest == GATE)
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
        else:
            # Boards without the power policy keep upstream's sequence in the function (the #else branch).
            need("resolvePanelController();" in others and "powerManager.startDeepSleepMinimal(gpio);" in others
                 and "gpio.resetTouchForSleep();" in others,
                 "sleepIfResetLoop() must keep upstream's own sleep sequence for the other boards (the #else branch)")
    calls = [m.start() for m in re.finditer(r"^\s*sleepIfResetLoop\(\);", code, re.M)]
    watch = [m.start() for m in re.finditer(r"^\s*sleepIfRenderWatchdog\(\);", code, re.M)]
    need(len(calls) == 1 and len(watch) == 1 and calls[0] < watch[0] and "gpio.begin(" not in code[calls[0] : watch[0]],
         "setup() must call sleepIfRenderWatchdog() right after sleepIfResetLoop(), before gpio.begin()")
    need(bool(re.search(r'^\s*#\s*include\s+"util/RenderWatchdog.h"', main, re.M)),
         'main.cpp must include "util/RenderWatchdog.h"')
    # setup(): the boot after the sleep takes the note the restart left, behind the gate.
    try:
        setup = function_body(main, "void setup()")
    except GuardError as e:
        problems.append(f"main.cpp: {e}")
    else:
        sig = significant(setup)
        texts = [t for _, t in sig]
        report = [k for k, t in enumerate(texts) if "renderwatchdog::takeReport(" in t]
        need(len(report) == 1, "setup() must call renderwatchdog::takeReport() exactly once (the boot after the sleep)")
        if report:
            need(gated(setup, sig[report[0]][0]), f"renderwatchdog::takeReport() must sit behind #if {GATE}")
    return problems


# --- the lock guard ----------------------------------------------------------------------------------------------------

# What the task may NOT call. Anything on this list in RenderWatchdog.cpp is a call the proof of POWER.md 3.3 has not
# covered: it can wait for a lock the stuck task holds.
FORBIDDEN = [
    (r"\bportMAX_DELAY\b", "an unbounded wait"),
    (r"\bxSemaphoreTake(Recursive)?\s*\(", "a mutex"),
    (r"\bLOG_(ERR|INF|DBG)\b", "the project logger (its ring buffer and the serial lock)"),
    (r"\b(Serial|logSerial)\b", "the serial port (HWCDC's lock)"),
    (r"\bWiFi\b|\besp_wifi_", "the Wi-Fi stack"),
    (r"\bWire1?\b", "the I2C driver (its bus lock)"),
    (r"\bSPI\b|\bspi[A-Z]\w*\(", "the SPI driver (its bus and transaction locks)"),
    (r"\bStorage\b|\bSdFat\b|\bHalStorage\b", "the storage mutex"),
    (r"\bFrontlight\b|\bHalFrontlight\b|\bFrontlightManager\b", "the front-light manager's gate lock"),
    (r"\bpinMode\s*\(|\bdigitalWrite\s*\(|\bdigitalRead\s*\(",
     "an Arduino pin call (the pin-bus manager reaches the SPI lock)"),
    (r"\bRenderLock\b|\bactivityManager\b", "the render lock"),
    (r"\besp_restart\s*\(|\bESP\.restart\b", "the restart that runs the shutdown handlers (esp_wifi_stop, the light's)"),
    (r"\bdelay\s*\(", "delay() (use vTaskDelay or esp_rom_delay_us)"),
    (r"\bHalPowerManager\b|\bpowerManager\b", "the power manager's mode mutex"),
]


def check_lock_free(source: str) -> list[str]:
    code = strip_comments(source)
    problems = []
    for pattern, what in FORBIDDEN:
        for m in re.finditer(pattern, code):
            line = code.count("\n", 0, m.start()) + 1
            problems.append(f"RenderWatchdog.cpp:{line}: {m.group(0).strip()} is {what}")
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
    return ran.returncode == 0, ran.stdout + ran.stderr


# --- the negative controls --------------------------------------------------------------------------------------------


def mutate(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        sys.exit(f"test_render_watchdog: the control's anchor is not in the source exactly once: {old!r}")
    return text.replace(old, new)


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
        "own copy of the sleep sequence": (am, mutate(main, "  sleepWithoutDrivers();\n#else", "  resolvePanelController();\n#else")),
        "other boards' sequence changed": (
            am, mutate(main, "  gpio.resetTouchForSleep();\n  powerManager.startDeepSleepMinimal(gpio);\n#endif\n}",
                       "  powerManager.startDeepSleepMinimal(gpio);\n#endif\n}")),
        "report not taken": (am, mutate(main, "renderwatchdog::takeReport(watchdogEvent)", "false")),
    }
    for name, (am_text, main_text) in hook_controls.items():
        problems = check_hooks(am_text, main_text)
        if problems:
            print(f"control `{name}`: the hook guard fails, as it must ({problems[0]})")
        else:
            print(f"CONTROL NOT CAUGHT: the hook guard passes with `{name}`")
            bad += 1
    src = read(SOURCE)
    for name, extra in (("a logger call", '  LOG_ERR("RWD", "x");\n'), ("a mutex", "  xSemaphoreTake(m, portMAX_DELAY);\n"),
                        ("esp_restart", "  esp_restart();\n"), ("pinMode", "  pinMode(42, OUTPUT);\n"),
                        ("the front-light manager", "  Frontlight.prepareForSleep();\n")):
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
    parser.add_argument("--source-only", action="store_true", help="only the hook and lock guards (no compiler)")
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
    for problem in check_lock_free(read(SOURCE)):
        print("LOCK GUARD:", problem)
        failed += 1
    if failed == 0:
        print("render watchdog: " + ("hook guard and lock guard pass" if args.source_only
                                     else "decision test, hook guard and lock guard pass"))
    if args.control:
        print("--- negative controls")
        failed += source_controls()
        if not args.source_only:
            failed += header_controls()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main_())
