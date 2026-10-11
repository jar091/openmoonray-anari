# Copyright 2026 openmoonray-anari contributors
# SPDX-License-Identifier: Apache-2.0

# RDL2 DSOs of the ANARI device that are not part of MoonRay or moonshine.
#
# A standalone project, built like moonshine: out of tree, against the
# *installed* MoonRay (find_package(Moonray)), with Ninja and ISPC, and
# installed into the same prefix so that the DSOs land next to MoonRay's in
# <prefix>/rdl2dso. scripts/build-moonray-windows.ps1 -Target anari_dso sets up
# what this needs: CMAKE_MODULE_PATH (openmoonray's cmake_modules), the
# CMAKE_MODULES_ROOT environment variable (ispc_dso_generate), the ISPC
# compiler and a PATH with the installed rdl2_json_exporter.
#
# Nothing here depends on anything but MoonRay.

cmake_minimum_required(VERSION 3.23.1)

project(AnariMoonrayDso
    VERSION 0.1.0
    LANGUAGES CXX ISPC)

list(APPEND CMAKE_MESSAGE_CONTEXT ${PROJECT_NAME})

include(OMR_Platform)

# default to Release if no build type specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ================================================
# Find dependencies
# ================================================
find_package(TBB REQUIRED)
find_package(Moonray REQUIRED)
find_package(SceneRdl2 REQUIRED)

# Set the RPATH for binaries in the install tree
set(CMAKE_INSTALL_RPATH ${GLOBAL_INSTALL_RPATH})
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# ===============================================
# Set the install directory for rdl2 dsos
# ===============================================
set(RDL2DSO_INSTALL_DIR rdl2dso)
set(RDL2DSO_PROXY_INSTALL_DIR rdl2dso.proxy)

# The flags moonshine builds its libraries with (MoonshineCompileOptions.cmake).
# MoonrayDso.cmake sets none for MSVC.
function(AnariMoonrayDso_cxx_compile_options target)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target}
            PRIVATE
                /arch:AVX2                      # match -march=core-avx2 (also predefines __AVX__/__AVX2__)
                /EHsc                           # standard C++ exception handling
                /bigobj                         # some TUs exceed the default section limit
                /Zc:preprocessor                # conforming preprocessor (VA_ARGS behavior of the assert macros)
                /MP                             # parallel compilation within a project
        )
    endif()
endfunction()

# ================================================
# Add project files
# ================================================
include(MoonrayDso)

add_subdirectory(AnariBumpNormalMap)
