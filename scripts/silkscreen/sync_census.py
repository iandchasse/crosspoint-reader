#!/usr/bin/env python3
"""Silkscreen sync census: where a merge can cost battery, hold a radio on, or take a function away from the keys.

A merge that builds can still add a transfer that ignores the low-battery cutoff, a Wi-Fi flow that never switches the
radio off, a new wake source or timer, or a touch-only control. This tool lists those places ("surfaces") by pattern, for
two revisions of the tree, and prints what a merge ADDED or REMOVED. The reviewer then reads only those lines, not the
whole merge. The patterns are in RULES below; a surface is (rule, file, normalised line text) with a count, so moving
code inside a file changes nothing and a changed line shows up as one removed and one added.

  sync_census.py snapshot [REV]            print the surfaces of REV (default HEAD) of the app and the SDK it pins
  sync_census.py diff OLD NEW              surfaces added / removed between two app revisions (or snapshot files)
  sync_census.py check [REV]               diff scripts/silkscreen/census_baseline.txt against REV (default HEAD)
  sync_census.py accept [REV]              rewrite the baseline from REV once a reviewer has read the diff
  sync_census.py overlap OLD NEW           files the merge changed that the Silkscreen port has also changed

Exit status of `check` / `diff`: 0 no change, 1 surfaces added or removed, 2 error. Read-only except `accept`.
Only git is needed (no checkout of the old revision): it reads revisions with `git grep` and `git show`.
"""
import collections
import os
import re
import subprocess
import sys

ROOT = os.environ.get("CENSUS_ROOT") or os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SDK = os.path.join(ROOT, "freeink-sdk")
BASELINE = os.path.join(ROOT, "scripts", "silkscreen", "census_baseline.txt")

# The cutoff API names: a function that has one of these in its body polls the low-battery cutoff (POWER.md BAT-01).
CUTOFF = re.compile(r"lowBatteryCutoff|cutoffAbort|CutoffAbort|pollLowBatteryCutoff|isLowBatteryCutoff|cutoffTripped")

# rule id -> (what a reader should check, regex, which repos). The regexes are PCRE (git grep -P).
RULES = [
    ("NET", "a network client or transfer: it must poll the low-battery cutoff (BAT-01) and have a bound",
     r"(HTTPClient|SecureHttpClient (?!&)|NetworkClient|WiFiClient|WiFiUDP|esp_http_client|"
     r"HttpDownloader::(download|fetch|post|get|put)\w*\(|fetchUrl\(|postForm\(|postToFile\(|fetchResumable\(|"
     r"readFileToStream|sendRequest\(|pluginhttp::request|AsyncWebServer|WebServer |esp_websocket|esp_tls|mbedtls_ssl_)"),
    ("CUTOFF", "the cutoff API itself: a removed line is a dropped poll",
     CUTOFF.pattern),
    ("RADIO", "Wi-Fi or Bluetooth on/off: every flow that turns a radio on must turn it off when the flow ends (AW-02)",
     r"(WiFi\.(mode|begin|softAP|disconnect|enableSTA|setSleep|scanNetworks)|esp_wifi_|NimBLE|BLEDevice|esp_bt_|"
     r"esp_ble_|esp_now|wifiOff|WIFI_OFF|startBluetooth|stopBluetooth|BtRemote|BleRemote)"),
    ("WAKE", "sleep, wake sources, timers, clocks: the sleep floor and the wake list (POWER.md section 2)",
     r"(esp_sleep_|esp_timer_create|esp_timer_start|xTimerCreate|xTimerStart|gpio_wakeup_enable|gpio_hold_|rtc_gpio_|"
     r"esp_light_sleep|lightSleep|deepSleep|startDeepSleep|armWakeOnPins|esp_pm_|setCpuFrequencyMhz|esp_deep_sleep)"),
    ("AWAKE", "the awake ceiling and the idle sleep timeout: input sources must not lift a ceiling (AW-01, AW-03)",
     r"(allowSleepAt|sleepTimeout|SLEEP_TIMEOUT|awakeCeiling|keepAwake|inhibitSleep|preventSleep|lastActivity|"
     r"AUTO_SLEEP|idleTimeout|resetSleepTimer|notifyActivity)"),
    ("SD", "SD card power and mount: the card sits behind a PMOS gate and is powered only while used",
     r"(SdMan\.(mount|unmount|begin|end|powerDown|powerUp|ensure)|Storage\.(begin|end|mount|unmount|powerDown|powerUp)|"
     r"sdmmc_|SD\.begin|SdFat::begin|sdPower)"),
    ("LIGHT", "the front light: lit only by a user action or Restore Light on Wake",
     r"(Frontlight|frontlight|setBrightness\(|WARMLIGHT|warmLight)"),
    ("PINS", "GPIO, ADC and PWM calls: a pin that floats or a pull that drains in sleep",
     r"(pinMode\(|gpio_set_direction|gpio_config\(|gpio_set_level|gpio_pullup|gpio_pulldown|gpio_set_pull_mode|"
     r"ledcAttach|ledcWrite|analogRead|adc_oneshot|adc1_get_raw)"),
    ("TOUCH", "touch-only controls: every function a touch reaches must also be reachable with the keys (key access)",
     r"(hasTouch\(\)|touchInProgress|TouchRegistry|onTouch|handleTouch|onTap\b|onLongPress|onSwipe|isTouch|"
     r"[Gg]esture|[Ll]ongPress|[Ss]wipe)"),
]

# Where to look. Tests, docs, generated tables and the owner's test firmware are not production behaviour.
APP_PATHS = ["src", "lib"]
APP_EXCLUDE = [":(exclude)src/activities/testsuite", ":(exclude,glob)lib/**/test/**", ":(exclude,glob)lib/**/tests/**",
               ":(exclude,glob)lib/**/*.generated.h"]
SDK_PATHS = ["libs"]
SDK_EXCLUDE = [":(exclude,glob)libs/**/test/**", ":(exclude,glob)libs/**/tests/**", ":(exclude,glob)libs/**/docs/**",
               ":(exclude,glob)libs/assets/**"]


def git(repo, *args, check=True):
    p = subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check and p.returncode not in (0, 1):  # git grep exits 1 for "no match"
        sys.stderr.write(p.stderr)
        sys.exit(2)
    return p.stdout


def norm(text):
    text = re.sub(r"\s+", " ", text.strip())
    return text[:170]


def sdk_rev_of(app_rev):
    out = git(ROOT, "ls-tree", app_rev, "freeink-sdk").split()
    return out[2] if len(out) >= 3 else None


def surfaces(repo, rev, paths, exclude, prefix):
    """{(rule, path, text): [line numbers]} for one repo at one revision."""
    found = collections.defaultdict(list)
    for rule, _why, rx in RULES:
        out = git(repo, "grep", "-I", "-n", "-P", "--no-color", "-e", rx, rev, "--", *paths, *exclude)
        for line in out.splitlines():
            # git grep REV prints "REV:path:lineno:text"
            rest = line[len(rev) + 1:] if line.startswith(rev + ":") else line
            try:
                path, lineno, text = rest.split(":", 2)
            except ValueError:
                continue
            if rule == "TOUCH" and not (path.startswith("src/activities/") or path.startswith("src/components/")
                                         or path.startswith("src/MappedInput") or path.startswith("lib/hal/")):
                continue  # touch words in the SDK and in the engine libraries are the touch drivers, not UI
            if text.lstrip().startswith(("//", "*", "/*")):
                continue  # a comment is not a surface
            found[(rule, prefix + path, norm(text))].append(int(lineno))
    return found


def snapshot(app_rev):
    app_rev = git(ROOT, "rev-parse", app_rev).strip()
    snap = surfaces(ROOT, app_rev, APP_PATHS, APP_EXCLUDE, "")
    sdk_rev = sdk_rev_of(app_rev)
    if sdk_rev and os.path.isdir(SDK):
        snap.update(surfaces(SDK, sdk_rev, SDK_PATHS, SDK_EXCLUDE, "sdk:"))
    return snap, app_rev, sdk_rev


def to_lines(snap, app_rev, sdk_rev):
    out = ["# silkscreen sync census; app %s, sdk %s; RULE<TAB>path<TAB>count<TAB>text" % (app_rev[:8], (sdk_rev or "?")[:7])]
    for (rule, path, text), lines in sorted(snap.items()):
        out.append("%s\t%s\t%d\t%s" % (rule, path, len(lines), text))
    return out


def read_file(path):
    snap = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            rule, p, n, text = line.rstrip("\n").split("\t", 3)
            snap[(rule, p, text)] = [0] * int(n)
    return snap


def load(arg):
    if os.path.isfile(arg):
        return read_file(arg), ("baseline" if os.path.abspath(arg) == os.path.abspath(BASELINE) else os.path.basename(arg))
    snap, app_rev, _ = snapshot(arg)
    return snap, app_rev[:8]


_SRC = {}


def function_of(repo, rev, path, lineno):
    """(enclosing function head, True when its body mentions the cutoff API) for a line, best effort."""
    key = (repo, rev, path)
    if key not in _SRC:
        _SRC[key] = git(repo, "show", "%s:%s" % (rev, path), check=False).splitlines()
    src = _SRC[key]
    i = min(lineno, len(src)) - 1
    head = None
    start = 0
    while i >= 0 and lineno - i < 400:
        s = src[i]
        if s and not s[0].isspace() and not s.startswith(("#", "//", "}", "{", "/*", "*")) and "(" in s \
                and not s.rstrip().endswith(";"):
            head = s.strip()
            start = i
            break
        i -= 1
    if head is None:
        return None, None
    j = start + 1
    while j < len(src) and not src[j].startswith("}"):
        j += 1
    body = "\n".join(src[start:j + 1])
    return norm(head)[:90], bool(CUTOFF.search(body))


def diff(old, new):
    a, a_name = load(old)
    b, b_name = load(new)
    new_rev = None
    if not os.path.isfile(new):
        new_rev = git(ROOT, "rev-parse", new).strip()
    sdk_new = sdk_rev_of(new_rev) if new_rev else None
    delta = collections.Counter({k: len(v) for k, v in b.items()})
    delta.subtract({k: len(v) for k, v in a.items()})
    adds = {k: n for k, n in delta.items() if n > 0}
    rems = {k: -n for k, n in delta.items() if n < 0}
    # A line that left one file and appeared in another (a function moved) is not a new surface.
    moved = 0
    by_text_add = collections.defaultdict(list)
    by_text_rem = collections.defaultdict(list)
    for k in adds:
        by_text_add[(k[0], k[2])].append(k)
    for k in rems:
        by_text_rem[(k[0], k[2])].append(k)
    for t, ra in by_text_add.items():
        rr = by_text_rem.get(t)
        if not rr:
            continue
        na = sum(adds[k] for k in ra)
        nr = sum(rems[k] for k in rr)
        m = min(na, nr)
        moved += m
        for group, dct in ((ra, adds), (rr, rems)):
            left = m
            for k in group:
                take = min(left, dct[k])
                dct[k] -= take
                left -= take
                if dct[k] == 0:
                    del dct[k]
    why = {r[0]: r[1] for r in RULES}
    print("census %s -> %s: %d surface(s) added, %d removed%s" % (
        a_name, b_name, sum(adds.values()), sum(rems.values()), (", %d moved between files (not listed)" % moved) if moved else ""))
    for rule, _w, _rx in RULES:
        ra = sorted((k, n) for k, n in adds.items() if k[0] == rule)
        rr = sorted((k, n) for k, n in rems.items() if k[0] == rule)
        if not ra and not rr:
            continue
        print("\n[%s] %s" % (rule, why[rule]))
        groups = collections.OrderedDict()
        for (_r, path, text), n in ra:
            lines = b.get((rule, path, text), [])
            fn, polls = None, None
            if new_rev and lines and lines[0]:
                repo, rev, p = (SDK, sdk_new, path[4:]) if path.startswith("sdk:") else (ROOT, new_rev, path)
                if rev:
                    fn, polls = function_of(repo, rev, p, lines[0])
            groups.setdefault((path, fn), {"n": 0, "polls": polls, "texts": [], "lines": []})
            g = groups[(path, fn)]
            g["n"] += n
            g["texts"].append(text)
            g["lines"].extend(lines[:2])
        for (path, fn), g in groups.items():
            flag = ""
            if rule == "NET" and g["polls"] is not None:
                flag = "  [cutoff polled]" if g["polls"] else "  [NO cutoff poll in this function]"
            ln = ",".join(str(x) for x in g["lines"][:3] if x)
            print("  + %s%s%s%s" % (path, (":" + ln) if ln else "", ("  in " + fn) if fn else "", flag))
            for t in g["texts"][:2]:
                print("        %s" % t[:120])
            if len(g["texts"]) > 2:
                print("        ... and %d more line(s) of the same function" % (len(g["texts"]) - 2))
        for (_r, path, text), n in rr:
            print("  - %s%s: %s" % (path, " x%d" % n if n > 1 else "", text[:110]))
    return 1 if (adds or rems) else 0


def overlap(old, new):
    """Files this merge changed (old..new) that the Silkscreen port had changed relative to upstream before it."""
    old = git(ROOT, "rev-parse", old).strip()
    new = git(ROOT, "rev-parse", new).strip()
    base = git(ROOT, "merge-base", old, "upstream/develop").strip()
    ours = set(git(ROOT, "diff", "--name-only", base, old).split())
    rows = []
    for line in git(ROOT, "diff", "--numstat", old, new).splitlines():
        a, r, p = line.split("\t", 2)
        if p in ours:
            rows.append((int(a) + int(r) if a != "-" else 0, a, r, p))
    rows.sort(reverse=True)
    print("overlap: the merge changed %d file(s) the port had changed (of %d the port changed)" % (len(rows), len(ours)))
    for _t, a, r, p in rows[:40]:
        print("  +%s -%s  %s" % (a, r, p))
    return 0


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0
    cmd = argv[1]
    if cmd == "snapshot":
        snap, app_rev, sdk_rev = snapshot(argv[2] if len(argv) > 2 else "HEAD")
        print("\n".join(to_lines(snap, app_rev, sdk_rev)))
        return 0
    if cmd == "diff" and len(argv) == 4:
        return diff(argv[2], argv[3])
    if cmd == "check":
        if not os.path.isfile(BASELINE):
            print("no baseline (%s): run `accept` first" % BASELINE)
            return 2
        return diff(BASELINE, argv[2] if len(argv) > 2 else "HEAD")
    if cmd == "accept":
        snap, app_rev, sdk_rev = snapshot(argv[2] if len(argv) > 2 else "HEAD")
        with open(BASELINE, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(to_lines(snap, app_rev, sdk_rev)) + "\n")
        print("baseline written: %d surfaces (%s)" % (len(snap), BASELINE))
        return 0
    if cmd == "overlap" and len(argv) == 4:
        return overlap(argv[2], argv[3])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
