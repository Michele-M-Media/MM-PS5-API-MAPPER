@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not exist ps5_ip.txt (
  echo [FAIL] ps5_ip.txt not found. Copy ps5_ip.txt.example to ps5_ip.txt and set your PS5 IP address.
  pause
  exit /b 1
)
set /p PS5IP=<ps5_ip.txt
set "ELF=output\mm_ps5_api_mapper_v0.8_resource_chain_correlation_graph.elf"

where wsl.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] WSL was not found.
  pause
  exit /b 1
)
if not exist "%ELF%" (
  echo [FAIL] ELF not found. Build first with BUILD-WINDOWS.bat
  pause
  exit /b 1
)

echo Sending MM PS5 API Mapper v0.8 RESOURCE CHAIN CORRELATION GRAPH to %PS5IP%:9021 ...
wsl.exe bash -lc "set -e; cd '$(wslpath '%CD%')'; if [ -x /opt/ps5-payload-sdk/bin/prospero-deploy ]; then /opt/ps5-payload-sdk/bin/prospero-deploy -h '%PS5IP%' -p 9021 output/mm_ps5_api_mapper_v0.8_resource_chain_correlation_graph.elf; elif command -v prospero-deploy >/dev/null 2>&1; then prospero-deploy -h '%PS5IP%' -p 9021 output/mm_ps5_api_mapper_v0.8_resource_chain_correlation_graph.elf; else echo '[FAIL] prospero-deploy not found in WSL'; exit 127; fi"
if errorlevel 1 (
  echo [FAIL] Send failed. Attach the complete console output when reporting the issue.
  pause
  exit /b 1
)

echo.
echo [OK] Payload sent. Resource/device correlation capture is running on the PS5.
echo The mapper captures the configured read-only runtime, FD resource, bus, device and driver evidence without modifying system state.
echo Results: /data/MM_PS5_API_MAP/LATEST.txt plus FULL MAP and FULL SUMMARY.
echo Wait for: [PASS] RESOURCE CHAIN CORRELATION CAPTURE COMPLETE
pause
