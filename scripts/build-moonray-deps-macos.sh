#!/usr/bin/env bash
# Build the MoonRay third-party dependency stack on macOS (Milestone 1).
#
# Uses the upstream superbuild (external/openmoonray/building/macOS) with:
#   - NO_USD=1               — the ANARI device never uses the Hydra/USD path
#   - target OpenImageDenoise — builds the serial dependency chain up to and
#     including OIDN, which skips Qt5/USD/GLFW/FreeType (GUI/Hydra-only) and
#     OpenSSL/libcurl/libuuid/MicroHttpd (chained after Qt5; OpenSSL and
#     CppUnit for scene_rdl2 come from Homebrew / the chain instead)
#   - CMAKE_POLICY_VERSION_MINIMUM=3.5 — several pinned third-party projects
#     declare cmake_minimum_required < 3.5, which CMake 4.x refuses without it
#   - a project-side Boost patch (scripts/patches/) — AppleClang 21 makes
#     -Wenum-constexpr-conversion a hard error, breaking Boost 1.78's MPL;
#     upstream's pragma-based Boost.patch is no longer sufficient
#
# Everything installs into external/installs — nothing touches $HOME or /usr.
#
# The chain is resumed in a loop: the four known failure modes (see the
# fixup functions below) can only be repaired after the subproject that
# triggers them has been unpacked, so a build from scratch fails once per
# fixup on the way through. Every one of them is a *known* failure with a
# known repair; an unknown one still stops the script.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
deps_build="$repo_root/build/moonray-deps"
install_root="$repo_root/external/installs"

export CMAKE_POLICY_VERSION_MINIMUM=3.5

mkdir -p "$deps_build" "$install_root"/{bin,lib,include}

cmake -S "$repo_root/external/openmoonray/building/macOS" -B "$deps_build" \
    -DNO_USD=1 \
    -DInstallRoot="$install_root"

# The Boost ExternalProject re-runs its update+patch steps on every resume
# (the update step is stampless), and upstream's PATCH_COMMAND lacks the
# `|| true` the other projects have — an already-applied patch aborts the
# chain. Before each resume, reverse upstream's pragma patch so the chain can
# re-apply it cleanly, and (re)apply our C++11 header fix (different hunk,
# see scripts/patches/): AppleClang 21 made -Wenum-constexpr-conversion a
# hard error, so the pragma alone no longer builds Boost 1.78.
prepare_boost_resume() {
    local boost_src="$deps_build/Boost-prefix/src/Boost"
    if [ -d "$boost_src" ]; then
        patch -d "$boost_src" -p3 -R -N \
            < "$repo_root/external/openmoonray/building/macOS/Boost.patch" \
            >/dev/null 2>&1 || true
        patch -d "$boost_src" -p1 -N \
            < "$repo_root/scripts/patches/boost-1.78-integral-wrapper-cxx11.patch" \
            || true
    fi
}

# Embree builds in-source with hundreds of `../`-relative includes; on an
# exFAT (FSKit) volume the same header reached via differently-spelled paths
# gets distinct file IDs, so clang's `#pragma once` dedup fails with
# "redefinition of ..." errors. Relocate the embree source tree to a real
# APFS filesystem (TMPDIR) and leave a symlink behind.
relocate_embree_off_exfat() {
    local src="$deps_build/embree-prefix/src/embree"
    local mount_point
    mount_point="$(df "$deps_build" | awk 'NR==2 {print $NF}')"
    if [ -d "$src" ] && [ ! -L "$src" ] \
        && mount | grep -qE "on $mount_point \(exfat"; then
        local dest="${TMPDIR:-/tmp}/openmoonray-anari-embree"
        echo "[info] relocating embree source to APFS ($dest) - exFAT breaks #pragma once"
        rm -rf "$dest"
        mkdir -p "$(dirname "$dest")"
        cp -R "$src/" "$dest"
        # A previous relocation may have left the parked copy behind, and the
        # ExternalProject download step replaces the symlink with a real
        # directory whenever it re-clones - so `mv` would move the new tree
        # *into* the old one instead of beside it.
        rm -rf "$src.exfat"
        mv "$src" "$src.exfat"
        ln -s "$dest" "$src"
    fi
}

# Lua 5.4.4's luac double-frees a Proto's line-info array when it combines
# more than one source file: combine() releases f->lineinfo without clearing
# the pointer, so the collector frees it a second time. On macOS libmalloc
# catches that and raises SIGTRAP, which is where scene_rdl2's rdlalib step
# dies:
#
#   luac -o rdlalib.luac constructors.lua transforms.lua
#   /bin/sh: line 1: 82644 Trace/BPT trap: 5  .../luac -o rdlalib.luac ...
#   make[2]: *** [lib/scene/rdl2/rdlalib.cc] Error 133
#
# Upstream fixed this after 5.4.4; the one-line patch is carried here because
# the superbuild pins the tarball. Building luac is seconds, so the fixup
# rebuilds and reinstalls it in place rather than invalidating the chain.
# The check is the failure itself rather than the patch state: what matters is
# that the *installed* luac can combine two files, whatever produced it.
fix_lua_luac_combine() {
    local src="$deps_build/Lua-prefix/src/Lua"
    local luac="$install_root/bin/luac"
    local patch="$repo_root/scripts/patches/lua-5.4.4-luac-combine-double-free.patch"
    [ -x "$luac" ] || return 0

    local probe
    probe="$(mktemp -d)"
    printf 'return 1\n' > "$probe/a.lua"
    printf 'return 2\n' > "$probe/b.lua"
    # The subshell keeps the shell's own "Trace/BPT trap: 5" job report - which
    # is what the failure looks like here - out of the script's output.
    if ( "$luac" -o "$probe/out.luac" "$probe/a.lua" "$probe/b.lua" ) >/dev/null 2>&1; then
        rm -rf "$probe"
        return 0
    fi
    rm -rf "$probe"

    echo "[info] installed luac cannot combine multiple files - patching Lua 5.4.4"
    patch -d "$src" -p1 -N --silent < "$patch" || true
    ( cd "$src" && make generic MYCFLAGS=-fPIC MYLIBS=-ldl >/dev/null 2>&1 \
      && cp src/lua src/luac "$install_root/bin" ) || true
}

# Log4CPlus is an autotools project cloned by the chain. git gives every file
# the checkout time, and on exFAT (2-second timestamp granularity, and no
# ordering guarantee within a checkout) `m4/*.m4` can end up a second newer
# than the `aclocal.m4` they generate. make then tries to regenerate it and
# runs into automake's `missing` shim, which demands the exact `aclocal-1.16`
# the tarball was made with:
#
#   .../missing: line 81: aclocal-1.16: command not found
#   make[4]: *** [aclocal.m4] Error 127
#
# Installing a newer automake does not help (the Makefile asks for -1.16 by
# name). Restoring the generated-after-their-inputs ordering does, and needs
# no extra tooling.
fix_autotools_timestamps() {
    local src="$deps_build/Log4CPlus-prefix/src/Log4CPlus"
    [ -d "$src" ] || return 0
    ( cd "$src" || return 0
      find . \( -name '*.am' -o -name 'configure.ac' -o -name '*.m4' \) \
           ! -name 'aclocal.m4' -exec touch -t 202601010000 {} +
      [ -f aclocal.m4 ] && touch -t 202601010001 aclocal.m4
      [ -f configure ] && touch -t 202601010002 configure
      find . -name '*.h.in' -o -name '*.hxx.in' -exec touch -t 202601010002 {} +
      find . -name 'Makefile.in' -exec touch -t 202601010003 {} + ) || true
}

# OpenVDB's FindTBB consults pkg-config, which happily returns Homebrew's
# oneTBB (2021+ API) even though CMAKE_IGNORE_PATH excludes /opt/homebrew —
# the Cellar path is not literally /opt/homebrew. Pin the include dir to the
# chain-built TBB 2020 whenever the OpenVDB build tree exists.
pin_openvdb_tbb() {
    local vdb_build="$deps_build/OpenVDB-prefix/src/OpenVDB-build"
    if [ -f "$vdb_build/CMakeCache.txt" ]; then
        cmake -DTbb_INCLUDE_DIR="$install_root/include" "$vdb_build" >/dev/null
    fi
}

# Each of the fixups above can only be applied once the subproject it targets
# has been unpacked, so a build from scratch necessarily fails once per fixup
# before the chain gets past it. The cap only exists to stop an *unknown*
# failure from looping forever, so it has to stay comfortably above the number
# of known ones (currently four: Boost, OpenVDB/TBB, Log4CPlus, embree).
# Boost's patch step re-runs on every invocation against an existing build
# tree (its update step is stampless and upstream's PATCH_COMMAND lacks the
# `|| true`), so a *resumed* run fails on it before anything else. Doing the
# reverse-then-reapply once up front turns that guaranteed failure into a
# no-op; on a first run the source does not exist yet and this does nothing.
prepare_boost_resume

attempts=0
until cmake --build "$deps_build" --target OpenImageDenoise; do
    attempts=$((attempts + 1))
    if [ "$attempts" -ge 8 ]; then
        echo "[FAIL] dependency chain still failing after $attempts resumes" >&2
        exit 1
    fi
    echo "[info] chain failed - applying known fixups and resuming (attempt $attempts)"
    prepare_boost_resume
    pin_openvdb_tbb
    fix_autotools_timestamps
    relocate_embree_off_exfat
done

# Not part of the resume loop: the luac defect does not fail the chain, it
# fails the *consumer* (scene_rdl2's rdlalib step), so it has to be repaired
# once the chain is done rather than when it breaks.
fix_lua_luac_combine

echo
echo "MoonRay dependencies installed to $install_root"
