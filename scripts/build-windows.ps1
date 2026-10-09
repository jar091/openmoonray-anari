# Configure and build openmoonray-anari on Windows.
#
# The presets use Ninja (single-config), the same generator the scene_rdl2 and
# MoonRay builds use: the ANARI device links MoonRay, and a multi-config build
# tree cannot express "Release only", which is what a Release-only MoonRay
# install forces. Ninja needs cl/link on PATH, so this script enters the VS2022
# x64 developer environment when it is not already active.
#
# Usage:  .\scripts\build-windows.ps1 [-Config Debug|Release]

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$preset = "windows-msvc-$($Config.ToLower())"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

# --- VS2022 x64 developer environment (cl/link/ninja on PATH) ---------------
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Visual Studio 2022 not detected (vswhere.exe missing)." }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw "Visual Studio 2022 with C++ tools not found." }
    # Enter-VsDevShell calls `vswhere` by bare name internally, so without the
    # installer directory on PATH it prints "'vswhere.exe' is not recognized"
    # before carrying on with the path given here. Noise, but it buries real
    # output.
    if (-not (Get-Command vswhere -ErrorAction SilentlyContinue)) {
        $env:PATH = "$(Split-Path -Parent $vswhere);$env:PATH"
    }
    Import-Module (Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64"
    Set-Location $repoRoot
}

if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    # vcpkg keeps a downloaded ninja under downloads/tools; use the newest.
    $candidate = Get-ChildItem (Join-Path $repoRoot "external\vcpkg\downloads\tools") `
        -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $candidate) { throw "ninja.exe not found (VS component or vcpkg download)." }
    $env:PATH = "$($candidate.DirectoryName);$env:PATH"
}

cmake --preset $preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build --preset $preset
exit $LASTEXITCODE
