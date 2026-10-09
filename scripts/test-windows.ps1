# Run the openmoonray-anari test suite on Windows.
# Usage:  .\scripts\test-windows.ps1 [-Config Debug|Release] [-Label <ctest label>]

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [string]$Label = ""
)

$ErrorActionPreference = "Stop"
$preset = "windows-msvc-$($Config.ToLower())"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$ctestArgs = @("--preset", $preset, "--output-on-failure")
if ($Label -ne "") { $ctestArgs += @("-L", $Label) }

ctest @ctestArgs
exit $LASTEXITCODE
