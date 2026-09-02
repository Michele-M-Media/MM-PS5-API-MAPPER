@echo off
setlocal EnableExtensions
cd /d "%~dp0"
if "%~2"=="" (
  echo Usage: COMPARE-RUNTIME-GRAPH-WINDOWS.bat BASE_RESOLVED_DIR NEW_RESOLVED_DIR
  pause
  exit /b 2
)
where wsl.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] WSL was not found.
  pause
  exit /b 1
)
wsl.exe bash -lc "set -e; cd '$(wslpath '%CD%')'; A=$(wslpath '%~f1'); B=$(wslpath '%~f2'); python3 tools/compare_runtime_graph.py \"$A\" \"$B\" --out RUNTIME_GRAPH_DIFF.md"
if errorlevel 1 (
  echo [FAIL] Runtime graph compare failed.
  pause
  exit /b 1
)
echo [OK] RUNTIME_GRAPH_DIFF.md created.
pause
