@echo off
rem CXX for the SDK's python host runners (a list-form subprocess cannot start an extensionless sh script).
python "%~dp0cxx_msvc.py" %*
