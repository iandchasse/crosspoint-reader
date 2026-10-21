"""Runs the app's host tests that need a C or C++ compiler on a host that has only MSVC: the python tests
(test/test_*.py, which call `cc` and `c++` by name and slice production source into a harness) and test/clipping_sync
(upstream's run.sh, which is CRLF in a Windows work tree). The Google Test suites in test/CMakeLists.txt are not run
(Google Test is fetched from GitHub at configure time). See README.md in this directory.

usage, from crosspoint-reader:   python -I -X utf8 scripts/silkscreen/msvc/run_app_host_tests.py [test/test_x.py ...]
With no argument it runs supported test/test_*.py suites and then test/clipping_sync. Exit code: the number of failures.

A python test that names `cc` or `c++` is run with those names replaced by this directory's cc_hostcc.bat and
cxx_hostcc.bat (hostcc.py), in a child process; the others run as they are. Default Windows runs report two documented host limitations as skips (explicit test arguments still run them): test_trusted_time.py (the stub includes POSIX <sys/time.h>) and test_patch_animatedgif.py (it
compares text with LF line ends against the CRLF files of a Windows work tree's patched .pio/libdeps).
"""
import os
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
WINDOWS_UNSUPPORTED = {
    "test_trusted_time.py": "MSVC has no POSIX sys/time.h required by its stub",
    "test_patch_animatedgif.py": "fixture compares LF text with CRLF dependency sources",
}


def run_one(path):
    """Child mode: run one python test with cc and c++ replaced."""
    path = pathlib.Path(path).resolve()
    src = path.read_text(encoding="utf-8")
    if '"cc"' not in src and '"c++"' not in src:
        return subprocess.run([sys.executable, "-B", str(path)]).returncode
    cc = repr(str(HERE / "cc_hostcc.bat").replace("\\", "/"))
    cxx = repr(str(HERE / "cxx_hostcc.bat").replace("\\", "/"))
    src = src.replace('"cc"', cc).replace('"c++"', cxx)
    exec(compile(src, str(path), "exec"), {"__file__": str(path), "__name__": "__main__"})
    return 0


def msys_path(p):
    p = str(p).replace("\\", "/")
    return "/" + p[0].lower() + p[2:] if len(p) > 2 and p[1] == ":" else p


def clipping_sync():
    sh = shutil.which("sh")
    run_sh = ROOT / "test" / "clipping_sync" / "run.sh"
    if not sh or not run_sh.is_file():
        return None
    script = run_sh.read_bytes().replace(b"\r\n", b"\n")
    env = dict(os.environ, PATH=msys_path(HERE / "bin-hostcc") + os.pathsep + os.environ["PATH"], PYTHONUTF8="1")
    # run.sh finds the tree from $0 (`cd "$(dirname "$0")/../.."`): run it from test/clipping_sync, fed to `sh -s`
    # with $0 = "sh" (so dirname is "."), not the full path of sh.exe
    return subprocess.run(["sh", "-s"], executable=sh, input=script, cwd=ROOT / "test" / "clipping_sync", env=env,
                          capture_output=True).returncode


def main(args):
    if args[:1] == ["--one"]:
        return run_one(args[1])
    tests = args or sorted(str(p.relative_to(ROOT)) for p in (ROOT / "test").glob("test_*.py"))
    failed = 0
    skipped = 0
    for t in tests:
        reason = WINDOWS_UNSUPPORTED.get(pathlib.Path(t).name) if os.name == "nt" and not args else None
        if reason:
            print("skip %s (%s)" % (t, reason))
            skipped += 1
            continue
        r = subprocess.run([sys.executable, "-B", str(pathlib.Path(__file__).resolve()), "--one", str(ROOT / t)],
                           cwd=ROOT, capture_output=True, text=True, env=dict(os.environ, PYTHONUTF8="1"))
        print("%-4s %s" % ("ok" if r.returncode == 0 else "FAIL", t))
        if r.returncode:
            failed += 1
            print("\n".join("     " + l for l in (r.stdout + r.stderr).strip().splitlines()[-6:]))
    if not args:
        r = clipping_sync()
        if r is None:
            print("skip test/clipping_sync (no sh or no run.sh)")
            skipped += 1
        else:
            print("%-4s %s" % ("ok" if r == 0 else "FAIL", "test/clipping_sync"))
            failed += r != 0
    print("%d failed, %d skipped" % (failed, skipped))
    return failed


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
