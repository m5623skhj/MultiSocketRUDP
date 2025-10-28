@echo off

echo Deleting development TLS certificate...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq 'CN=DevServerCert' } | Remove-Item"

IF %ERRORLEVEL% EQU 0 (
    echo Certificate deletion successful
) ELSE (
    echo Certificate deletion failed
)

pause