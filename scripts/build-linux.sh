#!/usr/bin/env bash
# Configure and build openmoonray-anari on Linux.
# Usage: ./scripts/build-linux.sh [debug|release]
set -euo pipefail

config="${1:-release}"
preset="linux-gcc-${config}"
cd "$(dirname "$0")/.."

cmake --preset "$preset"
cmake --build --preset "$preset" -- -j"$(nproc)"
