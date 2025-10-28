@echo off

echo Generating a development certificate...
powershell -NoProfile -ExecutionPolicy Bypass -Command "New-SelfSignedCertificate -DnsName 'DevServerCert' -CertStoreLocation 'cert:\CurrentUser\My' -FriendlyName 'Development Server TLS Certificate' -KeyExportPolicy Exportable -KeyLength 2048 -KeySpec KeyExchange -KeyUsage DigitalSignature, KeyEncipherment -Type SSLServerAuthentication -NotAfter (Get-Date).AddYears(5)"

IF %ERRORLEVEL% EQU 0 (
    echo Create success with CN=DevServerCert
) ELSE (
    echo Create failed
)

pause