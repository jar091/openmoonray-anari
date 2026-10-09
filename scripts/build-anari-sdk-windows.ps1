# Build and install the pinned ANARI-SDK on Windows (Milestone 5+).
#
# The SDK is a git submodule (external/anari-sdk, pinned to v0.16.0 - see
# DEPENDENCIES.md). It installs into external/install/anari-sdk, which the
# project's top-level CMakeLists.txt puts on CMAKE_PREFIX_PATH automatically;
# nothing touches system paths.
#
# BUILD_EXAMPLES=ON is required: it builds anariInfo, which the project's
# `sdk.anari_info` test runs against the moonray device. The viewer is off
# (GUI dependencies).
#
# -Cts additionally builds `anariCts`, the conformance tool Milestone 13 uses
# to ask the device what it implements and which tests it can run. It is off by
# default because it pulls in the SDK's test-scene and glTF machinery, which
# the rest of the project does not need.
#
# Usage:  .\scripts\build-anari-sdk-windows.ps1 [-Config Release|Debug] [-Cts]

param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$Cts
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$srcDir   = Join-Path $repoRoot "external\anari-sdk"
$buildDir = Join-Path $repoRoot "build\anari-sdk-$($Config.ToLower())"
$prefix   = Join-Path $repoRoot "external\install\anari-sdk"

if (-not (Test-Path (Join-Path $srcDir "CMakeLists.txt"))) {
    throw "external\anari-sdk is not checked out - run .\scripts\fetch-dependencies.ps1 first."
}

# The SDK generates its query/introspection code with a Python script at build
# time, so python must be findable.
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "python not found on PATH - the ANARI-SDK code generator needs it."
}

cmake -S $srcDir -B $buildDir -A x64 `
    "-DCMAKE_INSTALL_PREFIX=$prefix" `
    "-DBUILD_SHARED_LIBS=ON" `
    "-DBUILD_TESTING=OFF" `
    "-DBUILD_EXAMPLES=ON" `
    "-DBUILD_VIEWER=OFF" `
    "-DBUILD_CTS=$(if ($Cts) { 'ON' } else { 'OFF' })" `
    "-DINSTALL_CTS=$(if ($Cts) { 'ON' } else { 'OFF' })" `
    "-DBUILD_CAT=OFF" `
    "-DINSTALL_VIEWER=OFF"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --install $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "ANARI-SDK ($Config) installed to $prefix"
