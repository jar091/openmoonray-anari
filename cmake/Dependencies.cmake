# Upstream baseline for openmoonray-anari - the UPSTREAM ORIGIN of each
# dependency, recorded for provenance and for `git fetch upstream` rebases.
#
# NOTE: these are no longer the checkout pins. The sources are git submodules
# under external/ (see .gitmodules and DEPENDENCIES.md) and the submodule
# commits are what actually gets built; `git submodule status` is the
# machine-readable truth. The values here say where the code came FROM.
#
# The MoonRay repositories additionally exist as our own forks
# (github.com/jar091/*, branch `windows-port`) because
# they carry the Windows/MSVC port - docs/WINDOWS_PORT.md.
#
# Verified against upstream on 2026-08-16 (see docs/adr/0001-openmoonray-version.md).

# --- OpenMoonRay (top-level release; submodule SHAs below are as selected by the tag)
set(OPENMOONRAY_ANARI_OPENMOONRAY_TAG      "v2026.29.1")
set(OPENMOONRAY_ANARI_OPENMOONRAY_URL      "https://github.com/OpenMoonRay/openmoonray")
set(OPENMOONRAY_ANARI_OPENMOONRAY_FORK_URL "https://github.com/jar091/openmoonray")
set(OPENMOONRAY_ANARI_PORT_BRANCH          "windows-port")
set(OPENMOONRAY_ANARI_SCENE_RDL2_SHA       "d3da034")
set(OPENMOONRAY_ANARI_MOONRAY_SHA          "eef67ae")
set(OPENMOONRAY_ANARI_MOONSHINE_SHA        "a3c8667")
set(OPENMOONRAY_ANARI_MATERIALX_SHADERS_SHA "9b83c00")
set(OPENMOONRAY_ANARI_MCRT_DENOISE_SHA     "0050e72")

# --- ANARI
set(OPENMOONRAY_ANARI_ANARI_SDK_TAG        "v0.16.0")
set(OPENMOONRAY_ANARI_ANARI_SDK_URL        "https://github.com/KhronosGroup/ANARI-SDK")
set(OPENMOONRAY_ANARI_ANARI_SPEC_VERSION   "1.1")

# --- PyNARI (mjar/devel branch, pinned commit). The submodule points at the
# project fork (origin), with ingowald/pynari as upstream; see PYNARI.md.
set(OPENMOONRAY_ANARI_PYNARI_COMMIT        "de99e5064e506f374bc614d56bb9120c71db818c")
set(OPENMOONRAY_ANARI_PYNARI_URL           "https://github.com/jar091/pynari")
set(OPENMOONRAY_ANARI_PYNARI_UPSTREAM_URL  "https://github.com/ingowald/pynari")

# Existing-installation workflow (MILESTONES.md, §24.2 of the master prompt):
# an already-built OpenMoonRay / ANARI-SDK can be supplied via
#   CMAKE_PREFIX_PATH, OpenMoonRay_DIR, scene_rdl2_DIR, anari_DIR
# Package discovery is wired up in the milestones that first link against them.

if(OPENMOONRAY_ANARI_FETCH_DEPENDENCIES)
    message(STATUS "OPENMOONRAY_ANARI_FETCH_DEPENDENCIES is ON, but dependency "
                   "fetching is not implemented yet (Milestone 1+). Pins: "
                   "OpenMoonRay ${OPENMOONRAY_ANARI_OPENMOONRAY_TAG}, "
                   "ANARI-SDK ${OPENMOONRAY_ANARI_ANARI_SDK_TAG}")
endif()
