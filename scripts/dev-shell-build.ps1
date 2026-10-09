# Internal helper: run a command inside the VS2022 x64 developer environment
# with ninja on PATH. Used by the build/test scripts and handy interactively:
#
#   .\scripts\dev-shell-build.ps1 -Command "ctest --preset windows-msvc-release"

param(
    [Parameter(Mandatory = $true)]
    [string]$Command
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
    $vswhere = Join-Path $vsInstaller "vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Visual Studio 2022 not detected (vswhere.exe missing)." }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw "Visual Studio 2022 with C++ tools not found." }
    # Enter-VsDevShell's own scripts call `vswhere` by bare name, so without
    # the installer directory on PATH every invocation prints "'vswhere.exe'
    # is not recognized as an internal or external command" before carrying on
    # with the path we gave it. Nothing fails - it is pure noise - but it
    # buries real output.
    if (-not (Get-Command vswhere -ErrorAction SilentlyContinue)) {
        $env:PATH = "$vsInstaller;$env:PATH"
    }
    Import-Module (Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
}

if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $candidate = Get-ChildItem (Join-Path $repoRoot "external\vcpkg\downloads\tools") `
        -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime | Select-Object -Last 1
    if ($candidate) { $env:PATH = "$($candidate.DirectoryName);$env:PATH" }
}

Set-Location $repoRoot
# Native tools (cmake, ctest) write progress and warnings to stderr; with
# ErrorActionPreference=Stop PowerShell would turn the first such line into a
# terminating error, so the command itself runs with the default preference
# and is judged by its exit code.
$ErrorActionPreference = "Continue"
Invoke-Expression $Command
exit $LASTEXITCODE
