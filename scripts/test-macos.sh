#!/usr/bin/env bash
# Run the openmoonray-anari test suite on macOS.
# Usage: ./scripts/test-macos.sh [debug|release] [ctest-label]
set -euo pipefail

config="${1:-release}"
label="${2:-}"
preset="macos-clang-${config}"
cd "$(dirname "$0")/.."

args=(--preset "$preset" --output-on-failure)
if [ -n "$label" ]; then
    args+=(-L "$label")
fi
ctest "${args[@]}"
