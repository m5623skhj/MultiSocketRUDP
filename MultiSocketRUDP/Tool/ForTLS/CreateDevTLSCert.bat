@echo off
setlocal

set "SCRIPT_DIR=%~dp0"

echo Generating a development certificate and updating SessionGetterOption.txt...
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%CreateDevTLSCert.ps1"
set "RC=%ERRORLEVEL%"

IF %RC% EQU 0 (
    echo.
    echo Create success with CN=DevServerCert
) ELSE (
    echo.
    echo Create failed. ExitCode=%RC%
)

pause
endlocal & exit /b %RC%
