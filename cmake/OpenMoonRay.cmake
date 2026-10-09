# OpenMoonRay discovery.
#
# Two supported modes (master prompt §24):
#
#   1. Existing installation, supplied the usual CMake way:
#        -DCMAKE_PREFIX_PATH=<prefix> / -DMoonray_DIR / -DSceneRdl2_DIR
#   2. The project's own pinned build in external/installs/openmoonray,
#        produced by scripts/build-scene-rdl2-* and scripts/build-moonray-*.
#
# Sets, for the whole project:
#   OPENMOONRAY_ANARI_MOONRAY_FOUND   TRUE when both packages were found
#   OPENMOONRAY_ANARI_MOONRAY_PREFIX  install prefix of the found MoonRay
#   OPENMOONRAY_ANARI_RDL2DSO_DIR     <prefix>/rdl2dso, the shader DSO path
#                                     the renderer needs at run time
#
# Imported targets used by the project: Moonray::rendering_rndr,
# SceneRdl2::scene_rdl2.

include_guard(GLOBAL)

list(APPEND CMAKE_PREFIX_PATH
    "${CMAKE_SOURCE_DIR}/external/installs/openmoonray"
    "${CMAKE_SOURCE_DIR}/external/installs")

# On Windows MoonRay's own third-party stack comes from the project-local
# vcpkg tree (scripts/build-moonray-deps-windows.ps1), and the Moonray /
# SceneRdl2 config packages resolve Boost, TBB, OpenImageIO, ... through it.
# Debug first for a debug build, exactly as the MoonRay build scripts do:
# mixing an MDd binary with MD dependency DLLs crashes wherever a std type
# crosses the DLL boundary.
if(WIN32)
    set(_vcpkg_root "${CMAKE_SOURCE_DIR}/external/vcpkg-installed/x64-windows")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND CMAKE_PREFIX_PATH "${_vcpkg_root}/debug" "${_vcpkg_root}")
    else()
        list(APPEND CMAKE_PREFIX_PATH "${_vcpkg_root}")
    endif()
    # OIDN is not in vcpkg; the dependency script unpacks the prebuilt
    # release into external/tools, and MoonrayConfig.cmake looks for its
    # config package.
    file(GLOB _oidn_dirs "${CMAKE_SOURCE_DIR}/external/tools/oidn-*")
    list(APPEND CMAKE_PREFIX_PATH ${_oidn_dirs})
endif()

# The Moonray/SceneRdl2 config packages call find_dependency() on packages that
# only upstream's find modules can locate (TBB, OpenSubdiv, ...), so a consumer
# has to provide those modules too. Boost resolves through BoostConfig from the
# dependency install (CMake 4 dropped FindBoost).
list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_SOURCE_DIR}/external/openmoonray/cmake_modules/cmake"
    "${CMAKE_SOURCE_DIR}/external/openmoonray/moonray/scene_rdl2/cmake"
    "${CMAKE_SOURCE_DIR}/external/openmoonray/moonray/moonray/cmake")
set(CMAKE_POLICY_DEFAULT_CMP0167 NEW)
# Upstream FindTBB.cmake declares a cmake_minimum_required below 3.5, which
# CMake 4.x refuses without a floor.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# MoonrayConfig.cmake calls find_dependency(BISON) even though bison is only a
# build-time generator, so consumers need to find it as well. On Windows it
# comes from the win_flex_bison archive that build-moonray-deps-windows.ps1
# unpacks into external/tools (CMake's FindBISON knows the win_bison name); on
# Linux/macOS it is a system package and this hint is simply absent.
if(WIN32)
    file(GLOB _win_flex_bison_dirs
         "${CMAKE_SOURCE_DIR}/external/tools/win_flex_bison-*")
    list(APPEND CMAKE_PROGRAM_PATH ${_win_flex_bison_dirs})
endif()

# A MoonRay built with its GPU path (MOONRAY_USE_OPTIX=YES, i.e.
# scripts/build-moonray-windows.ps1 -Xpu) exports link dependencies on
# CUDA::cudart and OptiX::OptiX, so a *consumer* has to be able to resolve
# those targets as well - otherwise the exported target set fails to load with
# "The link interface of target Moonray::rendering_rt contains CUDA::cudart but
# the target was not found". Both lookups are QUIET and optional: a CPU-only
# MoonRay install references neither.
find_package(CUDAToolkit QUIET)
if(NOT DEFINED ENV{OPTIX_ROOT} AND WIN32)
    file(GLOB _optix_sdk_dirs "$ENV{ProgramData}/NVIDIA Corporation/OptiX SDK*")
    if(_optix_sdk_dirs)
        list(SORT _optix_sdk_dirs)
        list(GET _optix_sdk_dirs -1 _optix_sdk_dir)
        set(ENV{OPTIX_ROOT} "${_optix_sdk_dir}")
    endif()
endif()
find_package(OptiX QUIET)

find_package(Moonray CONFIG QUIET)
find_package(SceneRdl2 CONFIG QUIET)

# The device writes MoonRay's volume files itself (VdbGeometry reads an
# OpenVDB file and has no in-memory entry point), so it links OpenVDB
# directly. MoonrayConfig.cmake already find_dependency()s it, but the
# imported target that call creates is not guaranteed to be visible here, and
# an explicit find_package is what makes "the device needs OpenVDB" a fact of
# this project rather than a side effect of MoonRay's package.
find_package(OpenVDB QUIET)
# Which imported target that produced depends on which find module answered:
# upstream MoonRay's (cmake_modules/cmake/FindOpenVDB.cmake, on the module path
# above) defines OpenVDB::OpenVDB, OpenVDB's own defines OpenVDB::openvdb.
if(TARGET OpenVDB::OpenVDB)
    set(OPENMOONRAY_ANARI_OPENVDB_TARGET OpenVDB::OpenVDB)
elseif(TARGET OpenVDB::openvdb)
    set(OPENMOONRAY_ANARI_OPENVDB_TARGET OpenVDB::openvdb)
else()
    set(OPENMOONRAY_ANARI_OPENVDB_TARGET "")
    message(STATUS
        "OpenVDB not found - the ANARI device is built without volume support")
endif()

if(Moonray_FOUND AND SceneRdl2_FOUND)
    # A MoonRay install carries the configuration it was built in. Linking a
    # Debug project against a Release MoonRay mixes CRTs on Windows, so this
    # is a hard mismatch, not a warning.
    get_target_property(_moonray_configs Moonray::rendering_rndr
        IMPORTED_CONFIGURATIONS)
    if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug"
       AND NOT "DEBUG" IN_LIST _moonray_configs)
        set(OPENMOONRAY_ANARI_MOONRAY_FOUND FALSE)
        message(STATUS
            "MoonRay install at ${Moonray_DIR} provides ${_moonray_configs} "
            "only - a Debug build of the device would mix CRTs, so the ANARI "
            "device and the native render tests are not built. Use the "
            "Release preset, or install a Debug MoonRay.")
        return()
    endif()

    set(OPENMOONRAY_ANARI_MOONRAY_FOUND TRUE)
    # Moonray_DIR points at <prefix>/lib/cmake/Moonray-<version>.
    get_filename_component(OPENMOONRAY_ANARI_MOONRAY_PREFIX
        "${Moonray_DIR}/../../.." ABSOLUTE)
    set(OPENMOONRAY_ANARI_RDL2DSO_DIR
        "${OPENMOONRAY_ANARI_MOONRAY_PREFIX}/rdl2dso")
    message(STATUS
        "MoonRay found: ${OPENMOONRAY_ANARI_MOONRAY_PREFIX} "
        "(RDL2 DSOs: ${OPENMOONRAY_ANARI_RDL2DSO_DIR})")
else()
    set(OPENMOONRAY_ANARI_MOONRAY_FOUND FALSE)
    message(STATUS
        "MoonRay not found - the ANARI device and the native render tests are "
        "not built (build it with scripts/build-moonray-<platform> or set "
        "Moonray_DIR / CMAKE_PREFIX_PATH)")
endif()
