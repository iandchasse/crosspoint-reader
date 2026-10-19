"""FreeInkDisplay's run_picco.py through the shim. The runner uses one $CXX for two steps: the Picco driver test (built
and run with cl.exe through cxx_msvc.bat) and a facade check with -fsyntax-only (which cl has no equivalent of, so it
goes to the Xtensa g++ through cxx_syntax.bat). This runs the runner's own source with the second step's compiler
swapped. From freeink-sdk:   python -B ../scripts/silkscreen/msvc/run_picco_shim.py
"""
import os
import pathlib
import sys

here = pathlib.Path(__file__).resolve().parent
runner = pathlib.Path("libs/display/FreeInkDisplay/test/host/run_picco.py").resolve()
src = runner.read_text(encoding="utf-8")
needle = 'subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-fsyntax-only"'
if needle not in src:
    sys.exit("run_picco_shim: the runner's syntax step is not the one this expects; run it by hand")
os.environ["CXX"] = str(here / "cxx_msvc.bat").replace("\\", "/")
os.environ["CXX_SYNTAX"] = str(here / "cxx_syntax.bat").replace("\\", "/")
src = src.replace(needle, 'subprocess.run([os.environ["CXX_SYNTAX"], "-std=c++17", "-fsyntax-only"')
exec(compile(src, str(runner), "exec"), {"__file__": str(runner), "__name__": "__main__"})
