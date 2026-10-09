#!/usr/bin/env bash
# Bootstrap the Linux development environment for openmoonray-anari.
# Milestone 0 scope: verify the required toolchain. Later milestones extend
# this with pinned OpenMoonRay dependency checkout/build.
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

echo "openmoonray-anari Linux bootstrap"
echo "================================="
check "CMake" cmake "Install cmake >= 3.25."
check "Git" git "Install git."
check "GCC" g++ "Install g++ (or adapt the preset for clang)."
check "Make" make "Install make."
check "Python" python3 "Install python3 (needed for PyNARI tests in later milestones)."

if [ "$fail" -ne 0 ]; then
    echo
    echo "Bootstrap FAILED."
    exit 1
fi

"$(dirname "$0")/fetch-dependencies.sh"

echo
echo "Bootstrap OK. Next steps:"
echo "  cmake --preset linux-gcc-debug"
echo "  cmake --build --preset linux-gcc-debug"
echo "  ctest --preset linux-gcc-debug --output-on-failure"
