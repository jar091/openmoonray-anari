#!/usr/bin/env bash
# Build PyNARI (pinned submodule external/pynari) against the project's
# ANARI-SDK install, for the pynari.* CTest layer (Milestone 7).
#
# Mirrors scripts/build-pynari-windows.ps1. PyNARI is consumed as-is from the
# pinned fork (external/pynari, https://github.com/jar091/pynari, branch
# mjar/devel); this script never patches the checkout. The setParameter overload
# ordering the project depends on is a commit in that fork, not a patch step
# (PYNARI.md). The only project-side choices are which Python and which
# pybind11 it builds against:
#
#   * Python comes from the project-local .venv, never a system interpreter.
#     That needs PYBIND11_FINDPYTHON=ON: without it pybind11 takes CMake's
#     deprecated FindPythonInterp/FindPythonLibs path, which ignores
#     Python_EXECUTABLE and picks whatever `python3` it finds first - on this
#     host Homebrew's 3.9, producing a pynari.cpython-39-darwin.so the venv
#     interpreter cannot import.
#   * pybind11 comes from that venv (PYNARI_USE_INSTALLED_PYBIND, an option
#     PyNARI provides): its vendored copy is 2.12, which predates Python 3.13.
#
# The module lands in build/pynari; tests/pynari points PYTHONPATH at it.
#
# Usage:  ./scripts/build-pynari-macos.sh

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src_dir="$repo_root/external/pynari"
build_dir="$repo_root/build/pynari"
anari_dir="$repo_root/external/install/anari-sdk"
venv="$repo_root/.venv"
python_exe="$venv/bin/python"

if [ ! -f "$src_dir/CMakeLists.txt" ]; then
    echo "external/pynari is not checked out - run ./scripts/fetch-dependencies.sh first." >&2
    exit 1
fi
if [ ! -d "$anari_dir/lib/cmake" ]; then
    echo "ANARI-SDK not installed - run ./scripts/build-anari-sdk-macos.sh first." >&2
    exit 1
fi

if [ ! -x "$python_exe" ]; then
    echo "[run]  python3 -m venv .venv"
    python3 -m venv "$venv"
fi

echo "[run]  pip install numpy pybind11"
"$python_exe" -m pip install --quiet --upgrade pip
"$python_exe" -m pip install --quiet numpy pybind11

pybind11_dir="$("$python_exe" -c 'import pybind11; print(pybind11.get_cmake_dir())')"
ext_suffix="$("$python_exe" -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX"))')"

# A build tree configured against a different interpreter keeps using it (the
# Python paths are cached), so drop it rather than produce a module the venv
# cannot import.
if [ -f "$build_dir/CMakeCache.txt" ] \
   && ! { grep -q "^Python_EXECUTABLE:.*=$python_exe\$" "$build_dir/CMakeCache.txt" \
          && grep -q "^PYBIND11_FINDPYTHON:BOOL=ON\$" "$build_dir/CMakeCache.txt"; }; then
    echo "[info] build/pynari was configured against another interpreter - reconfiguring from scratch"
    rm -rf "$build_dir"
fi

cmake -S "$src_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$anari_dir" \
    -DPYNARI_USE_INSTALLED_PYBIND=ON \
    -Dpybind11_DIR="$pybind11_dir" \
    -DPYBIND11_FINDPYTHON=ON \
    -DPython_EXECUTABLE="$python_exe" \
    -DPython3_EXECUTABLE="$python_exe" \
    -DPYTHON_EXECUTABLE="$python_exe"

cmake --build "$build_dir" --parallel

module="$(find "$build_dir" -name "pynari*.so" -print -quit)"
if [ -z "$module" ]; then
    echo "the build produced no pynari*.so" >&2
    exit 1
fi

# The extension suffix carries the Python ABI, so this is what catches a
# module built against the wrong interpreter - which is silent otherwise until
# `import pynari` fails inside CTest.
case "$module" in
    *"$ext_suffix") ;;
    *)
        echo "built $module, but $python_exe expects a module ending in $ext_suffix" >&2
        echo "(PyNARI was configured against a different interpreter)" >&2
        exit 1
        ;;
esac

echo
echo "PyNARI built: $module"
echo "Reconfigure the project with -DOPENMOONRAY_ANARI_BUILD_PYNARI_TESTS=ON"
echo "to pick up the pynari.* tests."
