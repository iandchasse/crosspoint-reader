@echo off
rem cc for the python host tests (a list-form subprocess cannot start an extensionless sh script).
python "%~dp0hostcc.py" cc %*
