#!/usr/bin/env bash
# Configure and build openmoonray-anari on macOS.
# Usage: ./scripts/build-macos.sh [debug|release]
set -euo pipefail

config="${1:-release}"
preset="macos-clang-${config}"
cd "$(dirname "$0")/.."

cmake --preset "$preset"
cmake --build --preset "$preset" -- -j"$(sysctl -n hw.ncpu)"
