#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Executable = 'MultiSocketRUDP/x64/Debug/IntegrationTest.exe',
    [ValidateRange(1, 100)][int]$Iterations = 5,
    [ValidateRange(1, 600)][int]$CaseTimeoutSeconds = 180,
    [ValidateRange(1, 120)][int]$BudgetMinutes = 40,
    [string]$OutputDirectory = 'tmp/stability-results'
)

$ErrorActionPreference = 'Stop'
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
# A unique directory prevents stale XML from turning a missing test into a pass.
$runDirectory = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
$cases = @(
    'ConcurrentTrafficDisconnectAndSessionReuseAcrossWaves',
    'ReleasedSessionIdReturnsToRotationAndIsReusedAfterPoolCycle',
    'ClientDisconnectReleasesSessionAndUpdatesCounts',
    'ClientStopScenarioCompletesWithoutForcedTermination',
    'StopServerCancelsIdleTlsConnectionsAndClosesQueuedSockets',
    'ReliableAndUnreliableChannelsRoundTripAndStop'
)
$results = [Collections.Generic.List[object]]::new()
$clock = [Diagnostics.Stopwatch]::StartNew()
$failure = $null
$metadata = [ordered]@{
    commit = $env:GITHUB_SHA
    executable = $executablePath
    executableSha256 = (Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash
    machine = [Environment]::MachineName
    os = [Environment]::OSVersion.VersionString
    processorCount = [Environment]::ProcessorCount
    powershell = $PSVersionTable.PSVersion.ToString()
    iterations = $Iterations
    caseTimeoutSeconds = $CaseTimeoutSeconds
    budgetMinutes = $BudgetMinutes
    cases = $cases
}
$metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runDirectory 'environment.json') -Encoding utf8

try {
    for ($iteration = 1; $iteration -le $Iterations; $iteration++) {
        foreach ($case in $cases) {
            $remainingSeconds = [int][Math]::Floor($BudgetMinutes * 60 - $clock.Elapsed.TotalSeconds)
            if ($remainingSeconds -le 0) { throw 'Run budget exhausted before all cases completed.' }
            $timeoutSeconds = [Math]::Min($CaseTimeoutSeconds, $remainingSeconds)
            $testName = "IntegrationFixture.$case"
            $prefix = Join-Path $runDirectory ("{0:D3}-{1}" -f $iteration, $case)
            $xmlPath = "$prefix.xml"
            $arguments = @("--gtest_filter=$testName", "--gtest_output=xml:$xmlPath")
            $repro = "& '" + $Executable.Replace("'", "''") + "' '--gtest_filter=$testName'"
            Write-Host "[$iteration/$Iterations] $testName (timeout ${timeoutSeconds}s)"
            $entry = [ordered]@{
                iteration = $iteration; test = $testName; status = 'failed'; exitCode = $null
                startedUtc = [DateTime]::UtcNow.ToString('o'); durationSeconds = 0
                reproduction = $repro; xml = $xmlPath; stdout = "$prefix.stdout.log"; stderr = "$prefix.stderr.log"
                error = $null
            }
            $caseClock = [Diagnostics.Stopwatch]::StartNew()
            $process = [Diagnostics.Process]::new()
            $stdout = $null
            $stderr = $null
            $outputCopy = $null
            $errorCopy = $null
            $started = $false
            try {
                $process.StartInfo.FileName = $executablePath
                $process.StartInfo.WorkingDirectory = $runDirectory
                $process.StartInfo.UseShellExecute = $false
                $process.StartInfo.CreateNoWindow = $true
                $process.StartInfo.RedirectStandardOutput = $true
                $process.StartInfo.RedirectStandardError = $true
                # Ignore inherited GTest filtering, repetition and sharding settings.
                foreach ($key in @($process.StartInfo.Environment.Keys)) {
                    if ($key.StartsWith('GTEST_')) { $process.StartInfo.Environment.Remove($key) | Out-Null }
                }
                foreach ($argument in $arguments) { $process.StartInfo.ArgumentList.Add($argument) }
                $stdout = [IO.FileStream]::new($entry.stdout, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
                $stderr = [IO.FileStream]::new($entry.stderr, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
                if (-not $process.Start()) { throw 'Test process did not start.' }
                $started = $true
                $outputCopy = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
                $errorCopy = $process.StandardError.BaseStream.CopyToAsync($stderr)
                if (-not $process.WaitForExit($timeoutSeconds * 1000)) {
                    $entry.status = 'timeout'
                    throw "Process timed out after $timeoutSeconds seconds (including teardown)."
                }
                $entry.exitCode = $process.ExitCode
                if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($outputCopy, $errorCopy), 5000)) {
                    throw 'Output streams did not close; a child process may still be running.'
                }
                if ($process.ExitCode -ne 0) { throw "Test process exited with code $($process.ExitCode)." }
                [xml]$report = Get-Content -LiteralPath $xmlPath -Raw
                $testCases = @($report.SelectNodes('/testsuites/testsuite/testcase'))
                if ($testCases.Count -ne 1 -or $testCases[0].GetAttribute('classname') -ne 'IntegrationFixture' -or
                    $testCases[0].GetAttribute('name') -ne $case -or
                    $testCases[0].GetAttribute('status') -ne 'run' -or
                    $testCases[0].GetAttribute('result') -ne 'completed' -or
                    $report.SelectNodes('//failure | //error | //skipped').Count -ne 0) {
                    throw 'Expected exactly one completed, passing test; XML was missing the case, skipped or failed.'
                }
                $entry.status = 'passed'
            }
            catch {
                $entry.error = $_.Exception.Message
                throw
            }
            finally {
                try {
                    if ($started -and -not $process.HasExited) {
                        $process.Kill($true)
                        if (-not $process.WaitForExit(10000)) { Write-Warning 'Test process did not terminate.' }
                    }
                } catch { Write-Warning "Process cleanup: $_" }
                if ($null -ne $outputCopy -and $null -ne $errorCopy) {
                    try {
                        [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($outputCopy, $errorCopy), 5000) | Out-Null
                    } catch { Write-Warning "Output cleanup: $_" }
                }
                if ($null -ne $stdout) { $stdout.Dispose() }
                if ($null -ne $stderr) { $stderr.Dispose() }
                $process.Dispose()
                $entry.durationSeconds = [Math]::Round($caseClock.Elapsed.TotalSeconds, 3)
                $results.Add([pscustomobject]$entry)
                # Persist after every case, including the very first failure. Never retry into success.
                ConvertTo-Json -InputObject @($results.ToArray()) -Depth 5 |
                    Set-Content -LiteralPath (Join-Path $runDirectory 'results.json') -Encoding utf8
            }
        }
    }
}
catch {
    $failure = $_.Exception.Message
}
finally {
    $passed = @($results | Where-Object status -eq 'passed').Count
    $summary = @(
        '## Native stability tests', '',
        "- Passed: $passed / $($Iterations * $cases.Count) planned case runs",
        "- Elapsed: $([Math]::Round($clock.Elapsed.TotalMinutes, 2)) minutes",
        '- Isolation: one fresh process per case; no retry',
        "- Results: $runDirectory"
    )
    if ($failure) {
        $summary += "- Failure: $failure"
        if ($results.Count -gt 0 -and $results[-1].status -ne 'passed') {
            $summary += @('', 'Re-run this case from the repository root (scheduling is nondeterministic):',
                '```powershell', $results[-1].reproduction, '```')
        }
    }
    $summary | Set-Content -LiteralPath (Join-Path $runDirectory 'summary.md') -Encoding utf8
    if ($env:GITHUB_STEP_SUMMARY) { $summary | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding utf8 }
    $summary | ForEach-Object { Write-Host $_ }
}
if ($failure) { throw $failure }
