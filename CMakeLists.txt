# openmoonray-anari — Khronos ANARI device backed by the OpenMoonRay renderer.
#
# Milestone 0 state: project skeleton, pinned-baseline diagnostics tool and a
# non-rendering CTest smoke target. OpenMoonRay / ANARI-SDK integration is
# added in later milestones (see MILESTONES.md); cmake/OpenMoonRay.cmake and
# cmake/Dependencies.cmake document the pinned versions.

cmake_minimum_required(VERSION 3.24)

project(openmoonray_anari
    VERSION 0.1.0
    DESCRIPTION "Khronos ANARI device implementation backed by OpenMoonRay"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Project options (see CLAUDE_CODE_MASTER_PROMPT_OPENMOONRAY_ANARI.md §23)
# ---------------------------------------------------------------------------
option(OPENMOONRAY_ANARI_BUILD_TESTS               "Build unit/integration tests"                    ON)
option(OPENMOONRAY_ANARI_BUILD_EXAMPLES            "Build C++ examples"                              ON)
option(OPENMOONRAY_ANARI_BUILD_PYNARI_TESTS        "Enable PyNARI-based tests (needs Python+pynari)" OFF)
option(OPENMOONRAY_ANARI_BUILD_CTS                 "Enable ANARI Conformance Test Suite integration" OFF)
option(OPENMOONRAY_ANARI_FETCH_DEPENDENCIES        "Fetch pinned dependencies during configure"      OFF)
option(OPENMOONRAY_ANARI_ENABLE_ASAN               "Enable AddressSanitizer"                         OFF)
option(OPENMOONRAY_ANARI_ENABLE_UBSAN              "Enable UndefinedBehaviorSanitizer"               OFF)
option(OPENMOONRAY_ANARI_WARNINGS_AS_ERRORS        "Treat project warnings as errors"                OFF)
option(OPENMOONRAY_ANARI_ENABLE_STATIC_DSO_REGISTRY "Bootstrap: statically register MoonRay SceneClass DSOs" OFF)
option(OPENMOONRAY_ANARI_ENABLE_XPU                "Enable optional XPU/GPU acceleration (later milestone)" OFF)
option(OPENMOONRAY_ANARI_BUILD_DEVICE              "Build the anari_library_moonray device (needs installed ANARI-SDK)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Install layout (CMAKE_INSTALL_BINDIR/LIBDIR/...). Included here, before the
# subdirectories, because both the install rules and the install-tree tests
# need it: GNUInstallDirs defines those as cache variables, so a stale cache
# hid their absence until the first configure from scratch.
include(GNUInstallDirs)

include(CompilerWarnings)
include(Sanitizers)
include(Dependencies)
# MoonRay itself: the ANARI device links it, and the native render tests use
# it directly. Found once at top level so both see the same install.
include(OpenMoonRay)

if(OPENMOONRAY_ANARI_BUILD_DEVICE)
    # The pinned SDK install from scripts (external/install/anari-sdk) is found
    # automatically; an existing installation is supplied the usual way via
    # CMAKE_PREFIX_PATH / anari_DIR. Found at top level so the imported
    # anari:: targets are visible in src/ and tests/.
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/external/install/anari-sdk")
    find_package(anari QUIET COMPONENTS code_gen)
    if(NOT anari_FOUND)
        message(STATUS "ANARI-SDK not found - skipping anari_library_moonray "
                       "(install the SDK or set anari_DIR / CMAKE_PREFIX_PATH)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(OPENMOONRAY_ANARI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(Install)
