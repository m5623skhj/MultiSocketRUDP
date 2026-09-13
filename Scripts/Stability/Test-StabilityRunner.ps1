#requires -Version 7.0
# Exercise CI failure handling without running sockets or relying on flaky native tests.
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("stability-runner-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$source = Join-Path $testRoot 'FakeGTest.cs'
$executable = Join-Path $testRoot 'FakeGTest.exe'
@'
using System;
using System.IO;
using System.Threading;
using System.Xml.Linq;
class FakeGTest {
    static int Main(string[] args) {
        if (args.Length == 1 && args[0] == "--child") { Thread.Sleep(30000); return 0; }
        string mode = Environment.GetEnvironmentVariable("STABILITY_TEST_MODE");
        string filter = Array.Find(args, a => a.StartsWith("--gtest_filter=")).Substring(15);
        string path = Array.Find(args, a => a.StartsWith("--gtest_output=xml:")).Substring(19);
        Console.WriteLine("fixture mode=" + mode);
        Console.Error.WriteLine("fixture stderr");
        if (mode == "timeout") {
            var info = new System.Diagnostics.ProcessStartInfo(System.Reflection.Assembly.GetExecutingAssembly().Location, "--child");
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            using (var child = System.Diagnostics.Process.Start(info)) {
                File.WriteAllText(Environment.GetEnvironmentVariable("STABILITY_TEST_CHILD_PID"), child.Id.ToString());
                Thread.Sleep(30000);
            }
        }
        if (mode == "exit") { return 7; }
        if (mode == "missing") { return 0; }
        if (mode == "malformed") { File.WriteAllText(path, "<broken"); return 0; }
        var test = new XElement("testcase", new XAttribute("classname", "IntegrationFixture"),
            new XAttribute("name", mode == "wrong-case" ? "WrongCase" : filter.Split('.')[1]),
            new XAttribute("status", "run"), new XAttribute("result", mode == "skip" ? "skipped" : "completed"));
        if (mode == "skip") { test.Add(new XElement("skipped")); }
        if (mode == "assertion") { test.Add(new XElement("failure", "assertion failed")); }
        var suite = new XElement("testsuite");
        if (mode != "empty") { suite.Add(test); }
        if (mode == "duplicate") { suite.Add(new XElement(test)); }
        new XDocument(new XElement("testsuites", suite)).Save(path);
        return 0;
    }
}
'@ | Set-Content -LiteralPath $source
& "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe" /nologo /r:System.Xml.Linq.dll "/out:$executable" $source
if ($LASTEXITCODE -ne 0) { throw 'Fake test executable compilation failed.' }
$runner = Join-Path $PSScriptRoot 'Invoke-StabilityTests.ps1'
$originalMode = $env:STABILITY_TEST_MODE
$originalChildPid = $env:STABILITY_TEST_CHILD_PID
$originalSummary = $env:GITHUB_STEP_SUMMARY
$env:STABILITY_TEST_CHILD_PID = Join-Path $testRoot 'child.pid'
$env:GITHUB_STEP_SUMMARY = Join-Path $testRoot 'fixture-summary.md'
try {
    foreach ($mode in @('pass', 'exit', 'missing', 'malformed', 'skip', 'empty', 'wrong-case', 'duplicate', 'assertion', 'timeout')) {
        $env:STABILITY_TEST_MODE = $mode
        $output = Join-Path $testRoot $mode
        $timeoutSeconds = if ($mode -eq 'timeout') { 2 } else { 15 }
        & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File $runner -Executable $executable `
            -Iterations 1 -CaseTimeoutSeconds $timeoutSeconds -OutputDirectory $output *> (Join-Path $testRoot "$mode.log")
        $exitCode = $LASTEXITCODE
        $resultFile = @(Get-ChildItem -LiteralPath $output -Recurse -Filter results.json)
        if ($resultFile.Count -ne 1) { throw "${mode}: missing result file" }
        $results = @(Get-Content -LiteralPath $resultFile[0].FullName -Raw | ConvertFrom-Json)
        if ($mode -eq 'pass') {
            if ($exitCode -ne 0 -or $results.Count -ne 6 -or @($results | Where-Object status -ne 'passed').Count) {
                throw "${mode}: successful cases were not all recorded: $(Get-Content (Join-Path $testRoot "$mode.log") -Raw)"
            }
        } else {
            if ($exitCode -eq 0 -or $results.Count -ne 1 -or $results[0].status -eq 'passed') {
                throw "${mode}: false pass or first failure was not preserved"
            }
            if ($mode -eq 'timeout' -and $results[0].status -ne 'timeout') { throw 'Timeout was not classified.' }
            if ($mode -eq 'timeout') {
                $childId = [int](Get-Content -LiteralPath $env:STABILITY_TEST_CHILD_PID)
                if (Get-Process -Id $childId -ErrorAction SilentlyContinue) { throw 'Timed-out child process survived cleanup.' }
            }
            if (-not (Get-Content -LiteralPath $results[0].stdout -Raw).Contains("fixture mode=$mode")) {
                throw "${mode}: stdout was not preserved"
            }
        }
        Write-Host "PASS: $mode"
    }
} finally {
    $env:STABILITY_TEST_MODE = $originalMode
    $env:STABILITY_TEST_CHILD_PID = $originalChildPid
    $env:GITHUB_STEP_SUMMARY = $originalSummary
    Write-Host "Runner validation artifacts: $testRoot"
}
# The last child process intentionally failed; do not propagate its exit code to CI.
exit 0
