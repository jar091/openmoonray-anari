# Build and install the minimal MoonRay stack on Windows (Milestone 3), after
# scripts/build-moonray-deps-windows.ps1 and
# scripts/build-scene-rdl2-windows.ps1 have populated external/.
#
# Mirrors scripts/build-moonray-macos.sh: standalone projects rather than the
# openmoonray superproject (which descends into arras/hydra/moonshine_usd
# submodules that are not part of the pinned minimal checkout), Ninja for
# first-class ISPC, CPU-only (no OptiX/Metal/XPU).
#
# Order: scene_rdl2 (already installed by its own script) -> mcrt_denoise ->
# moonray -> moonshine. Installs into external/installs/openmoonray.
#
# Usage:  .\scripts\build-moonray-windows.ps1 [-Config Release|Debug]
#             [-Target mcrt_denoise|moonray|moonshine|anari_dso|all]
#             [-WithDebugInfo] [-Xpu]
#
# anari_dso is not part of MoonRay and not part of `all`: it builds the RDL2
# DSOs of this repository (dso/: AnariBumpNormalMap) the way moonshine is
# built - out of tree against the *installed* MoonRay - and installs them into
# the same prefix. It never rebuilds MoonRay; pass the same -Xpu, -OpenVdbRoot
# and -BuildRoot the installed MoonRay was built with. The build tree is
# <BuildRoot>\anari-dso; once it is configured, `cmake --build` and
# `cmake --install` on it are all an incremental rebuild takes.
#
# moonshine is the shader library (the Dwa* materials and the pattern maps): a
# set of RDL2 DSOs plus a few support DLLs. It is built against the *installed*
# MoonRay (find_package(Moonray)), so `-Target moonshine` needs moonray to have
# been installed first and never rebuilds it.
#
# -WithDebugInfo keeps the Release optimization settings but adds /Zi and links
# with /DEBUG, so the installed DLLs get PDBs. That is what makes a crash
# inside MoonRay diagnosable at all on Windows: without symbols a stack trace
# is a list of module offsets (tests/common/CrashTrace.h prints the stack). It
# changes no code, only what the compiler records, so the binaries stay
# ABI-compatible with a plain Release build.
#
# -Xpu builds MoonRay's GPU path (Milestone 15): CUDA + OptiX, which MoonRay
# uses for its XPU execution mode. It needs a CUDA toolkit and the OptiX SDK
# headers - OPTIX_ROOT is honored, and the default NVIDIA installer location is
# used when it is not set. Without -Xpu the build is CPU-only, which stays the
# portability baseline (XPU is optional and comes after the CPU backend).

param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [ValidateSet("all", "mcrt_denoise", "moonray", "moonshine", "anari_dso")]
    [string]$Target = "all",
    [switch]$WithDebugInfo,
    [switch]$Xpu,
    # OpenVDB to build against instead of the vcpkg one, e.g. the libraries of
    # the application the device is loaded into (only one OpenVDB version can
    # be loaded in a process on Windows).
    [string]$OpenVdbRoot = "",
    # Directory holding the per-project build trees.
    [string]$BuildRoot = ""
)

$ErrorActionPreference = "Stop"
$repoRoot  = Split-Path -Parent $PSScriptRoot
$external  = Join-Path $repoRoot "external"
$vcpkgInst = Join-Path $external "vcpkg-installed\x64-windows"
$prefix    = Join-Path $external "installs\openmoonray"
$srcRoot   = Join-Path $external "openmoonray\moonray"
$modules   = Join-Path $external "openmoonray\cmake_modules\cmake"
$ispcExe   = Join-Path $external "tools\ispc-v1.20.0-windows\bin\ispc.exe"
$oidnRoot  = Join-Path $external "tools\oidn-2.2.2.x64.windows"
$wfbDir    = Join-Path $external "tools\win_flex_bison-2.5.25"

foreach ($p in @($vcpkgInst, $ispcExe, (Join-Path $prefix "lib\cmake"))) {
    if (-not (Test-Path $p)) {
        throw "Missing prerequisite '$p' - run build-moonray-deps-windows.ps1 and build-scene-rdl2-windows.ps1 first."
    }
}

# --- Source-tree self-alias junctions ---------------------------------------
# The checkouts carry git symlinks that let their own sources include
# <moonray/...> / <mcrt_denoise/...> / <moonshine/...>:
#     moonray/moonray            -> lib
#     moonray/include/moonray     -> ../lib/
#     mcrt_denoise/mcrt_denoise   -> lib
#     moonshine/include/moonshine -> ../lib
# Without symlink support git materializes them as small text files, so those
# includes fail. Replace each with an NTFS junction (unprivileged).
function Repair-SelfAlias {
    param([string]$LinkPath, [string]$TargetPath)

    $item = Get-Item $LinkPath -Force -ErrorAction SilentlyContinue
    if ($item -and $item.PSIsContainer) { return }  # already a directory/junction
    if (-not $item) { return }                      # no such alias in this checkout
    if (-not (Test-Path $TargetPath)) { return }

    Remove-Item $LinkPath -Force
    cmd /c mklink /J "$LinkPath" "$TargetPath" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Failed to create junction $LinkPath -> $TargetPath" }

    # Hide the swap from git where the path IS tracked (as a symlink that
    # Linux/macOS need): otherwise a `git add -A` would delete the symlink
    # and walk the junction, duplicating the whole lib/ tree into the commit.
    # Best-effort only - an untracked alias needs no protection, and failing
    # here must never abort a build.
    $note = ""
    $repo = Split-Path $LinkPath -Parent
    while ($repo -and -not (Test-Path (Join-Path $repo ".git"))) { $repo = Split-Path $repo -Parent }
    if ($repo) {
        $rel = $LinkPath.Substring($repo.Length).TrimStart('\','/') -replace '\\','/'
        $tracked = (git -C $repo ls-files -s -- $rel 2>$null | Out-String).Trim()
        if ($tracked) {
            git -C $repo update-index --skip-worktree -- $rel 2>&1 | Out-Null
            if ($LASTEXITCODE -eq 0) { $note = " (git: skip-worktree)" }
        }
    }
    Write-Host "[fix]  $LinkPath replaced with a junction$note"
}

# mcrt_denoise has no tracked self-alias upstream and builds without one, so
# only moonray's two and moonshine's one are repaired here.
Repair-SelfAlias (Join-Path $srcRoot "moonray\moonray") (Join-Path $srcRoot "moonray\lib")
Repair-SelfAlias (Join-Path $srcRoot "moonray\include\moonray") (Join-Path $srcRoot "moonray\lib")
Repair-SelfAlias (Join-Path $srcRoot "moonshine\include\moonshine") (Join-Path $srcRoot "moonshine\lib")

# --- VS2022 x64 developer environment ---------------------------------------
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
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
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $candidate = Get-ChildItem (Join-Path $external "vcpkg\downloads\tools") `
        -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $candidate) { throw "ninja.exe not found." }
    $env:PATH = "$($candidate.DirectoryName);$env:PATH"
}

$env:CMAKE_POLICY_VERSION_MINIMUM = "3.5"
# MoonrayDso.cmake locates the ispc_dso_generate helper through this variable
# in standalone builds.
$env:CMAKE_MODULES_ROOT = Join-Path $external "openmoonray\cmake_modules"

# Build-time code generators (rdl2_json_exporter, DSO tooling) run freshly
# built executables and the installed scene_rdl2 tools; no RPATH on Windows.
$vcpkgBin = if ($Config -eq "Debug") { Join-Path $vcpkgInst "debug\bin" } else { Join-Path $vcpkgInst "bin" }
$env:PATH = @(
    (Join-Path $prefix "bin"),
    $vcpkgBin,
    (Join-Path $vcpkgInst "bin"),
    (Join-Path $oidnRoot "bin"),
    $wfbDir,  # win_bison / win_flex, found by CMake's FindBISON/FindFLEX
    # scene_rdl2 DLLs also live in its build tree until it is installed.
    (Join-Path $repoRoot "build\scene_rdl2\lib\common\grid_util")
) -join ";" | ForEach-Object { "$_;$env:PATH" }

if ($Config -eq "Debug") {
    $prefixPath = "$prefix;$vcpkgInst\debug;$vcpkgInst;$oidnRoot"
} else {
    $prefixPath = "$prefix;$vcpkgInst;$oidnRoot"
}

$commonArgs = @(
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_INSTALL_PREFIX=$prefix",
    "-DCMAKE_PREFIX_PATH=$prefixPath",
    "-DCMAKE_MODULE_PATH=$($modules -replace '\\','/')",
    "-DCMAKE_ISPC_COMPILER=$ispcExe",
    "-DBUILD_TESTING=NO",
    # Interim DLL-export strategy for the Windows port (same as scene_rdl2):
    # without it a DLL exports nothing and no import library is produced.
    "-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON"
)
if ($OpenVdbRoot) {
    $commonArgs += @(
        "-DOpenVDB_INCLUDE_DIR=$((Join-Path $OpenVdbRoot 'include\openvdb') -replace '\\','/')",
        "-DOpenVDB_LIBRARIES=$((Join-Path $OpenVdbRoot 'lib\openvdb.lib') -replace '\\','/')",
        # The vcpkg include directory (with its own openvdb/ headers) is on the
        # include path of most targets; search these headers first.
        "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /I$((Join-Path $OpenVdbRoot 'include') -replace '\\','/')"
    )
    $env:PATH = "$(Join-Path $OpenVdbRoot 'bin');$env:PATH"
}
if (-not $BuildRoot) { $BuildRoot = Join-Path $repoRoot "build" }

if ($WithDebugInfo) {
    $commonArgs += @(
        "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi /FS",
        "-DCMAKE_C_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi /FS",
        "-DCMAKE_SHARED_LINKER_FLAGS=/DEBUG",
        "-DCMAKE_EXE_LINKER_FLAGS=/DEBUG"
    )
}

# Run a native executable and fail on its EXIT CODE, not on its stderr.
#
# This has to exist because of a PowerShell 5.1 trap: with
# $ErrorActionPreference = 'Stop' (set at the top of this script, and wanted
# for the cmdlets), any line a native program writes to stderr is wrapped in a
# NativeCommandError ErrorRecord that TERMINATES the script. CMake writes its
# warnings to stderr, so a single "CMake Warning (dev)" aborted the build
# before the $LASTEXITCODE check below was ever reached - while cmake itself
# had exited 0. Lowering the preference for the duration of the call restores
# the intended behaviour: warnings are printed, only a nonzero exit is fatal.
function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments)

    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Build-Project {
    # $Source: the source directory, for a project that is not one of
    # MoonRay's own (those live in $srcRoot\$Name).
    param([string]$Name, [string[]]$ExtraArgs, [string]$Source = "")

    $src = if ($Source) { $Source } else { Join-Path $srcRoot $Name }
    $build = Join-Path $BuildRoot "$Name$(if ($Config -eq 'Debug') { '-debug' } else { '' })"

    Write-Host ""
    Write-Host "== $Name ($Config) =="
    Invoke-Native cmake (@("-S", $src, "-B", $build) + $commonArgs + $ExtraArgs)
    Invoke-Native cmake @("--build", $build)
    Invoke-Native cmake @("--install", $build)
}

if ($Target -in @("all", "mcrt_denoise")) {
    # CPU-only denoiser: no CUDA/OptiX, no Metal.
    Build-Project "mcrt_denoise" @(
        "-DMCRTDENOISE_BUILD_TESTING=NO",
        "-DMOONRAY_USE_OPTIX=NO",
        "-DMOONRAY_USE_METAL=NO"
    )
}

$moonrayGpuArgs = @("-DMOONRAY_USE_OPTIX=NO")
if ($Xpu) {
    # OptiX is header-only; upstream's FindOptiX looks under $env:OPTIX_ROOT.
    if (-not $env:OPTIX_ROOT) {
        $optixRoot = Join-Path $env:ProgramData "NVIDIA Corporation"
        $optixDirs = Get-ChildItem $optixRoot -Filter "OptiX SDK*" -Directory `
            -ErrorAction SilentlyContinue | Sort-Object Name -Descending
        if (-not $optixDirs) {
            throw "-Xpu needs the OptiX SDK: set OPTIX_ROOT, or install it (the NVIDIA installer puts it under $optixRoot)"
        }
        $env:OPTIX_ROOT = $optixDirs[0].FullName
        Write-Host "[info] OPTIX_ROOT = $($env:OPTIX_ROOT)"
    }
    if (-not (Test-Path (Join-Path $env:OPTIX_ROOT "include/optix.h"))) {
        throw "OPTIX_ROOT does not contain include/optix.h: $($env:OPTIX_ROOT)"
    }
    # nvcc has to be findable for CMake's check_language(CUDA).
    if (-not (Get-Command nvcc -ErrorAction SilentlyContinue)) {
        $cudaRoot = Join-Path ${env:ProgramFiles} "NVIDIA GPU Computing Toolkit/CUDA"
        $cudaDirs = Get-ChildItem $cudaRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
        if (-not $cudaDirs) {
            throw "-Xpu needs the CUDA toolkit: install it or put nvcc on PATH"
        }
        $env:PATH = "$($cudaDirs[0].FullName)/bin;$($env:PATH)"
        Write-Host "[info] CUDA = $($cudaDirs[0].FullName)"
    }
    $moonrayGpuArgs = @("-DMOONRAY_USE_OPTIX=YES")
}

if ($Target -in @("all", "moonray")) {
    Build-Project "moonray" (@(
        "-DMOONRAY_BUILD_TESTING=NO",
        "-DMOONRAY_USE_METAL=NO"
    ) + $moonrayGpuArgs)
}

if ($Target -in @("all", "moonshine")) {
    # Standalone against the installed MoonRay. The installed MoonrayTargets
    # reference CUDA::cudart / OptiX::OptiX when MoonRay was built with -Xpu,
    # and MoonrayConfig only looks for those packages when MOONRAY_USE_OPTIX
    # is set, so the same GPU arguments are passed here.
    Build-Project "moonshine" (@(
        "-DMOONSHINE_BUILD_TESTING=NO"
    ) + $moonrayGpuArgs)
}

if ($Target -eq "anari_dso") {
    # This repository's own RDL2 DSOs (dso/): standalone against the installed
    # MoonRay like moonshine, hence the same GPU arguments. Deliberately not
    # part of "all", which is MoonRay itself.
    Build-Project "anari-dso" $moonrayGpuArgs (Join-Path $repoRoot "dso")
}

Write-Host ""
Write-Host "MoonRay ($Config) installed to $prefix"
