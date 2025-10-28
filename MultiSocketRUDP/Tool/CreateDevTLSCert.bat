@echo off

New-SelfSignedCertificate `
    -DnsName "DevServerCert" `
    -CertStoreLocation "cert:\LocalMachine\My" `
    -FriendlyName "Development Server TLS Certificate" `
    -KeyExportPolicy Exportable `
    -KeyLength 2048 `
    -KeySpec KeyExchange `
    -KeyUsage DigitalSignature, KeyEncipherment `
    -Type SSLServerAuthentication `
    -NotAfter (Get-Date).AddYears(5)


IF %ERRORLEVEL% EQU 0 
(
    echo CN=DevServerCert 인증서 생성 완료.
    echo certlm.msc -> 개인(My) -> 인증서 에서 확인 가능
)
ELSE 
(
    echo 인증서 생성 실패. 관리자 권한과 PowerShell 실행 정책을 확인하세요.
)

pause