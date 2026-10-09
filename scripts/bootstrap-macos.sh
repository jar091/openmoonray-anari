#!/usr/bin/env bash
# Bootstrap the macOS development environment for openmoonray-anari.
# Milestone 0 scope: verify the required toolchain. Apple Silicon first;
# CPU rendering is the baseline, Metal/XPU is optional (master prompt §27).
set -euo pipefail

fail=0
check() {
    local name="$1" cmd="$2" hint="$3"
    if command -v "$cmd" >/dev/null 2>&1; then
        echo "[ok]   $name found: $(command -v "$cmd")"
    else
        echo "[FAIL] $name not found. $hint"
        fail=1
    fi
}

echo "openmoonray-anari macOS bootstrap"
echo "================================="
check "CMake" cmake "brew install cmake"
check "Git" git "Install Xcode command line tools: xcode-select --install"
check "AppleClang" clang++ "Install Xcode command line tools: xcode-select --install"
check "Make" make "Install Xcode command line tools."
check "Python" python3 "brew install python (needed for PyNARI tests in later milestones)."

if [ "$fail" -ne 0 ]; then
    echo
    echo "Bootstrap FAILED."
    exit 1
fi

"$(dirname "$0")/fetch-dependencies.sh"

echo
echo "Bootstrap OK. Next steps:"
echo "  cmake --preset macos-clang-debug"
echo "  cmake --build --preset macos-clang-debug"
echo "  ctest --preset macos-clang-debug --output-on-failure"
