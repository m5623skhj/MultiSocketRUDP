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
    $trustPath = "Cert:\LocalMachine\Root\$thumbprint"
    if (-not (Test-Path -LiteralPath $trustPath)) {
        # Record the exact certificate for the always-run cleanup step before importing it.
        "thumbprint=$thumbprint" | Add-Content -LiteralPath $env:GITHUB_OUTPUT
        # Use the machine store to avoid the current-user root installation dialog in CI.
        Write-Host "Importing IntegrationTest certificate into LocalMachine\Root: $thumbprint"
        Import-Certificate -FilePath $certPath -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
        if (-not (Test-Path -LiteralPath $trustPath)) {
            throw "IntegrationTest certificate was not found after import: $trustPath"
        }
        Write-Host "IntegrationTest certificate trust registered: $thumbprint"
    } else {
        Write-Host "IntegrationTest certificate is already trusted: $thumbprint"
    }
} finally {
    $certificate.Dispose()
}
