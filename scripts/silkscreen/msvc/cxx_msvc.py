"""A GNU-style `c++` over MSVC's cl.exe, so the SDK's host-test runners (their `c++ ...` lines and `$CXX`) can
build and run on a host that has Visual Studio Build Tools and no g++. See README.md in this directory.

It translates -D, -U, -I, -o and the sources, ignores -std=, -W*, -f*, -O* and -pthread (it always compiles
/std:c++20 /EHsc /utf-8 /W1 /Zc:__cplusplus, with NDEBUG undefined so assert() holds) and force-includes
gnu_shim.h for the GNU builtins the SDK's drivers use. Every argument goes to cl through a response file (a
line that holds a space is quoted): a batch file splits /DX=1 at the "=", and Start-Process -Wait on cl.exe
hangs on the lingering mspdbsrv, so the compile runs under subprocess with a timeout (CXX_MSVC_TIMEOUT
seconds, default 600).

Environment: VCVARS names vcvars64.bat if it is not in one of the default Visual Studio 2022 places.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
VS_ROOT = r"C:\Program Files (x86)\Microsoft Visual Studio\2022"
VCVARS_DEFAULTS = [
    VS_ROOT + r"\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    r"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
    r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
]


def rsp_line(arg):
    """One response-file line: forward slashes, and quotes when it holds a space (cl splits unquoted lines there)."""
    arg = arg.replace("\\", "/")
    return '"%s"' % arg if " " in arg or "\t" in arg else arg


def find_vcvars():
    named = os.environ.get("VCVARS")
    candidates = ([named] if named else []) + VCVARS_DEFAULTS
    for path in candidates:
        if path and os.path.isfile(path):
            return path
    sys.exit("cxx_msvc: vcvars64.bat not found (set VCVARS to its path)")


def main(args):
    cl = [
        "/nologo",
        "/EHsc",
        "/utf-8",
        "/W1",
        "/std:c++20",
        "/Zc:__cplusplus",
        "/UNDEBUG",
        "/FI" + str(HERE / "gnu_shim.h").replace("\\", "/"),
    ]
    sources, out = [], None
    i = 0
    while i < len(args):
        a = args[i]
        if a == "-o":
            out = args[i + 1]
            i += 2
            continue
        if a.startswith("-D"):
            cl.append("/D" + a[2:])
        elif a.startswith("-U"):
            cl.append("/U" + a[2:])
        elif a.startswith("-I"):
            cl.append("/I" + a[2:])
        elif a.startswith(("-std=", "-W", "-f", "-O")) or a == "-pthread":
            pass
        elif a.endswith((".cpp", ".cc", ".cxx")):
            sources.append(a)
        else:
            print("cxx_msvc: ignoring", a, file=sys.stderr)
        i += 1
    out = str(pathlib.Path(out or "a.exe"))

    objdir = tempfile.mkdtemp(prefix="msvc-obj-")
    try:
        rsp = pathlib.Path(objdir) / "args.rsp"
        lines = [rsp_line(a) for a in cl + sources]
        lines += [rsp_line("/Fo" + objdir + "/"), rsp_line("/Fe:" + out)]
        rsp.write_text("\n".join(lines) + "\n", encoding="utf-8")
        bat = pathlib.Path(objdir) / "run.bat"
        bat.write_text('@echo off\r\ncall "%s" >nul 2>nul\r\ncl @"%s"\r\n' % (find_vcvars(), rsp))
        timeout = int(os.environ.get("CXX_MSVC_TIMEOUT", "600"))
        try:
            r = subprocess.run(["cmd.exe", "/c", str(bat)], capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            print("cxx_msvc: cl.exe did not finish in %d s" % timeout, file=sys.stderr)
            return 124
        text = "\n".join(l for l in (r.stdout or "").splitlines() if "vswhere" not in l)
        sys.stdout.write(text + "\n")
        sys.stderr.write(r.stderr or "")
        return r.returncode
    finally:
        shutil.rmtree(objdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
