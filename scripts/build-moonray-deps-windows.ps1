# Build/install the Windows third-party stack for the MoonRay port (Milestone 2+).
#
# Everything is project-local (master prompt: nothing touches system paths):
#   external/vcpkg            - pinned vcpkg checkout (release tag 2026.07.29)
#   external/vcpkg-installed  - installed packages (triplet x64-windows)
#   external/tools/ispc       - pinned ISPC compiler (prebuilt release zip)
#
# The package set mirrors scripts/vcpkg/vcpkg.json (manifest mode with a
# pinned builtin-baseline; Lua is pinned to the upstream superbuild's 5.4
# line and built with the [tools] feature so lua.exe/luac.exe exist for the
# rdlalib code generation).
#
# Usage:  .\scripts\build-moonray-deps-windows.ps1

$ErrorActionPreference = "Stop"
$repoRoot  = Split-Path -Parent $PSScriptRoot
$external  = Join-Path $repoRoot "external"
$vcpkgRoot = Join-Path $external "vcpkg"
$vcpkgTag  = "2026.07.29"           # -> builtin-baseline in scripts/vcpkg/vcpkg.json
$ispcVer   = "v1.20.0"              # matches the macOS baseline (ISPC 1.20)

New-Item -ItemType Directory -Force $external | Out-Null

# --- vcpkg (pinned) ---------------------------------------------------------
if (-not (Test-Path (Join-Path $vcpkgRoot ".git"))) {
    Write-Host "[get]  vcpkg ($vcpkgTag)"
    git clone --quiet https://github.com/microsoft/vcpkg $vcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw "vcpkg clone failed" }
}
git -C $vcpkgRoot checkout --quiet $vcpkgTag
if ($LASTEXITCODE -ne 0) { throw "vcpkg checkout of $vcpkgTag failed" }

$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
if (-not (Test-Path $vcpkgExe)) {
    Write-Host "[run]  bootstrap-vcpkg"
    & (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw "vcpkg bootstrap failed" }
}

# --- MoonRay third-party packages (manifest mode) ---------------------------
Write-Host "[run]  vcpkg install (triplet x64-windows; this builds Boost - long)"
$manifestRoot = Join-Path $repoRoot "scripts\vcpkg"
$installRoot  = Join-Path $external "vcpkg-installed"
& $vcpkgExe install "--x-manifest-root=$manifestRoot" "--x-install-root=$installRoot" --triplet x64-windows
if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed" }

$toolsDir = Join-Path $external "tools"

# --- OIDN (prebuilt, pinned; not in vcpkg) ----------------------------------
$oidnVer = "v2.2.2"
$oidnBase = "oidn-$($oidnVer.TrimStart('v')).x64.windows"
$oidnDir  = Join-Path $toolsDir $oidnBase
if (-not (Test-Path (Join-Path $oidnDir "lib\OpenImageDenoise.lib"))) {
    New-Item -ItemType Directory -Force $toolsDir | Out-Null
    $zip = Join-Path $toolsDir "$oidnBase.zip"
    $url = "https://github.com/RenderKit/oidn/releases/download/$oidnVer/$oidnBase.zip"
    Write-Host "[get]  $url"
    Invoke-WebRequest -Uri $url -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $toolsDir -Force
    Remove-Item $zip
}

# --- bison / flex (winflexbison, prebuilt; not in vcpkg) ---------------------
# moonray's pbr library generates a parser with bison_target()/flex_target().
# CMake's FindBISON/FindFLEX accept the win_bison/win_flex names, so the
# winflexbison distribution works unmodified.
$wfbVer = "2.5.25"
$wfbDir = Join-Path $toolsDir "win_flex_bison-$wfbVer"
if (-not (Test-Path (Join-Path $wfbDir "win_bison.exe"))) {
    New-Item -ItemType Directory -Force $wfbDir | Out-Null
    $zip = Join-Path $toolsDir "win_flex_bison-$wfbVer.zip"
    $url = "https://github.com/lexxmark/winflexbison/releases/download/v$wfbVer/win_flex_bison-$wfbVer.zip"
    Write-Host "[get]  $url"
    Invoke-WebRequest -Uri $url -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $wfbDir -Force
    Remove-Item $zip
    if (-not (Test-Path (Join-Path $wfbDir "win_bison.exe"))) {
        throw "winflexbison extraction did not produce win_bison.exe in $wfbDir"
    }
}

# --- ISPC compiler (prebuilt, pinned) ---------------------------------------
$toolsDir = Join-Path $external "tools"
$ispcDir  = Join-Path $toolsDir "ispc-$ispcVer-windows"
$ispcExe  = Join-Path $ispcDir "bin\ispc.exe"
if (-not (Test-Path $ispcExe)) {
    New-Item -ItemType Directory -Force $toolsDir | Out-Null
    $zip = Join-Path $toolsDir "ispc-$ispcVer-windows.zip"
    $url = "https://github.com/ispc/ispc/releases/download/$ispcVer/ispc-$ispcVer-windows.zip"
    Write-Host "[get]  $url"
    Invoke-WebRequest -Uri $url -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $toolsDir -Force
    Remove-Item $zip
    if (-not (Test-Path $ispcExe)) { throw "ISPC extraction did not produce $ispcExe" }
}
& $ispcExe --version

Write-Host ""
Write-Host "Windows MoonRay dependency stack ready:"
Write-Host "  vcpkg packages: $external\vcpkg-installed\x64-windows"
Write-Host "  ISPC:           $ispcExe"
Write-Host "  OIDN:           $oidnDir"
Write-Host "  bison/flex:     $wfbDir"
