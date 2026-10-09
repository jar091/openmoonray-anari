# Bootstrap the Windows development environment for openmoonray-anari.
#
# Milestone 0 scope: verify the required toolchain is present and report
# actionable errors. Later milestones extend this script with pinned
# dependency checkout/build (see cmake/Dependencies.cmake for the pins).
#
# Usage:  .\scripts\bootstrap-windows.ps1

$ErrorActionPreference = "Stop"
$failures = @()

function Test-Tool {
    param([string]$Name, [string]$Command, [string]$Hint)
    try {
        $null = Get-Command $Command -ErrorAction Stop
        Write-Host "[ok]   $Name found: $((Get-Command $Command).Source)"
        return $true
    } catch {
        Write-Host "[FAIL] $Name not found. $Hint"
        return $false
    }
}

Write-Host "openmoonray-anari Windows bootstrap"
Write-Host "==================================="

if (-not (Test-Tool "CMake" "cmake" "Install CMake >= 3.25 and add it to PATH.")) { $failures += "cmake" }
if (-not (Test-Tool "Git" "git" "Install Git for Windows.")) { $failures += "git" }
if (-not (Test-Tool "Python" "python" "Install Python 3.10+ (needed for PyNARI tests in later milestones).")) { $failures += "python" }

# Visual Studio 2022 with C++ workload
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) {
        Write-Host "[ok]   Visual Studio C++ toolset: $vs"
    } else {
        Write-Host "[FAIL] Visual Studio found but the 'Desktop development with C++' workload is missing."
        $failures += "msvc"
    }
} else {
    Write-Host "[FAIL] Visual Studio 2022 not detected (vswhere.exe missing)."
    $failures += "visualstudio"
}

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "Bootstrap FAILED, missing: $($failures -join ', ')"
    exit 1
}

# Fetch pinned upstream dependencies (openmoonray, ANARI-SDK, pynari).
& (Join-Path $PSScriptRoot "fetch-dependencies.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Bootstrap OK. Next steps:"
Write-Host "  cmake --preset windows-msvc-debug"
Write-Host "  cmake --build --preset windows-msvc-debug"
Write-Host "  ctest --preset windows-msvc-debug --output-on-failure"
exit 0
