@echo off
setlocal
cd /d "%~dp0"
wsl.exe bash -lc "cd '$(wslpath '%CD%')' && ./HOST-VALIDATE.sh"
if errorlevel 1 (echo [FAIL] Host validation failed & pause & exit /b 1)
echo [OK] HOST VALIDATION PASS
pause
