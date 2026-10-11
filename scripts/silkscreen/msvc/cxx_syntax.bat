@echo off
rem CXX for the python runners that MSVC cannot link: a syntax check with the Xtensa g++ (cxx_syntax.py).
python "%~dp0cxx_syntax.py" %*
