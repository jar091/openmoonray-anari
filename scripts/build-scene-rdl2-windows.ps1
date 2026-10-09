# Build and install scene_rdl2 on Windows (Milestone 2), after
# scripts/build-moonray-deps-windows.ps1 has populated external/vcpkg-installed
# and external/tools/ispc-*.
#
# Deviations from upstream, all deliberate (mirrors build-moonray-macos.sh):
#   - Standalone project (the openmoonray superproject descends into
#     arras/hydra/... submodules that are not part of the minimal checkout).
#   - Ninja generator inside a VS2022 x64 dev environment: CMake's
#     first-class ISPC language is not supported by the Visual Studio
#     generator, and Ninja matches the Makefiles path used on Linux/macOS.
#   - Python bindings off (SCENERDL2_BUILD_PYTHON_BINDINGS=NO): they need
#     Boost.Python which is not part of the Windows dependency set yet.
#
# Installs to external/installs/openmoonray. Nothing touches system paths.
#
# Usage:  .\scripts\build-scene-rdl2-windows.ps1 [-Config Release|Debug]

param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$Tests   # build the upstream CppUnit tests (adds ctest targets)
)

$ErrorActionPreference = "Stop"
$repoRoot  = Split-Path -Parent $PSScriptRoot
$external  = Join-Path $repoRoot "external"
$vcpkgInst = Join-Path $external "vcpkg-installed\x64-windows"
$prefix    = Join-Path $external "installs\openmoonray"
$srcDir    = Join-Path $external "openmoonray\moonray\scene_rdl2"
$modules   = Join-Path $external "openmoonray\cmake_modules\cmake"
$ispcExe   = Join-Path $external "tools\ispc-v1.20.0-windows\bin\ispc.exe"
$luaTools  = Join-Path $vcpkgInst "tools\lua"
# Per-config build dirs (Ninja is single-config); only Release installs into
# the shared prefix that the moonray core (M3) builds against.
if ($Config -eq "Debug") {
    $buildDir = Join-Path $repoRoot "build\scene_rdl2-debug"
} else {
    $buildDir = Join-Path $repoRoot "build\scene_rdl2"
}

foreach ($p in @($vcpkgInst, $ispcExe, (Join-Path $luaTools "luac.exe"))) {
    if (-not (Test-Path $p)) {
        throw "Missing prerequisite '$p' - run scripts\build-moonray-deps-windows.ps1 first."
    }
}

# --- include/scene_rdl2 self-alias ------------------------------------------
# The checkout contains a git symlink include/scene_rdl2 -> ../lib; without
# symlink support git materializes it as a 7-byte text file, which breaks
# every <scene_rdl2/...> include. Replace it with an NTFS junction
# (unprivileged, unlike symlinks).
$includeAlias = Join-Path $srcDir "include\scene_rdl2"
$aliasItem = Get-Item $includeAlias -ErrorAction SilentlyContinue
if ($aliasItem -and -not $aliasItem.PSIsContainer) {
    Remove-Item $includeAlias -Force
    $libDir = Join-Path $srcDir "lib"
    cmd /c mklink /J "$includeAlias" "$libDir" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Failed to create junction $includeAlias -> $libDir" }
    # Hide the swap from git: upstream tracks this path as a symlink and the
    # fork must keep it that way (Linux/macOS need it). Without this a
    # `git add -A` deletes the symlink and walks the junction, duplicating
    # the entire lib/ tree into the commit.
    git -C $srcDir update-index --skip-worktree "include/scene_rdl2" 2>$null
    Write-Host "[fix]  include\scene_rdl2 symlink surrogate replaced with a junction (git: skip-worktree)"
}

# --- VS2022 x64 developer environment (cl/link/ninja on PATH) ---------------
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

$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $ninja) {
    # vcpkg keeps a downloaded ninja under downloads/tools; use the newest.
    $candidate = Get-ChildItem (Join-Path $external "vcpkg\downloads\tools") `
        -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $candidate) { throw "ninja.exe not found (VS component or vcpkg download)." }
    $env:PATH = "$($candidate.DirectoryName);$env:PATH"
}

# CMake 4 refuses <3.5 minimums that appear in some upstream subdirectories.
$env:CMAKE_POLICY_VERSION_MINIMUM = "3.5"

# MoonrayDso.cmake locates the ispc_dso_generate helper via
# superproject-relative paths or this variable (standalone builds; mirrors
# build-moonray-macos.sh).
$env:CMAKE_MODULES_ROOT = Join-Path $external "openmoonray\cmake_modules"

# For Debug, the vcpkg debug tree goes first so find_library picks the
# debug-CRT variants (log4cplusD, cppunitd, tbb12_debug, debug jsoncpp/lua);
# mixing an MDd binary with MD dependency DLLs crashes wherever std types
# cross the DLL boundary.
if ($Config -eq "Debug") {
    $prefixPath = "$vcpkgInst\debug;$vcpkgInst"
} else {
    $prefixPath = "$vcpkgInst"
}

# Every -D argument is passed as a fully quoted string: PowerShell does not
# interpolate variables inside bare tokens that begin with '-'.
cmake -S $srcDir -B $buildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCMAKE_INSTALL_PREFIX=$prefix" `
    "-DCMAKE_PREFIX_PATH=$prefixPath" `
    "-DCMAKE_MODULE_PATH=$($modules -replace '\\','/')" `
    "-DCMAKE_ISPC_COMPILER=$ispcExe" `
    "-DLUA_BIN_LUA=$luaTools\lua.exe" `
    "-DLUA_BIN_LUAC=$luaTools\luac.exe" `
    "-DBUILD_TESTING=$(if ($Tests) { 'YES' } else { 'NO' })" `
    "-DSCENERDL2_BUILD_TESTING=$(if ($Tests) { 'YES' } else { 'NO' })" `
    "-DSCENERDL2_BUILD_PYTHON_BINDINGS=NO" `
    "-DSCENERDL2_BUILD_GRID_UTIL=YES"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Build-time code generation (rdl2_json_exporter for the coredata JSONs)
# runs freshly built executables; the loader must find both the vcpkg DLLs
# and the just-built scene_rdl2 DLLs. No RPATH on Windows - extend PATH.
$vcpkgBin = if ($Config -eq "Debug") { Join-Path $vcpkgInst "debug\bin" } else { Join-Path $vcpkgInst "bin" }
$dllDirs = @(
    $vcpkgBin,
    (Join-Path $vcpkgInst "bin"),
    "$buildDir\lib\common\platform",
    "$buildDir\lib\common\math",
    "$buildDir\lib\common\math\ispc",
    "$buildDir\lib\common\fb_util",
    "$buildDir\lib\common\fb_util\ispc",
    "$buildDir\lib\common\grid_util",
    "$buildDir\lib\common\except",
    "$buildDir\lib\common\rec_time",
    "$buildDir\lib\render\logging",
    "$buildDir\lib\render\util",
    "$buildDir\lib\render\cache",
    "$buildDir\lib\scene\rdl2",
    "$buildDir\lib\pdevunit"
)
$env:PATH = ($dllDirs -join ";") + ";" + $env:PATH

cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Tests) {
    # The upstream CppUnit suites register as CTest tests; the loader env
    # (vcpkg + build-tree DLL dirs) is already on PATH from above.
    ctest --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($Config -eq "Release") {
    cmake --install $buildDir
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host ""
    Write-Host "scene_rdl2 installed to $prefix"
} else {
    Write-Host ""
    Write-Host "scene_rdl2 $Config build complete in $buildDir (not installed; the shared prefix stays Release)"
}
