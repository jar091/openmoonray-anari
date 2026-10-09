# Stage a self-contained Windows package of the ANARI MoonRay device
# (Milestone 14) and, optionally, zip it.
#
# What "self-contained" has to mean here is set by how the pieces load:
#
#   * the ANARI loader finds the device by *filename* on PATH, so
#     anari_library_moonray.dll and anari.dll go in bin/;
#   * MoonRay has no RPATH on Windows and its libraries are found on PATH, so
#     its bin/ goes in too, along with the vcpkg runtime it was built against;
#   * scene_rdl2 loads shader DSOs from RDL2_DSO_PATH at run time, so rdl2dso/
#     and coredata/ are part of the payload, not build artefacts;
#   * OIDN's own directory is deliberately *not* copied: it ships a tbb12.dll
#     that shadows the oneTBB MoonRay was built against, and the renderer then
#     dies with STATUS_ENTRYPOINT_NOT_FOUND. The denoiser DLL itself comes from
#     the MoonRay install.
#
# Usage:
#   .\scripts\package-windows.ps1 [-BuildDir build\windows-msvc-release]
#                                 [-OutputDir dist] [-Zip]
#
# The package is verified after staging: the device is loaded from it with the
# project's own install test, with PATH pointing *only* at the package.

param(
    [string]$BuildDir = "build\windows-msvc-release",
    [string]$OutputDir = "dist",
    [switch]$Zip
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDir
if (-not (Test-Path $buildPath)) {
    throw "build directory not found: $buildPath (configure and build first)"
}

$version = (Select-String -Path (Join-Path $repoRoot "CMakeLists.txt") `
    -Pattern '^\s*VERSION\s+([0-9.]+)').Matches[0].Groups[1].Value
$name = "openmoonray-anari-$version-windows-x64"
$stage = Join-Path (Join-Path $repoRoot $OutputDir) $name

Write-Host "== staging $name =="
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# 1. The project's own install tree (device + headers + diagnostics tool).
& cmake --install $buildPath --prefix $stage | Out-Null
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }

$binDir = Join-Path $stage "bin"
New-Item -ItemType Directory -Force -Path $binDir | Out-Null

# 2. The ANARI loader.
$anariBin = Join-Path $repoRoot "external\install\anari-sdk\bin"
Copy-Item (Join-Path $anariBin "anari.dll") $binDir -Force

# 3. MoonRay and its third-party runtime.
$moonray = Join-Path $repoRoot "external\installs\openmoonray"
Copy-Item (Join-Path $moonray "bin\*.dll") $binDir -Force
$vcpkgBin = Join-Path $repoRoot "external\vcpkg-installed\x64-windows\bin"
Copy-Item (Join-Path $vcpkgBin "*.dll") $binDir -Force

# 4. The shader DSOs and their metadata, which scene_rdl2 loads at run time.
Copy-Item (Join-Path $moonray "rdl2dso") $stage -Recurse -Force
Copy-Item (Join-Path $moonray "coredata") $stage -Recurse -Force

# 4b. The OptiX PTX, when the MoonRay build has one: XPU mode loads it from
#     $REZ_MOONRAY_ROOT/shaders at run time.
$shaders = Join-Path $moonray "shaders"
if (Test-Path $shaders) {
    Copy-Item $shaders $stage -Recurse -Force
}

# 5. What the package is and how to use it.
$readme = @"
openmoonray-anari $version (Windows x64)

The ANARI device backed by OpenMoonRay, with everything it loads at run time.

    set PATH=%CD%\bin;%PATH%
    set RDL2_DSO_PATH=%CD%\rdl2dso
    anariInfo -l moonray          (from the ANARI SDK)

An application loads it with anariLoadLibrary("moonray"); the loader derives
the filename from that name, so bin\ has to be on PATH.

Contents:
  bin\            the device, the ANARI loader, MoonRay and its dependencies
  rdl2dso\        MoonRay shader DSOs (RDL2_DSO_PATH)
  coredata\       the shader metadata that goes with them
  shaders\        the OptiX PTX, when this build has the GPU path
                  (REZ_MOONRAY_ROOT points at the package root)
  include\        ANARI headers are the SDK's; nothing here is device-specific
  lib\            import libraries

Licensing: this package contains third-party software - see THIRD_PARTY.md in
the source repository.
"@
Set-Content -Path (Join-Path $stage "README.txt") -Value $readme -Encoding utf8

$dllCount = (Get-ChildItem $binDir -Filter *.dll).Count
$dsoCount = (Get-ChildItem (Join-Path $stage "rdl2dso") -Filter *.dll).Count
$size = [math]::Round(((Get-ChildItem $stage -Recurse | Measure-Object Length -Sum).Sum / 1MB), 1)
Write-Host "   $dllCount DLLs in bin, $dsoCount shader DSOs, $size MB total"

# 6. Verify the package by loading the device from it, with PATH pointing at
#    the package and nowhere else that could supply a copy.
$test = Join-Path $buildPath "tests\integration\integration_load_library.exe"
if (Test-Path $test) {
    Write-Host "== verifying =="
    $previousPath = $env:PATH
    $previousDso = $env:RDL2_DSO_PATH
    try {
        $env:PATH = "$binDir;$env:SystemRoot\System32;$env:SystemRoot"
        $env:RDL2_DSO_PATH = Join-Path $stage "rdl2dso"
        & $test
        if ($LASTEXITCODE -ne 0) {
            throw "the packaged device did not load (exit $LASTEXITCODE)"
        }
        Write-Host "   the device loads from the package"
    } finally {
        $env:PATH = $previousPath
        if ($null -ne $previousDso) { $env:RDL2_DSO_PATH = $previousDso }
    }
} else {
    Write-Host "   (integration_load_library not built - package not verified)"
}

if ($Zip) {
    $zipPath = Join-Path (Join-Path $repoRoot $OutputDir) "$name.zip"
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Write-Host "== compressing =="
    Compress-Archive -Path $stage -DestinationPath $zipPath
    $zipSize = [math]::Round(((Get-Item $zipPath).Length / 1MB), 1)
    Write-Host "   $zipPath ($zipSize MB)"
}

Write-Host "Package: $stage"
