# Check out the pinned upstream sources (Windows).
#
# The pins are git submodules (see .gitmodules / DEPENDENCIES.md), so this is
# a thin wrapper around `git submodule update` plus a verification pass. It
# replaces the old hand-maintained clone list, which duplicated the pins in
# this script, in fetch-dependencies.sh and in cmake/Dependencies.cmake.
#
# Usage:  .\scripts\fetch-dependencies.ps1

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

Write-Host "[run]  git submodule update --init (top level)"
git submodule update --init `
    external/openmoonray external/anari-sdk external/pynari external/vcpkg
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Submodule checkout failed. If this is the first run after the"
    Write-Host "MoonRay repositories moved into our namespace, make sure they"
    Write-Host "have been published: .\scripts\publish-windows-port.ps1"
    exit $LASTEXITCODE
}

# Deliberately NOT --recursive: openmoonray carries ~20 submodules (arras,
# hydra, moonshine_usd, dcc plugins, ...) that the minimal build does not use
# (docs/MINIMAL_MOONRAY_DEPENDENCIES.md). Two of them also cannot be checked
# out on Windows at all - moonray_dcc_plugins contains paths like
# `Vop::DW_MOONRAY::AttributeMap::1.hda`, and ':' is not a legal filename
# character here. Init only what we build.
Write-Host "[run]  git submodule update --init (openmoonray: minimal set)"
git -C external/openmoonray submodule update --init `
    moonray/scene_rdl2 moonray/moonray moonray/mcrt_denoise moonray/moonshine cmake_modules
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# --- verification -----------------------------------------------------------
# A submodule left at the wrong commit (e.g. a stale `git pull` in a checkout)
# silently changes what gets built, so report the state rather than assume it.
$expected = @{
    "external/openmoonray"                          = "openmoonray (windows-port)"
    "external/openmoonray/moonray/scene_rdl2"       = "scene_rdl2"
    "external/openmoonray/moonray/moonray"          = "moonray"
    "external/openmoonray/moonray/mcrt_denoise"     = "mcrt_denoise"
    "external/openmoonray/cmake_modules"            = "cmake_modules"
    "external/anari-sdk"                            = "ANARI-SDK"
    "external/pynari"                               = "PyNARI"
    "external/vcpkg"                                = "vcpkg"
}

Write-Host ""
Write-Host "Checked-out pins:"
$problems = @()
foreach ($path in $expected.Keys | Sort-Object) {
    if (-not (Test-Path (Join-Path $path ".git"))) {
        $problems += "$path is missing"
        Write-Host ("  {0,-46} MISSING" -f $expected[$path])
        continue
    }
    $sha = (git -C $path rev-parse --short HEAD).Trim()
    $desc = (git -C $path describe --tags --always 2>$null)
    $dirty = if (git -C $path status --porcelain) { " [locally modified]" } else { "" }
    Write-Host ("  {0,-46} {1} {2}{3}" -f $expected[$path], $sha, $desc, $dirty)
}

# The vcpkg checkout and the manifest baseline must agree, or the dependency
# versions silently differ from the ones recorded in the manifest.
$manifest = Get-Content (Join-Path $repoRoot "scripts\vcpkg\vcpkg.json") -Raw | ConvertFrom-Json
$vcpkgSha = (git -C external/vcpkg rev-parse HEAD).Trim()
if ($manifest.'builtin-baseline' -ne $vcpkgSha) {
    $problems += "vcpkg submodule ($vcpkgSha) != manifest builtin-baseline ($($manifest.'builtin-baseline'))"
}

if ($problems.Count -gt 0) {
    Write-Host ""
    Write-Host "Problems:"
    $problems | ForEach-Object { Write-Host "  - $_" }
    exit 1
}

Write-Host ""
Write-Host "All pinned sources present."
