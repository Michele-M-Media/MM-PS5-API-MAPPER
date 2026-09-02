@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ====================================================================
echo  MM PS5 API MAPPER v0.8 - RESOURCE CHAIN CORRELATION GRAPH
echo  ROOTS + DEV + ALLPROC/DYNLIB + THREADS + VM + FD RESOURCE RAW + COMPLETE BUS/DEVICE/DRIVER/METHODS + SDK/AEROLIB/NID
echo  READ-ONLY: no module loading, no discovered API execution, no patching
echo ====================================================================

where wsl.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] WSL was not found.
  pause
  exit /b 1
)

echo.
echo [1/3] Host parser/runtime/provider/header validation...
wsl.exe bash -lc "set -e; cd '$(wslpath '%CD%')'; ./HOST-VALIDATE.sh"
if errorlevel 1 goto :fail

echo.
echo [2/3] Building full known-NID/prototype catalog + exhaustive PS5 payload in WSL...
wsl.exe bash -lc "set -e; cd '$(wslpath '%CD%')'; if [ -z \"$PS5_PAYLOAD_SDK\" ]; then if [ -d /opt/ps5-payload-sdk ]; then export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk; elif [ -d \"$HOME/ps5-payload-sdk\" ]; then export PS5_PAYLOAD_SDK=\"$HOME/ps5-payload-sdk\"; else echo '[FAIL] PS5_PAYLOAD_SDK not set and no known SDK path was found'; exit 2; fi; fi; echo '[SDK]' $PS5_PAYLOAD_SDK; make clean; make"
if errorlevel 1 goto :fail

echo.
echo [3/3] Build complete.
echo [OK] BUILD PASS
echo SDK/NID catalog: %CD%\output\sdk_api_db.csv
echo SDK prototypes: %CD%\output\sdk_api_prototypes.csv
echo ELF: %CD%\output\mm_ps5_api_mapper_v0.8_resource_chain_correlation_graph.elf
echo.
echo v0.8 builds the read-only runtime/resource graph and PC-side correlation pipeline without dereferencing arbitrary raw pointers.
pause
exit /b 0

:fail
echo.
echo [FAIL] Build failed. Attach the complete console output when reporting the issue.
pause
exit /b 1
