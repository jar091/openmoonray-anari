# Build PyNARI (pinned submodule external/pynari) against the project's
# ANARI-SDK install, for the pynari.* CTest layer (Milestone 7).
#
# PyNARI is consumed as-is from the pinned fork (external\pynari,
# https://github.com/jar091/pynari, branch mjar/devel); this script never patches
# the checkout. The setParameter overload ordering the project depends on is a
# commit in that fork, not a patch step (PYNARI.md). The only project-side
# choices are which Python and which pybind11 it builds against:
#
#   * Python comes from the project-local .venv, never a system interpreter.
#   * pybind11 comes from that venv (PYNARI_USE_INSTALLED_PYBIND, an option
#     PyNARI provides): its vendored copy is 2.12, which predates Python 3.13
#     support.
#
# The module lands in build/pynari; tests/pynari points PYTHONPATH at it.
#
# Usage:  .\scripts\build-pynari-windows.ps1

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$srcDir   = Join-Path $repoRoot "external\pynari"
$buildDir = Join-Path $repoRoot "build\pynari"
$anariDir = Join-Path $repoRoot "external\install\anari-sdk"
$venv     = Join-Path $repoRoot ".venv"
$python   = Join-Path $venv "Scripts\python.exe"

if (-not (Test-Path (Join-Path $srcDir "CMakeLists.txt"))) {
    throw "external\pynari is not checked out - run .\scripts\fetch-dependencies.ps1 first."
}

if (-not (Test-Path (Join-Path $anariDir "lib\cmake"))) {
    throw "ANARI-SDK not installed - run .\scripts\build-anari-sdk-windows.ps1 first."
}

if (-not (Test-Path $python)) {
    Write-Host "[run]  python -m venv .venv"
    python -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw "failed to create $venv" }
}

Write-Host "[run]  pip install numpy pybind11"
& $python -m pip install --quiet --upgrade pip
& $python -m pip install --quiet numpy pybind11
if ($LASTEXITCODE -ne 0) { throw "pip install failed" }

$pybind11Dir = & $python -c "import pybind11; print(pybind11.get_cmake_dir())"
if ($LASTEXITCODE -ne 0) { throw "could not locate pybind11's cmake dir" }

# --- VS2022 x64 developer environment ---------------------------------------
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
        -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
    Set-Location $repoRoot
}

cmake -S $srcDir -B $buildDir -A x64 `
    "-DCMAKE_PREFIX_PATH=$anariDir" `
    "-DPYNARI_USE_INSTALLED_PYBIND=ON" `
    "-Dpybind11_DIR=$pybind11Dir" `
    "-DPython_EXECUTABLE=$python" `
    "-DPython3_EXECUTABLE=$python"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$module = Get-ChildItem $buildDir -Recurse -Filter "pynari*.pyd" -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $module) { throw "the build produced no pynari*.pyd" }

Write-Host ""
Write-Host "PyNARI built: $($module.FullName)"
Write-Host "Reconfigure the project with -DOPENMOONRAY_ANARI_BUILD_PYNARI_TESTS=ON"
Write-Host "to pick up the pynari.* tests."
