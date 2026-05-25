@echo off
setlocal

set "ROOT_DIR=%~dp0..\.."
set "OUTPUT_PATH=%ROOT_DIR%\IntegrationTest\TestCert.pfx"
set "CERT_PASSWORD=MultiSocketRUDPIntegrationTest!"

echo Generating integration test TLS certificate...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$outputPath = [System.IO.Path]::GetFullPath('%OUTPUT_PATH%');" ^
    "$password = ConvertTo-SecureString '%CERT_PASSWORD%' -AsPlainText -Force;" ^
    "New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($outputPath)) | Out-Null;" ^
    "Remove-Item -LiteralPath $outputPath -ErrorAction SilentlyContinue;" ^
    "$cert = New-SelfSignedCertificate -DnsName 'DevServerCert' -CertStoreLocation 'cert:\CurrentUser\My' -FriendlyName 'Integration Test TLS Certificate' -KeyExportPolicy Exportable -KeyLength 2048 -KeySpec KeyExchange -KeyUsage DigitalSignature, KeyEncipherment -Type SSLServerAuthentication -NotAfter (Get-Date).AddYears(5);" ^
    "try { Export-PfxCertificate -Cert $cert -FilePath $outputPath -Password $password | Out-Null } finally { if ($cert) { Remove-Item -LiteralPath ('cert:\CurrentUser\My\' + $cert.Thumbprint) -ErrorAction SilentlyContinue } }"

if %errorlevel% neq 0 (
    echo Create failed
    exit /b %errorlevel%
)

echo Create success: %OUTPUT_PATH%
exit /b 0
