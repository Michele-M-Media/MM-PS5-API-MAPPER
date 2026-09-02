@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if "%~1"=="" (
  echo Usage: drag the FULL MAP .jsonl file onto RESOLVE-MAP-WINDOWS.bat
  echo or: RESOLVE-MAP-WINDOWS.bat C:\path\full_map_fw_xxx.jsonl
  pause
  exit /b 2
)
set "MAP=%~f1"

where wsl.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] WSL was not found.
  pause
  exit /b 1
)

wsl.exe bash -lc "set -e; cd '$(wslpath '%CD%')'; MAP=$(wslpath '%MAP%'); if [ -f output/sdk_api_db.csv ]; then python3 tools/resolve_map.py --map \"$MAP\" --sdk-db output/sdk_api_db.csv --prototype-db output/sdk_api_prototypes.csv; else if [ -z \"$PS5_PAYLOAD_SDK\" ]; then if [ -d /opt/ps5-payload-sdk ]; then export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk; elif [ -d \"$HOME/ps5-payload-sdk\" ]; then export PS5_PAYLOAD_SDK=\"$HOME/ps5-payload-sdk\"; else export PS5_PAYLOAD_SDK=''; fi; fi; if [ -n \"$PS5_PAYLOAD_SDK\" ]; then python3 tools/resolve_map.py --map \"$MAP\" --sdk \"$PS5_PAYLOAD_SDK\"; else python3 tools/resolve_map.py --map \"$MAP\"; fi; fi"
if errorlevel 1 (
  echo [FAIL] Resolve failed.
  pause
  exit /b 1
)

echo [OK] v0.8 resource-chain correlation graph resolved: process/thread/VM/FD/bus/driver/module/API tables generated.
pause
