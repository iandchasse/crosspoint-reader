@echo off
rem c++ with -c, .c and .o support, for the python host tests (see hostcc.py).
python "%~dp0hostcc.py" cxx %*
