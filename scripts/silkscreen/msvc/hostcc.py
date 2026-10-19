"""`cc` and `c++` over MSVC's cl.exe for the runners that cxx_msvc.py cannot serve: the host tests that compile a C
file to an object first (`cc -c x.c -o x.o`) and then link that object into a C++ test (`c++ ... x.o ...`). That is
ContentProtection's run.sh (miniz) and the app's python host tests (MiniBidi). See README.md in this directory.

usage: hostcc.py cc|cxx ARGS...

It reads the arguments cxx_msvc.py reads (-D, -U, -I each joined to its value or with the value as the next argument,
-o, the sources; -std=, -W*, -w, -f*, -O* and -pthread are ignored) and adds -c, `.c` sources and `.o`/`.obj` inputs
(cl links a file of an unknown type as an object, with warning D9024). `cc` compiles C with /W1 and no C++ switches;
`cxx` compiles /std:c++20 /EHsc /utf-8 /W1 /Zc:__cplusplus with NDEBUG undefined and gnu_shim.h force-included, as
cxx_msvc.py does. A compile-only run takes one source; its -o names the object. Every argument goes to cl through a
response file under cmd /c with a timeout (CXX_MSVC_TIMEOUT seconds, default 600), as in cxx_msvc.py.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import cxx_msvc  # the same directory: rsp_line(), find_vcvars(), HERE


def main(mode, args):
    cl = ["/nologo", "/utf-8", "/W1", "/UNDEBUG"]
    if mode == "cxx":
        cl += ["/EHsc", "/std:c++20", "/Zc:__cplusplus", "/FI" + str(cxx_msvc.HERE / "gnu_shim.h").replace("\\", "/")]
    sources, objects, out, compile_only = [], [], None, False
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-D", "-U", "-I") and i + 1 < len(args):
            a += args[i + 1]
            i += 1
        if a == "-o":
            out = args[i + 1]
            i += 2
            continue
        if a == "-c":
            compile_only = True
        elif a.startswith("-D"):
            cl.append("/D" + a[2:])
        elif a.startswith("-U"):
            cl.append("/U" + a[2:])
        elif a.startswith("-I"):
            cl.append("/I" + a[2:])
        elif a.startswith(("-std=", "-W", "-w", "-f", "-O")) or a == "-pthread":
            pass
        elif a.endswith((".cpp", ".cc", ".cxx", ".c")):
            sources.append(a)
        elif a.endswith((".o", ".obj")):
            objects.append(a)
        else:
            print("hostcc: ignoring", a, file=sys.stderr)
        i += 1
    if compile_only and len(sources) != 1:
        sys.exit("hostcc: -c takes exactly one source")

    objdir = tempfile.mkdtemp(prefix="hostcc-obj-")
    try:
        lines = [cxx_msvc.rsp_line(a) for a in cl]
        if compile_only:
            lines += [cxx_msvc.rsp_line("/c"), cxx_msvc.rsp_line(sources[0])]
            lines += [cxx_msvc.rsp_line("/Fo" + str(pathlib.Path(out or "a.o")))]
        else:
            lines += [cxx_msvc.rsp_line(a) for a in sources + objects]
            lines += [cxx_msvc.rsp_line("/Fo" + objdir + "/"), cxx_msvc.rsp_line("/Fe:" + str(pathlib.Path(out or "a.exe")))]
        rsp = pathlib.Path(objdir) / "args.rsp"
        rsp.write_text("\n".join(lines) + "\n", encoding="utf-8")
        bat = pathlib.Path(objdir) / "run.bat"
        bat.write_text('@echo off\r\ncall "%s" >nul 2>nul\r\ncl @"%s"\r\n' % (cxx_msvc.find_vcvars(), rsp))
        timeout = int(os.environ.get("CXX_MSVC_TIMEOUT", "600"))
        try:
            r = subprocess.run(["cmd.exe", "/c", str(bat)], capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            print("hostcc: cl.exe did not finish in %d s" % timeout, file=sys.stderr)
            return 124
        text = "\n".join(l for l in (r.stdout or "").splitlines() if "vswhere" not in l)
        sys.stdout.write(text + "\n")
        sys.stderr.write(r.stderr or "")
        return r.returncode
    finally:
        shutil.rmtree(objdir, ignore_errors=True)


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in ("cc", "cxx"):
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2:]))
