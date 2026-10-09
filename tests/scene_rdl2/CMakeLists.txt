# scene_rdl2 portability layer (TESTING.md: prefix `scene_rdl2.`, label
# "portability;scene_rdl2"). These tests do not rebuild scene_rdl2 — they run
# the upstream CppUnit suites from a scene_rdl2 build tree produced by
# scripts/build-scene-rdl2-windows.ps1 -Tests or
# scripts/build-moonray-macos.sh --tests, so the port's own test coverage is
# reported through this project's single CTest entry point.
#
# The build tree is located via OPENMOONRAY_ANARI_SCENE_RDL2_BUILD_DIR (cache
# variable, defaults to build/scene_rdl2 next to this repo). When the suites
# are absent the tests are simply not registered — with a configure-time
# STATUS message, never a silently passing stub. Which suites exist is
# platform-dependent: common_grid_util is POSIX-only (sockets/shm/NUMA), so
# scene_rdl2.grid_util appears on Linux/macOS and not on Windows.

set(OPENMOONRAY_ANARI_SCENE_RDL2_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/scene_rdl2"
    CACHE PATH "scene_rdl2 build tree containing the upstream CppUnit test suites")

# suite name -> subdirectory of the scene_rdl2 build tree
set(_suites
    "math:tests/lib/common/math:scenerdl2_common_math_tests"
    "math_ispc:tests/lib/common/math/ispc:scenerdl2_common_math_ispc_tests"
    "simd:tests/lib/common/simd:scenerdl2_common_simd_tests"
    "fb_util:tests/lib/common/fb_util:scenerdl2_common_fb_util_tests"
    "grid_util:tests/lib/common/grid_util:scenerdl2_common_grid_util_tests"
    "rec_time:tests/lib/common/rec_time:scenerdl2_common_rec_time_tests"
    "render_cache:tests/lib/render/cache:scenerdl2_render_cache_tests"
    "render_util:tests/lib/render/util:scenerdl2_render_util_tests"
    "scene_context:tests/lib/scene/rdl2:scenerdl2_scene_rdl2_tests"
)

# Runtime library directories of the scene_rdl2 build tree (no RPATH on
# Windows; harmless elsewhere).
set(_srdl2_lib_dirs
    lib/common/platform lib/common/math lib/common/math/ispc
    lib/common/fb_util lib/common/fb_util/ispc lib/common/grid_util
    lib/common/except lib/common/rec_time lib/render/logging
    lib/render/util lib/render/cache lib/scene/rdl2 lib/pdevunit)

set(_registered 0)
foreach(_suite IN LISTS _suites)
    string(REPLACE ":" ";" _parts "${_suite}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _subdir)
    list(GET _parts 2 _exe)

    set(_exePath "${OPENMOONRAY_ANARI_SCENE_RDL2_BUILD_DIR}/${_subdir}/${_exe}${CMAKE_EXECUTABLE_SUFFIX}")
    if(NOT EXISTS "${_exePath}")
        continue()
    endif()

    set(_env "")
    foreach(_var PATH LD_LIBRARY_PATH DYLD_LIBRARY_PATH DYLD_FALLBACK_LIBRARY_PATH)
        foreach(_dir IN LISTS _srdl2_lib_dirs)
            list(APPEND _env
                "${_var}=path_list_prepend:${OPENMOONRAY_ANARI_SCENE_RDL2_BUILD_DIR}/${_dir}")
        endforeach()
    endforeach()
    # Third-party runtime (vcpkg on Windows, external/installs elsewhere).
    if(WIN32)
        foreach(_var PATH)
            list(APPEND _env
                "${_var}=path_list_prepend:${CMAKE_SOURCE_DIR}/external/vcpkg-installed/x64-windows/bin")
        endforeach()
    endif()
    list(JOIN _env ";" _env)

    add_test(NAME scene_rdl2.${_name} COMMAND "${_exePath}")
    set_tests_properties(scene_rdl2.${_name} PROPERTIES
        LABELS "portability;scene_rdl2"
        WORKING_DIRECTORY "${OPENMOONRAY_ANARI_SCENE_RDL2_BUILD_DIR}/${_subdir}"
        ENVIRONMENT_MODIFICATION "${_env}"
        # render_util's MemPool test performs ~33M allocations; that takes
        # minutes in Release and much longer with the debug CRT.
        TIMEOUT 5400
    )
    math(EXPR _registered "${_registered}+1")
endforeach()

if(_registered EQUAL 0)
    message(STATUS "scene_rdl2 test suites not found in "
                   "${OPENMOONRAY_ANARI_SCENE_RDL2_BUILD_DIR} - scene_rdl2.* tests not added "
                   "(build them with scripts/build-scene-rdl2-windows.ps1 -Tests, or\n"
                   "scripts/build-moonray-macos.sh --tests)")
else()
    message(STATUS "scene_rdl2.* portability tests registered: ${_registered}")
endif()

# ---------------------------------------------------------------------------
# scene_rdl2.dso_registration - SceneClass DSO error paths (M4 / ADR 0003 §3)
# ---------------------------------------------------------------------------
# Unlike the suites above this one is ours and links against the INSTALLED
# scene_rdl2, so it needs the same find_package chain the native tests use.
find_package(SceneRdl2 CONFIG QUIET)

if(NOT SceneRdl2_FOUND)
    message(STATUS "scene_rdl2 install not found - scene_rdl2.dso_registration not added")
else()
    # The deliberately malformed plugins. They must be real shared libraries,
    # so they are compiled rather than fabricated at run time; each is named
    # <ClassName><suffix> because that is how Dso derives a file name.
    add_library(dso_stub_no_symbols MODULE dso_stubs/no_symbols.cpp)
    set_target_properties(dso_stub_no_symbols PROPERTIES
        OUTPUT_NAME "NoSymbolsDso" PREFIX "" SUFFIX "${CMAKE_SHARED_MODULE_SUFFIX}")

    add_library(dso_stub_declare_only MODULE dso_stubs/declare_only.cpp)
    set_target_properties(dso_stub_declare_only PROPERTIES
        OUTPUT_NAME "DeclareOnlyDso" PREFIX "" SUFFIX "${CMAKE_SHARED_MODULE_SUFFIX}")

    # The unresolvable dependency. absent_helper is SHARED (so missing_dep
    # really imports it) and is parked in a subdirectory that is never added to
    # the DSO search path or to PATH - that absence is the test.
    add_library(dso_stub_absent_helper SHARED dso_stubs/absent_helper.cpp)
    set_target_properties(dso_stub_absent_helper PROPERTIES
        OUTPUT_NAME "omr_anari_absent_helper"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/not_on_the_path"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/not_on_the_path"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/not_on_the_path"
        # Being in an unreferenced directory is enough on Windows, where the
        # loader searches by file name. On macOS the dependency is recorded by
        # the helper's *install name*, which defaults to its absolute build
        # path - so dyld would find it there and the module would load after
        # all, turning the "unresolved dependency" case into a passing load.
        # An @rpath install name, plus a module with no RPATH below, makes the
        # dependency genuinely unresolvable on every platform.
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_NAME_DIR TRUE)

    add_library(dso_stub_missing_dep MODULE dso_stubs/missing_dep.cpp)
    target_link_libraries(dso_stub_missing_dep PRIVATE dso_stub_absent_helper)
    # Same reason, for the ELF side: CMake's build RPATH would otherwise point
    # at the helper's directory.
    set_target_properties(dso_stub_missing_dep PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "")
    set_target_properties(dso_stub_missing_dep PROPERTIES
        OUTPUT_NAME "MissingDepDso" PREFIX "" SUFFIX "${CMAKE_SHARED_MODULE_SUFFIX}")

    # Keep the three plugins together and away from the helper.
    set(_stub_dir "${CMAKE_CURRENT_BINARY_DIR}/rdl2dso_stubs")
    foreach(_t dso_stub_no_symbols dso_stub_declare_only dso_stub_missing_dep)
        set_target_properties(${_t} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${_stub_dir}"
            RUNTIME_OUTPUT_DIRECTORY "${_stub_dir}")
    endforeach()

    add_executable(scene_rdl2_dso_registration test_dso_registration.cpp)
    target_link_libraries(scene_rdl2_dso_registration PRIVATE SceneRdl2::scene_rdl2)
    target_compile_features(scene_rdl2_dso_registration PRIVATE cxx_std_17)
    add_dependencies(scene_rdl2_dso_registration
        dso_stub_no_symbols dso_stub_declare_only dso_stub_missing_dep)

    set(_dso_env
        "OPENMOONRAY_ANARI_DSO_STUB_DIR=set:${_stub_dir}"
        "OPENMOONRAY_ANARI_DSO_SCRATCH_DIR=set:${CMAKE_CURRENT_BINARY_DIR}/dso_scratch")
    if(WIN32)
        list(APPEND _dso_env
            "PATH=path_list_prepend:${CMAKE_SOURCE_DIR}/external/installs/openmoonray/bin"
            "PATH=path_list_prepend:${CMAKE_SOURCE_DIR}/external/vcpkg-installed/x64-windows/bin")
    endif()

    add_test(NAME scene_rdl2.dso_registration COMMAND scene_rdl2_dso_registration)
    set_tests_properties(scene_rdl2.dso_registration PROPERTIES
        LABELS "portability;scene_rdl2"
        ENVIRONMENT_MODIFICATION "${_dso_env}"
    )
    message(STATUS "scene_rdl2.dso_registration registered")
endif()
