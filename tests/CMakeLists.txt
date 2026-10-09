# Test tree. CTest is the single entry point (master prompt §31).
#
# Layout (populated milestone by milestone):
#   smoke/          - non-rendering skeleton smoke tests (Milestone 0)
#   unit/           - adapter unit tests (Milestone 6+)
#   moonray_native/ - native in-process MoonRay render tests, no ANARI (Milestones 1-3)
#   integration/    - ANARI integration tests via anariLoadLibrary (Milestone 5+)
#   render/         - image-metric render regression tests (Milestone 7+)
#   pynari/         - PyNARI tests (Milestone 7+, option OPENMOONRAY_ANARI_BUILD_PYNARI_TESTS)
#   cts/            - ANARI CTS integration (Milestone 13, option OPENMOONRAY_ANARI_BUILD_CTS)
#   data/           - test fixtures (canonical RDL2 binaries, reference images)

# ---------------------------------------------------------------------------
# Shared runtime environment for tests
# ---------------------------------------------------------------------------
# Two things have to be findable at run time:
#
#   * the shared libraries: the device and the ANARI loader from the build
#     tree, plus - now that the device really renders - MoonRay and its
#     third-party stack, which have no RPATH on Windows;
#   * the MoonRay RDL2 shader DSOs, through RDL2_DSO_PATH.
#
# Both are assembled here so every test layer applies the same environment.

# Directories holding shared libraries the tests load at run time.
set(OPENMOONRAY_ANARI_RUNTIME_DIRS "")
if(OPENMOONRAY_ANARI_MOONRAY_FOUND)
    list(APPEND OPENMOONRAY_ANARI_RUNTIME_DIRS
        "${OPENMOONRAY_ANARI_MOONRAY_PREFIX}/bin"
        "${OPENMOONRAY_ANARI_MOONRAY_PREFIX}/lib")
    if(WIN32)
        set(_vcpkg_bin
            "${CMAKE_SOURCE_DIR}/external/vcpkg-installed/x64-windows/bin")
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            list(APPEND OPENMOONRAY_ANARI_RUNTIME_DIRS
                "${CMAKE_SOURCE_DIR}/external/vcpkg-installed/x64-windows/debug/bin")
        endif()
        list(APPEND OPENMOONRAY_ANARI_RUNTIME_DIRS "${_vcpkg_bin}")
        # Deliberately NOT the OIDN bin directory: the prebuilt OIDN release
        # ships its own tbb12.dll, and putting it on PATH makes MoonRay load
        # that one instead of the oneTBB it was built against (observed:
        # STATUS_ENTRYPOINT_NOT_FOUND, 0xc0000139, in every native test).
        # OIDN is a build-time dependency here; nothing in the test suite
        # runs the denoiser.
    else()
        # MoonRay's own third-party stack (OpenSubdiv, TBB, OpenImageIO, ...)
        # lives in the shared dependency prefix, not in the MoonRay prefix.
        # The build-tree device finds it through CMake's build RPATH, but the
        # *installed* one does not - install.load_library failed with
        # "Library not loaded: @rpath/libosdGPU.3.5.0.dylib" until this was on
        # the loader path.
        list(APPEND OPENMOONRAY_ANARI_RUNTIME_DIRS
            "${CMAKE_SOURCE_DIR}/external/installs/lib")
    endif()
endif()

# path_list_prepend puts each entry in front, so the list is walked backwards
# to make the first directory above end up first in the search path.
list(REVERSE OPENMOONRAY_ANARI_RUNTIME_DIRS)

# Loader search-path environment for tests that load the device through the
# real ANARI loader: PATH (Windows dll search), LD_LIBRARY_PATH (Linux),
# DYLD_FALLBACK_LIBRARY_PATH (macOS — plain-name dlopen falls back to it
# after the anchored attempt next to libanari.dylib). Used by tests/unit
# (object-model tests) and tests/integration.
if(TARGET anari_library_moonray)
    set(OPENMOONRAY_ANARI_LOADER_ENV_VARS PATH LD_LIBRARY_PATH)
    if(APPLE)
        list(APPEND OPENMOONRAY_ANARI_LOADER_ENV_VARS
            DYLD_LIBRARY_PATH DYLD_FALLBACK_LIBRARY_PATH)
    endif()
    set(OPENMOONRAY_ANARI_LOADER_ENV "")
    foreach(_var IN LISTS OPENMOONRAY_ANARI_LOADER_ENV_VARS)
        list(APPEND OPENMOONRAY_ANARI_LOADER_ENV
            "${_var}=path_list_prepend:$<TARGET_FILE_DIR:anari_library_moonray>"
            "${_var}=path_list_prepend:$<TARGET_FILE_DIR:anari::anari>")
        foreach(_dir IN LISTS OPENMOONRAY_ANARI_RUNTIME_DIRS)
            list(APPEND OPENMOONRAY_ANARI_LOADER_ENV
                "${_var}=path_list_prepend:${_dir}")
        endforeach()
    endforeach()
    # The device asks scene_rdl2 to load the shader DSOs; without this it
    # would search next to the *application* executable.
    if(OPENMOONRAY_ANARI_RDL2DSO_DIR)
        list(APPEND OPENMOONRAY_ANARI_LOADER_ENV
            "RDL2_DSO_PATH=set:${OPENMOONRAY_ANARI_RDL2DSO_DIR}")
    endif()
    # MoonRay's XPU mode loads its OptiX PTX from
    # $REZ_MOONRAY_ROOT/shaders/OptixGPUPrograms.ptx - the one piece of
    # MoonRay's rez-based deployment that survives into the open-source build.
    # Without it, asking for xpu falls back to vectorized (which the
    # anari.execution_mode_xpu test reports rather than hides).
    if(OPENMOONRAY_ANARI_MOONRAY_PREFIX)
        list(APPEND OPENMOONRAY_ANARI_LOADER_ENV
            "REZ_MOONRAY_ROOT=set:${OPENMOONRAY_ANARI_MOONRAY_PREFIX}")
    endif()
    list(JOIN OPENMOONRAY_ANARI_LOADER_ENV ";" OPENMOONRAY_ANARI_LOADER_ENV)
endif()

add_subdirectory(smoke)
add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(scene_rdl2)
add_subdirectory(moonray_native)
add_subdirectory(pynari)
add_subdirectory(bench)
add_subdirectory(cts)
