[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ServerExecutable,

    [Parameter(Mandatory = $true)]
    [string]$ServerWorkingDirectory,

    [Parameter(Mandatory = $true)]
    [string]$BenchmarkDll,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string]$CommitSha,

    [int]$ZeroLossSamples = 1000,
    [int]$LossSamples = 1000,
    [int]$RunCount = 5,
    [int]$ZeroLossWarmupSamples = 500,
    [int]$LossWarmupSamples = 100,
    [int]$TimeoutMs = 5000,
    [int]$RunTimeoutSeconds = 300,
    [ValidateRange(1, 255)]
    [int]$ServerThreadCount = 1,
    [int]$SeedBase = 20260803,
    [string]$BenchmarkHost = "127.0.0.1",
    [int]$SessionBrokerPort = 11011
)

$ErrorActionPreference = "Stop"

function Wait-ForTcpPort {
    param(
        [string]$TargetHost,
        [int]$TargetPort,
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) {
            throw "Server exited before opening the session broker port. Exit code: $($Process.ExitCode)"
        }

        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $connectTask = $client.ConnectAsync($TargetHost, $TargetPort)
            if ($connectTask.Wait(500) -and $client.Connected) {
                return
            }
        }
        catch {
            # The server can still be starting. Retry until the deadline.
        }
        finally {
            $client.Dispose()
        }

        Start-Sleep -Milliseconds 250
    }

    throw "Timed out waiting for $TargetHost`:$TargetPort."
}

function Invoke-Scenario {
    param(
        [string]$Name,
        [double]$LossRate,
        [int]$Samples,
        [int]$WarmupSamples,
        [int]$ScenarioSeed,
        [string]$OutputPath
    )

    $lossRateText = $LossRate.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    & dotnet $script:benchmarkDllPath run `
        --host $BenchmarkHost `
        --port $SessionBrokerPort `
        --scenario $Name `
        --samples $Samples `
        --runs $RunCount `
        --warmup-samples $WarmupSamples `
        --timeout-ms $TimeoutMs `
        --run-timeout-seconds $RunTimeoutSeconds `
        --server-thread-count $ServerThreadCount `
        --loss-rate $lossRateText `
        --seed-base $ScenarioSeed `
        --commit $CommitSha `
        --output $OutputPath

    if ($LASTEXITCODE -ne 0) {
        throw "RTT benchmark scenario '$Name' failed with exit code $LASTEXITCODE."
    }
}

function Set-ServerThreadCount {
    param(
        [string]$CoreOptionPath,
        [int]$ThreadCount
    )

    $contents = [System.IO.File]::ReadAllText($CoreOptionPath)
    $pattern = '(?m)^([ \t]*THREAD_COUNT[ \t]*=[ \t]*)\d+([ \t]*\r?)$'
    if ([regex]::Matches($contents, $pattern).Count -ne 1) {
        throw "Expected exactly one THREAD_COUNT entry in '$CoreOptionPath'."
    }

    $updatedContents = [regex]::Replace(
        $contents,
        $pattern,
        { param($match) $match.Groups[1].Value + $ThreadCount + $match.Groups[2].Value })
    $utf8WithBom = [System.Text.UTF8Encoding]::new($true)
    [System.IO.File]::WriteAllText($CoreOptionPath, $updatedContents, $utf8WithBom)
}

$serverExecutablePath = (Resolve-Path -LiteralPath $ServerExecutable).Path
$serverWorkingDirectoryPath = (Resolve-Path -LiteralPath $ServerWorkingDirectory).Path
$script:benchmarkDllPath = (Resolve-Path -LiteralPath $BenchmarkDll).Path
$serverCoreOptionPath = Join-Path $serverWorkingDirectoryPath "ServerOptionFile/CoreOption.txt"
$serverCoreOptionOriginalBytes = [System.IO.File]::ReadAllBytes($serverCoreOptionPath)
$outputDirectoryPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputDirectoryPath -Force | Out-Null

$serverOutputPath = Join-Path $outputDirectoryPath "server.stdout.log"
$serverErrorPath = Join-Path $outputDirectoryPath "server.stderr.log"
$serverProcess = $null
$certificate = $null

try {
    Set-ServerThreadCount -CoreOptionPath $serverCoreOptionPath -ThreadCount $ServerThreadCount
    Write-Host "RTT benchmark server THREAD_COUNT=$ServerThreadCount"

    $certificate = New-SelfSignedCertificate `
        -DnsName "DevServerCert" `
        -CertStoreLocation "cert:\CurrentUser\My" `
        -FriendlyName "RTT Benchmark TLS Certificate" `
        -KeyExportPolicy Exportable `
        -KeyLength 2048 `
        -KeySpec KeyExchange `
        -KeyUsage DigitalSignature, KeyEncipherment `
        -Type SSLServerAuthentication `
        -NotAfter (Get-Date).AddDays(7)

    $serverProcess = Start-Process `
        -FilePath $serverExecutablePath `
        -WorkingDirectory $serverWorkingDirectoryPath `
        -WindowStyle Hidden `
        -RedirectStandardOutput $serverOutputPath `
        -RedirectStandardError $serverErrorPath `
        -PassThru

    Wait-ForTcpPort -TargetHost $BenchmarkHost -TargetPort $SessionBrokerPort -Process $serverProcess -TimeoutSeconds 30

    Invoke-Scenario `
        -Name "Loss 0%" `
        -LossRate 0.0 `
        -Samples $ZeroLossSamples `
        -WarmupSamples $ZeroLossWarmupSamples `
        -ScenarioSeed $SeedBase `
        -OutputPath (Join-Path $outputDirectoryPath "rtt-loss-0.json")

    Invoke-Scenario `
        -Name "TX/RX Loss 10%" `
        -LossRate 0.1 `
        -Samples $LossSamples `
        -WarmupSamples $LossWarmupSamples `
        -ScenarioSeed ($SeedBase + 1000) `
        -OutputPath (Join-Path $outputDirectoryPath "rtt-loss-10.json")
}
finally {
    if ($null -ne $serverProcess -and -not $serverProcess.HasExited) {
        Stop-Process -Id $serverProcess.Id -Force
        $serverProcess.WaitForExit(10000) | Out-Null
    }

    if ($null -ne $certificate) {
        $certificatePath = "cert:\CurrentUser\My\$($certificate.Thumbprint)"
        Remove-Item -LiteralPath $certificatePath -Force -ErrorAction SilentlyContinue
    }

    [System.IO.File]::WriteAllBytes($serverCoreOptionPath, $serverCoreOptionOriginalBytes)
}
