$ErrorActionPreference = 'Stop'

# 1) 경로 계산: 이 스크립트(.ps1) 기준 상대 경로로 옵션 파일을 찾는다.
$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$optionPath = [System.IO.Path]::GetFullPath(
    (Join-Path $scriptDir '..\..\ContentsClient\ClientOptionFile\SessionGetterOption.txt'))

if (-not (Test-Path -LiteralPath $optionPath)) {
    throw "Option file not found: $optionPath"
}

# 2) 개발용 자체서명 인증서 생성 (기존 배치 로직과 동일)
$cert = New-SelfSignedCertificate `
    -DnsName           'DevServerCert' `
    -CertStoreLocation 'cert:\CurrentUser\My' `
    -FriendlyName      'Development Server TLS Certificate' `
    -KeyExportPolicy   Exportable `
    -KeyLength         2048 `
    -KeySpec           KeyExchange `
    -KeyUsage          DigitalSignature, KeyEncipherment `
    -Type              SSLServerAuthentication `
    -NotAfter          (Get-Date).AddYears(5)

Write-Host ("Certificate created. Subject={0} Thumbprint={1}" -f $cert.Subject, $cert.Thumbprint)

# 3) SubjectPublicKeyInfo(DER) -> SHA-256 -> hex
#    - PowerShell 7+/.NET 5+ 는 ExportSubjectPublicKeyInfo() 보유
#    - Windows PowerShell 5.1 fallback: RSA 전용 ASN.1 수동 구성
$spkiDer = $null
try { $spkiDer = $cert.PublicKey.ExportSubjectPublicKeyInfo() } catch { $spkiDer = $null }

if (-not $spkiDer) {
    if ($cert.PublicKey.Oid.Value -ne '1.2.840.113549.1.1.1') {
        throw "Unsupported public key algorithm: $($cert.PublicKey.Oid.Value). Fallback only supports RSA."
    }
    function Encode-AsnLen {
        param([int]$len)
        if ($len -lt 0x80)    { return [byte[]]@([byte]$len) }
        if ($len -lt 0x100)   { return [byte[]]@(0x81, [byte]$len) }
        if ($len -lt 0x10000) { return [byte[]]@(0x82, [byte](($len -shr 8) -band 0xFF), [byte]($len -band 0xFF)) }
        throw "ASN.1 length too large: $len"
    }
    # AlgorithmIdentifier: SEQUENCE { OID rsaEncryption(1.2.840.113549.1.1.1), NULL }
    $algId = [byte[]]@(0x30,0x0D,0x06,0x09,0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01,0x05,0x00)
    $rawKey = $cert.PublicKey.EncodedKeyValue.RawData
    # BIT STRING: 0 unused bits + rawKey
    $bitStrContent = (,[byte]0x00) + $rawKey
    $bitStr        = ,[byte]0x03 + (Encode-AsnLen $bitStrContent.Length) + $bitStrContent
    # Outer SEQUENCE
    $content = $algId + $bitStr
    $spkiDer = ,[byte]0x30 + (Encode-AsnLen $content.Length) + $content
}

$hashBytes = [System.Security.Cryptography.SHA256]::Create().ComputeHash($spkiDer)
$hex       = ($hashBytes | ForEach-Object { $_.ToString('x2') }) -join ''
Write-Host ("EXPECTED_SPKI_SHA256 = {0}" -f $hex)

# 4) 옵션 파일(UTF-16 LE + BOM) 갱신
#    - SESSION_BROKER 블록 안에 EXPECTED_SPKI_SHA256 라인을 삽입하거나 교체
#    - 기존 외의 다른 내용은 건드리지 않음 (idempotent)
$newLine  = "`tEXPECTED_SPKI_SHA256 = `"$hex`""
$lines    = [System.IO.File]::ReadAllLines($optionPath, [System.Text.Encoding]::Unicode)
$result   = New-Object System.Collections.Generic.List[string]
$inBroker = $false
$done     = $false

foreach ($ln in $lines) {
    if ($ln -match '^\s*:SESSION_BROKER\s*$') { $inBroker = $true }

    if ($inBroker -and -not $done -and $ln -match '^\s*EXPECTED_SPKI_SHA256\s*=') {
        # 기존 값이 있으면 교체
        $result.Add($newLine) | Out-Null
        $done = $true
        continue
    }

    if ($inBroker -and -not $done -and $ln -match '^\s*\}\s*$') {
        # SESSION_BROKER 블록 종료 직전에 삽입
        $result.Add($newLine) | Out-Null
        $done = $true
        $inBroker = $false
    }

    if ($ln -match '^\s*\}\s*$') { $inBroker = $false }

    $result.Add($ln) | Out-Null
}

if (-not $done) {
    throw "SESSION_BROKER block not found in $optionPath"
}

# UTF-16 LE + BOM 로 저장 (원본 인코딩 유지)
[System.IO.File]::WriteAllLines($optionPath, $result, [System.Text.Encoding]::Unicode)
Write-Host ("Option file updated: {0}" -f $optionPath)
