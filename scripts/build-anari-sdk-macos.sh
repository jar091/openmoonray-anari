#!/usr/bin/env bash
# Build and install the pinned ANARI-SDK on macOS (Milestone 5+).
#
# Mirrors scripts/build-anari-sdk-windows.ps1: the SDK is a git submodule
# (external/anari-sdk, pinned to v0.16.0), installed into
# external/install/anari-sdk, which the top-level CMakeLists puts on
# CMAKE_PREFIX_PATH automatically. Nothing touches system paths.
#
# BUILD_EXAMPLES=ON is required: it is what builds anariInfo, which the
# project's sdk.anari_info test runs against the moonray device.
#
# NOTE: written alongside the Windows script during the Milestone 7 session,
# which had no macOS host. It has not been run on macOS yet - if it needs a
# fix, that fix belongs here rather than in a one-off command line.
#
# Usage:  ./scripts/build-anari-sdk-macos.sh [Release|Debug]

set -euo pipefail

config="${1:-Release}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src_dir="$repo_root/external/anari-sdk"
build_dir="$repo_root/build/anari-sdk-$(echo "$config" | tr '[:upper:]' '[:lower:]')"
prefix="$repo_root/external/install/anari-sdk"

if [ ! -f "$src_dir/CMakeLists.txt" ]; then
    echo "external/anari-sdk is not checked out - run ./scripts/fetch-dependencies.sh first." >&2
    exit 1
fi

# The SDK generates its query/introspection code with a Python script at build
# time. Prefer the project-local virtualenv when it exists (BUILDING.md).
python_exe="$(command -v python3 || true)"
if [ -x "$repo_root/.venv/bin/python" ]; then
    python_exe="$repo_root/.venv/bin/python"
fi
if [ -z "$python_exe" ]; then
    echo "python3 not found - the ANARI-SDK code generator needs it." >&2
    exit 1
fi

cmake -S "$src_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE="$config" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DPython3_EXECUTABLE="$python_exe" \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TESTING=OFF \
    -DBUILD_EXAMPLES=ON \
    -DBUILD_VIEWER=OFF \
    -DBUILD_CTS=OFF \
    -DBUILD_CAT=OFF \
    -DINSTALL_VIEWER=OFF

cmake --build "$build_dir" --parallel
cmake --install "$build_dir"

echo
echo "ANARI-SDK ($config) installed to $prefix"
