@echo off
setlocal
cd /d "%~dp0"

where py >nul 2>&1
if %errorlevel%==0 (
  py -3 tools\usb_server.py %*
  goto :end
)

where python >nul 2>&1
if %errorlevel%==0 (
  python tools\usb_server.py %*
  goto :end
)

echo Python 3 not found. Please install Python 3 from https://www.python.org/downloads/
pause
exit /b 1

:end
if not %errorlevel%==0 pause
