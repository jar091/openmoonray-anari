#!/usr/bin/env bash
# Build and install the minimal MoonRay stack on macOS (Milestone 1), after
# scripts/build-moonray-deps-macos.sh has populated external/installs.
#
# Deviations from upstream's macOS_build.md, all deliberate:
#   - scene_rdl2 and moonray are built as standalone projects instead of the
#     openmoonray superproject: the superproject unconditionally descends
#     into arras/hydra/moonshine_usd/... submodules that are not part of the
#     pinned minimal checkout (docs/MINIMAL_MOONRAY_DEPENDENCIES.md).
#   - Unix Makefiles instead of the Xcode generator: the Xcode generator
#     needs `xcodebuild -runFirstLaunch` (admin) on this host, and the
#     non-Xcode path enables ISPC as a first-class CMake language, which is
#     what the Linux build uses too. Not Ninja: on POSIX its ISPC support
#     doubles CMAKE_BINARY_DIR into the generated dependency edge, so the
#     build stops with "Transcendental_ispc_stubs.h ... missing and no known
#     rule to make it". (Windows uses Ninja and needs the other half of the
#     same CMake quirk - see GLOBAL_ISPC_HEADER_DIR_PREFIX in
#     cmake_modules/cmake/OMR_Platform.cmake.)
#   - Sources are copied to an APFS location (TMPDIR) before building when
#     the repo lives on exFAT: FSKit-exFAT breaks clang's `#pragma once`
#     dedup for differently-spelled paths to the same header (proven by the
#     embree build; see build-moonray-deps-macos.sh).
#   - OpenSSL comes from Homebrew (chained after Qt5 in the upstream deps
#     superbuild, which we do not build).
#
# Installs to external/installs/openmoonray. Nothing touches $HOME.
#
# Usage:  ./scripts/build-moonray-macos.sh [--tests] [extra -D args...]
#
#   --tests   also build (and run) the upstream scene_rdl2 CppUnit suites, the
#             macOS counterpart of `build-scene-rdl2-windows.ps1 -Tests`. They
#             are what reports the M2 shared-code changes on this platform;
#             tests/scene_rdl2/ registers them into this project's CTest from
#             the same build tree (build/scene_rdl2).
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"

build_tests=NO
cmake_extra=()
for arg in "$@"; do
    case "$arg" in
        --tests) build_tests=YES ;;
        *) cmake_extra+=("$arg") ;;
    esac
done
installs="$repo_root/external/installs"
prefix="$installs/openmoonray"
src_root="$repo_root/external/openmoonray"

openssl_root="${OPENSSL_ROOT_DIR:-/opt/homebrew/opt/openssl@3}"
if [ ! -d "$openssl_root" ]; then
    echo "OpenSSL not found at $openssl_root - brew install openssl@3" >&2
    exit 1
fi

# Copy sources off exFAT if needed (same failure mode as embree).
build_src="$src_root"
mount_point="$(df "$repo_root" | awk 'NR==2 {print $NF}')"
if mount | grep -qE "on $mount_point \(exfat"; then
    build_src="${TMPDIR:-/tmp}/openmoonray-anari-moonray-src"
    echo "[info] repo is on exFAT - syncing MoonRay sources to $build_src"
    mkdir -p "$build_src"
    for d in cmake_modules moonray/scene_rdl2 moonray/mcrt_denoise moonray/moonray; do
        mkdir -p "$build_src/$(dirname "$d")"
        rsync -a --delete "$src_root/$d/" "$build_src/$d/"
    done
fi

# Upstream bug (ISPC-language path only, i.e. every non-Xcode generator): the
# AppleClang-only `-Wno-gnu-alignof-expression` is declared PUBLIC, so it
# propagates into ISPC compiles, which abort with "Unknown option". The Xcode
# path compiles ISPC via custom commands and never hits this. Guard the flag
# to CXX in the build copy.
for f in "$build_src/moonray/scene_rdl2/cmake/SceneRdl2CompileOptions.cmake" \
         "$build_src/moonray/moonray/cmake/MoonrayCompileOptions.cmake"; do
    if [ -f "$f" ]; then
        sed -i '' \
            's/^\( *\)-Wno-gnu-alignof-expression$/\1$<$<COMPILE_LANGUAGE:CXX>:-Wno-gnu-alignof-expression>/' \
            "$f"
    fi
done

# Upstream bug #2 (Xcode-only glob): the ad-hoc codesign step for
# rdl2_ispc_util uses `*/rdl2_ispc_util`, which only matches Xcode's
# per-config subdirectory. Use the portable generator expression instead.
sed -i '' \
    's|codesign -s - -f -o linker-signed \*/rdl2_ispc_util|codesign -s - -f -o linker-signed $<TARGET_FILE:rdl2_ispc_util>|' \
    "$build_src/moonray/scene_rdl2/lib/scene/rdl2/CMakeLists.txt"

export CMAKE_POLICY_VERSION_MINIMUM=3.5

# Environment hints mirroring upstream CMakeMacOSPresets.json
# (macos-environment), pointing at the chain-built installs.
export Boost_ROOT="$installs"
export CppUnit_ROOT="$installs"
export JsonCpp_ROOT="$installs"
export Log4cplus_ROOT="$installs"
export LUA_DIR="$installs"
export OpenSubDiv_ROOT="$installs"
export OpenVDB_ROOT="$installs"
export Random123_ROOT="$installs"
export ISPC="$installs/bin/ispc"
# Standalone moonray: MoonrayDso.cmake locates the ispc_dso_generate helper
# via superproject-relative paths or this env var.
export CMAKE_MODULES_ROOT="$build_src/cmake_modules"
# The DSO coredata step invokes scene_rdl2's rdl2_json_exporter by bare name.
export PATH="$prefix/bin:$PATH"

common_args=(
    -G "Unix Makefiles"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$prefix"
    -DCMAKE_PREFIX_PATH="$installs;$prefix"
    -DCMAKE_MODULE_PATH="$build_src/cmake_modules/cmake"
    -DCMAKE_ISPC_COMPILER="$installs/bin/ispc"
    -DOPENSSL_ROOT_DIR="$openssl_root"
    # The scene_rdl2 python bindings (mod/python, no off-switch) must compile
    # against the same Python that Boost.Python 1.78 was built for; the
    # upstream recipe pins Xcode's Python 3.9 (user-config.jam), and 1.78
    # predates the C-API removals of Python 3.13+. The project's own tooling
    # still uses the repo-local .venv — this pin is internal to the MoonRay
    # stack build.
    -DPython_EXECUTABLE=/usr/bin/python3
    # Upstream guards test trees with `(standalone OR <PKG>_BUILD_TESTING)
    # AND BUILD_TESTING`; standalone builds need the global switch off.
    -DBUILD_TESTING=NO
)

jobs="$(sysctl -n hw.ncpu)"

# CMake refuses to change the generator of an existing build tree, so a tree
# left behind by a different generator has to be dropped rather than
# reconfigured.
drop_foreign_generator() {
    local dir="$1"
    local cache="$dir/CMakeCache.txt"
    [ -f "$cache" ] || return 0
    if ! grep -q '^CMAKE_GENERATOR:INTERNAL=Unix Makefiles$' "$cache"; then
        echo "[info] $dir was configured with a different generator - reconfiguring from scratch"
        rm -rf "$dir"
    fi
}

echo "== scene_rdl2 =="
drop_foreign_generator "$repo_root/build/scene_rdl2"
# BUILD_TESTING is in common_args as NO; the upstream gate is
# `(standalone OR <PKG>_BUILD_TESTING) AND BUILD_TESTING`, so both have to
# flip for --tests.
srdl2_args=(-DSCENERDL2_BUILD_TESTING="$build_tests")
if [ "$build_tests" = YES ]; then
    srdl2_args+=(-DBUILD_TESTING=YES)
fi
cmake -S "$build_src/moonray/scene_rdl2" -B "$repo_root/build/scene_rdl2" \
    "${common_args[@]}" "${srdl2_args[@]}" ${cmake_extra[@]+"${cmake_extra[@]}"}
cmake --build "$repo_root/build/scene_rdl2" -j"$jobs"
if [ "$build_tests" = YES ]; then
    # Run them here as well as through the project's CTest layer: a failure at
    # this point is a scene_rdl2 problem, not an openmoonray-anari one.
    ctest --test-dir "$repo_root/build/scene_rdl2" --output-on-failure
fi
cmake --install "$repo_root/build/scene_rdl2"
# Upstream's install rules omit the arm64 emulation headers that
# scene_rdl2/common/math/Math.h includes on Apple Silicon.
mkdir -p "$prefix/include/scene_rdl2/common/arm"
cp "$build_src/moonray/scene_rdl2/lib/common/arm/"*.h \
   "$prefix/include/scene_rdl2/common/arm/"

echo "== mcrt_denoise =="
drop_foreign_generator "$repo_root/build/mcrt_denoise"
cmake -S "$build_src/moonray/mcrt_denoise" -B "$repo_root/build/mcrt_denoise" \
    "${common_args[@]}" -DMCRTDENOISE_BUILD_TESTING=NO \
    ${cmake_extra[@]+"${cmake_extra[@]}"}
cmake --build "$repo_root/build/mcrt_denoise" -j"$jobs"
cmake --install "$repo_root/build/mcrt_denoise"

echo "== moonray =="
drop_foreign_generator "$repo_root/build/moonray"
# MOONRAY_USE_METAL defaults YES on Darwin (XPU/OIDN-Metal); the CPU baseline
# is the portability target (master prompt §27) and the METAL language is not
# available with the Makefiles generator here.
cmake -S "$build_src/moonray/moonray" -B "$repo_root/build/moonray" \
    "${common_args[@]}" -DMOONRAY_BUILD_TESTING=NO -DMOONRAY_USE_METAL=NO \
    ${cmake_extra[@]+"${cmake_extra[@]}"}
cmake --build "$repo_root/build/moonray" -j"$jobs"
cmake --install "$repo_root/build/moonray"

echo
echo "MoonRay installed to $prefix"
