#requires -Version 7.0
[CmdletBinding()]
param([string]$OutputDirectory = 'tmp/asan')

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runRoot = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ([guid]::NewGuid().ToString('N'))
$bin = Join-Path $runRoot 'bin'
New-Item -ItemType Directory -Path $bin -Force | Out-Null
$originalPath = $env:PATH
$originalOptions = $env:ASAN_OPTIONS
$failure = $null
$passedTests = 0

function Invoke-CheckedBuild([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $runRoot 'build.log') -Append | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Command failed (exit $LASTEXITCODE)." }
}

# Drain stdout/stderr concurrently, bound the process lifetime, and preserve crash output.
function Invoke-TestProcess([string]$Executable, [string[]]$Arguments, [string]$Name, [int]$TimeoutSeconds) {
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = $Executable
    $process.StartInfo.WorkingDirectory = $runRoot
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    foreach ($key in @($process.StartInfo.Environment.Keys)) {
        if ($key.StartsWith('GTEST_')) { $process.StartInfo.Environment.Remove($key) | Out-Null }
    }
    foreach ($argument in $Arguments) { $process.StartInfo.ArgumentList.Add($argument) }
    $started = $false
    $stdoutTask = $null
    $stderrTask = $null
    try {
        $started = $process.Start()
        if (-not $started) { throw "$Name did not start." }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { throw "$Name timed out after $TimeoutSeconds seconds." }
        return $process.ExitCode
    } finally {
        if ($started -and -not $process.HasExited) {
            $process.Kill($true)
            $process.WaitForExit(10000) | Out-Null
        }
        if ($null -ne $stdoutTask -and $null -ne $stderrTask) {
            if ([Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($stdoutTask, $stderrTask), 5000)) {
                $stdoutTask.Result | Set-Content -LiteralPath (Join-Path $runRoot "$Name.stdout.log")
                $stderrTask.Result | Set-Content -LiteralPath (Join-Path $runRoot "$Name.stderr.log")
            } else { throw "$Name output streams did not close." }
        }
        $process.Dispose()
    }
}

try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Visual Studio C++ tools were not found.' }
    & (Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $env:ASAN_OPTIONS = 'halt_on_error=1:alloc_dealloc_mismatch=1'
    $toolBin = Split-Path (Get-Command cl.exe).Source
    if (-not (Test-Path (Join-Path $toolBin 'clang_rt.asan_dynamic-x86_64.dll'))) {
        throw 'Install the MSVC AddressSanitizer component in Visual Studio.'
    }
    $env:PATH = "$toolBin;$env:PATH"
    @("Commit: $env:GITHUB_SHA", "Compiler: $toolBin", "Options: $env:ASAN_OPTIONS") |
        Set-Content -LiteralPath (Join-Path $runRoot 'environment.txt')

    # Build GTest with the same compiler, runtime and instrumentation as project code.
    $gtest = Join-Path $repository 'external/CommonCode/Common/googletest-main/googletest'
    foreach ($sourceName in @('gtest-all', 'gtest_main')) {
        $object = Join-Path $runRoot "$sourceName.obj"
        Invoke-CheckedBuild cl.exe @('/nologo', '/c', '/EHsc', '/std:c++20', '/MTd', '/Zi', '/Od', '/fsanitize=address',
            "/I$gtest", "/I$gtest/include", "/Fo$object", "/Fd$runRoot/gtest.pdb", "$gtest/src/$sourceName.cc")
        $libraryName = if ($sourceName -eq 'gtest-all') { 'gtest' } else { 'gtest_main' }
        Invoke-CheckedBuild lib.exe @('/nologo', "/OUT:$bin/$libraryName.lib", $object)
    }
    Invoke-CheckedBuild MSBuild.exe @((Join-Path $repository 'MultiSocketRUDP/MultiSocketRUDP.sln'),
        '/m', '/t:CoreTest', '/p:Configuration=Debug', '/p:Platform=x64', '/p:EnableASAN=true',
        "/p:AsanRoot=$runRoot", "/p:ForceImportBeforeCppTargets=$PSScriptRoot/ASan.targets", '/v:normal')
    Copy-Item -LiteralPath (Join-Path $repository 'MultiSocketRUDP/CoreTest/ProtocolInteropV2Vector.json') -Destination $bin

    $probe = Join-Path $bin 'DetectionProbe.exe'
    Invoke-CheckedBuild cl.exe @('/nologo', '/MTd', '/Zi', '/Od', '/fsanitize=address',
        "/Fe$probe", "/Fo$runRoot/DetectionProbe.obj", "/Fd$runRoot/DetectionProbe.pdb",
        (Join-Path $PSScriptRoot 'DetectionProbe.cpp'), '/link', '/INCREMENTAL:NO')
    $probeExit = Invoke-TestProcess $probe @() 'probe' 30
    $probeLog = Get-Content -LiteralPath (Join-Path $runRoot 'probe.stderr.log') -Raw
    if ($probeExit -eq 0 -or $probeLog -notmatch 'AddressSanitizer: heap-buffer-overflow') {
        throw 'ASan detection probe failed: expected a nonzero exit and heap-buffer-overflow report.'
    }
    Write-Host 'ASan detection probe passed (the intentional memory error was detected).'

    $xmlPath = Join-Path $runRoot 'CoreTest.xml'
    $testExit = Invoke-TestProcess (Join-Path $bin 'CoreTest.exe') @("--gtest_output=xml:$xmlPath") 'CoreTest' 900
    if ($testExit -ne 0) { throw "ASan CoreTest failed (exit $testExit); see CoreTest logs." }
    $testLogs = (Get-Content (Join-Path $runRoot 'CoreTest.stdout.log') -Raw) + (Get-Content (Join-Path $runRoot 'CoreTest.stderr.log') -Raw)
    if ($testLogs -match 'ERROR: AddressSanitizer|SUMMARY: AddressSanitizer') { throw 'ASan reported an error despite a zero exit code.' }
    [xml]$report = Get-Content -LiteralPath $xmlPath -Raw
    $cases = @($report.SelectNodes('/testsuites/testsuite/testcase'))
    if ($cases.Count -eq 0 -or $report.SelectNodes('//failure | //error | //skipped').Count -gt 0 -or
        @($cases | Where-Object { $_.status -ne 'run' -or $_.result -ne 'completed' }).Count -gt 0) {
        throw 'CoreTest XML contains no tests, skipped tests or failures.'
    }
    $passedTests = $cases.Count
} catch {
    $failure = $_.Exception.Message
} finally {
    $env:PATH = $originalPath
    $env:ASAN_OPTIONS = $originalOptions
    $summary = @('## AddressSanitizer', '', "- Passing CoreTest cases: $passedTests", "- Results: $runRoot")
    if ($failure) { $summary += "- Failed: $failure" } else { $summary += '- Detection probe and CoreTest passed.' }
    $summary | Set-Content -LiteralPath (Join-Path $runRoot 'summary.md')
    if ($env:GITHUB_STEP_SUMMARY) { $summary | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY }
    $summary | ForEach-Object { Write-Host $_ }
}
if ($failure) { throw $failure }
