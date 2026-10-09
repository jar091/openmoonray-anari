#!/usr/bin/env bash
# Check out the pinned upstream sources (Linux/macOS).
#
# The pins are git submodules (see .gitmodules / DEPENDENCIES.md), so this is
# a thin wrapper around `git submodule update` plus a verification pass -
# the same shape as scripts/fetch-dependencies.ps1. It replaces the old
# hand-maintained clone list, which cloned into external/ *next to* the
# submodules and duplicated the pins that now live in .gitmodules.
#
# Usage:  ./scripts/fetch-dependencies.sh

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

echo "[run]  git submodule update --init (top level)"
if ! git submodule update --init \
        external/openmoonray external/anari-sdk external/pynari external/vcpkg; then
    echo
    echo "Submodule checkout failed. If this is the first run after the"
    echo "MoonRay repositories moved into our namespace, make sure they"
    echo "have been published (scripts/publish-windows-port.ps1)."
    exit 1
fi

# Deliberately NOT --recursive: openmoonray carries ~20 submodules (arras,
# hydra, moonshine_usd, dcc plugins, ...) that the minimal build does not use
# (docs/MINIMAL_MOONRAY_DEPENDENCIES.md). Init only what we build.
echo "[run]  git submodule update --init (openmoonray: minimal set)"
git -C external/openmoonray submodule update --init \
    moonray/scene_rdl2 moonray/moonray moonray/mcrt_denoise moonray/moonshine cmake_modules

# --- verification -----------------------------------------------------------
# A submodule left at the wrong commit (e.g. a stale `git pull` in a checkout)
# silently changes what gets built, so report the state rather than assume it.
paths=(
    "external/openmoonray:openmoonray (windows-port)"
    "external/openmoonray/moonray/scene_rdl2:scene_rdl2"
    "external/openmoonray/moonray/moonray:moonray"
    "external/openmoonray/moonray/mcrt_denoise:mcrt_denoise"
    "external/openmoonray/cmake_modules:cmake_modules"
    "external/anari-sdk:ANARI-SDK"
    "external/pynari:PyNARI"
    "external/vcpkg:vcpkg"
)

echo
echo "Checked-out pins:"
problems=()
for entry in "${paths[@]}"; do
    path="${entry%%:*}"
    name="${entry#*:}"
    if [ ! -e "$path/.git" ]; then
        problems+=("$path is missing")
        printf '  %-46s MISSING\n' "$name"
        continue
    fi
    sha="$(git -C "$path" rev-parse --short HEAD)"
    desc="$(git -C "$path" describe --tags --always 2>/dev/null || true)"
    dirty=""
    if [ -n "$(git -C "$path" status --porcelain)" ]; then
        dirty=" [locally modified]"
    fi
    printf '  %-46s %s %s%s\n' "$name" "$sha" "$desc" "$dirty"
done

# The vcpkg checkout and the manifest baseline must agree, or the dependency
# versions silently differ from the ones recorded in the manifest. (vcpkg is
# only used by the Windows build, but the check is cheap and keeps the two
# scripts saying the same thing.)
if [ -e external/vcpkg/.git ]; then
    manifest_baseline="$(sed -n 's/.*"builtin-baseline"[[:space:]]*:[[:space:]]*"\([0-9a-f]*\)".*/\1/p' \
        scripts/vcpkg/vcpkg.json)"
    vcpkg_sha="$(git -C external/vcpkg rev-parse HEAD)"
    if [ -n "$manifest_baseline" ] && [ "$manifest_baseline" != "$vcpkg_sha" ]; then
        problems+=("vcpkg submodule ($vcpkg_sha) != manifest builtin-baseline ($manifest_baseline)")
    fi
fi

if [ ${#problems[@]} -gt 0 ]; then
    echo
    echo "Problems:"
    for p in "${problems[@]}"; do echo "  - $p"; done
    exit 1
fi

echo
echo "All pinned sources present."
