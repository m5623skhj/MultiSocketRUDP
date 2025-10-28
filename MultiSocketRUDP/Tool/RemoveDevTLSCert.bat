@echo off

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
"Get-ChildItem -Path Cert:\LocalMachine\My | Where-Object {$_.Subject -like '*CN=DevServerCert*'} | Remove-Item"

IF %ERRORLEVEL% EQU 0
(
    echo CN=DevServerCert 인증서 삭제 완료.
)
ELSE
(
    echo 인증서 삭제 실패. 관리자 권한과 PowerShell 실행 정책을 확인하세요.
)

pause