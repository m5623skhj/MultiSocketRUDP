#requires -Version 7.5
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or -not $env:GITHUB_OUTPUT) {
    throw 'This script is intended for a GitHub Actions test step.'
}

$certPath = Join-Path $PSScriptRoot '../../IntegrationTest/TestCert.cer'
$certificate = [System.Security.Cryptography.X509Certificates.X509CertificateLoader]::LoadCertificateFromFile($certPath)
try {
    $thumbprint = $certificate.Thumbprint
    if (-not (Test-Path -LiteralPath "Cert:\CurrentUser\Root\$thumbprint")) {
        # Record the exact certificate for the always-run cleanup step before importing it.
        "thumbprint=$thumbprint" | Add-Content -LiteralPath $env:GITHUB_OUTPUT
        Import-Certificate -FilePath $certPath -CertStoreLocation 'Cert:\CurrentUser\Root' | Out-Null
    }
} finally {
    $certificate.Dispose()
}
