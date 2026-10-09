# PyNARI tests (master prompt §34). Opt-in: PyNARI is built separately by
# scripts/build-pynari-<platform>, because it needs a Python environment this
# project does not otherwise require.
#
# Each test runs in its own process: ANARI library state and the MoonRay
# runtime are process-global, so isolation is what makes a failure readable.

if(NOT OPENMOONRAY_ANARI_BUILD_PYNARI_TESTS)
    return()
endif()

if(NOT TARGET anari_library_moonray)
    message(STATUS "pynari tests need the moonray device - not added")
    return()
endif()

# The project-local virtualenv, which is also what builds PyNARI.
if(WIN32)
    set(_venv_python "${CMAKE_SOURCE_DIR}/.venv/Scripts/python.exe")
else()
    set(_venv_python "${CMAKE_SOURCE_DIR}/.venv/bin/python")
endif()
if(NOT EXISTS "${_venv_python}")
    message(STATUS
        "pynari tests: no .venv interpreter at ${_venv_python} - not added "
        "(create it with scripts/build-pynari-<platform>)")
    return()
endif()

# The built module, wherever the PyNARI build put it.
file(GLOB_RECURSE _pynari_modules
    "${CMAKE_SOURCE_DIR}/build/pynari/*pynari*.pyd"
    "${CMAKE_SOURCE_DIR}/build/pynari/*pynari*.so")
if(NOT _pynari_modules)
    message(STATUS
        "pynari tests: no built pynari module under build/pynari - not added "
        "(build it with scripts/build-pynari-<platform>)")
    return()
endif()
list(GET _pynari_modules 0 _pynari_module)
get_filename_component(_pynari_module_dir "${_pynari_module}" DIRECTORY)
message(STATUS "PyNARI module found: ${_pynari_module}")

set(_pynari_env "${OPENMOONRAY_ANARI_LOADER_ENV}")
list(APPEND _pynari_env "PYTHONPATH=path_list_prepend:${_pynari_module_dir}")
# The test scripts live next to _dll_path.py, but CTest runs them by absolute
# path, so their directory has to be importable as well.
list(APPEND _pynari_env "PYTHONPATH=path_list_prepend:${CMAKE_CURRENT_SOURCE_DIR}")

# Python 3.8+ on Windows ignores PATH when resolving an extension module's
# dependent DLLs, so the directories are handed to the test through
# _dll_path.py / os.add_dll_directory() instead.
set(_dll_dirs
    "$<TARGET_FILE_DIR:anari_library_moonray>"
    "$<TARGET_FILE_DIR:anari::anari>"
    ${OPENMOONRAY_ANARI_RUNTIME_DIRS})
# '|' rather than the platform path separator: ';' is CMake's own list
# separator *and* the Windows path separator, and escaping it through
# ENVIRONMENT_MODIFICATION is not worth the fragility. _dll_path.py splits on
# the same character.
list(JOIN _dll_dirs "|" _dll_dirs)
list(APPEND _pynari_env "OPENMOONRAY_ANARI_DLL_DIRS=set:${_dll_dirs}")
list(JOIN _pynari_env ";" _pynari_env)

foreach(_test load_device triangle instancing dynamic_updates pbr texture hdri
        spheres)
    add_test(NAME pynari.${_test}
        COMMAND "${_venv_python}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_${_test}.py")
    set_tests_properties(pynari.${_test} PROPERTIES
        LABELS "pynari"
        ENVIRONMENT_MODIFICATION "${_pynari_env}"
    )
endforeach()

# --- install.pynari_triangle (Milestone 14) --------------------------------
# The acceptance criterion for packaging is that the *installed* tree renders,
# from Python as well as from C. This is `pynari.triangle` again, with the
# loader pointed at the install prefix ahead of the build tree - the same
# environment install.load_library uses, plus the DLL directories Python needs
# on Windows.
if(TARGET anari_library_moonray)
    set(_install_prefix "${CMAKE_BINARY_DIR}/install-tree-test")
    set(_install_dll_dirs
        "${_install_prefix}/${CMAKE_INSTALL_BINDIR}"
        "${_install_prefix}/${CMAKE_INSTALL_LIBDIR}"
        "$<TARGET_FILE_DIR:anari::anari>"
        ${OPENMOONRAY_ANARI_RUNTIME_DIRS})
    list(JOIN _install_dll_dirs "|" _install_dll_dirs)

    set(_pynari_install_env_list "${OPENMOONRAY_ANARI_LOADER_ENV}")
    list(APPEND _pynari_install_env_list
        "PYTHONPATH=path_list_prepend:${_pynari_module_dir}"
        "PYTHONPATH=path_list_prepend:${CMAKE_CURRENT_SOURCE_DIR}"
        "OPENMOONRAY_ANARI_DLL_DIRS=set:${_install_dll_dirs}")
    foreach(_var IN LISTS OPENMOONRAY_ANARI_LOADER_ENV_VARS)
        list(APPEND _pynari_install_env_list
            "${_var}=path_list_prepend:${_install_prefix}/${CMAKE_INSTALL_LIBDIR}"
            "${_var}=path_list_prepend:${_install_prefix}/${CMAKE_INSTALL_BINDIR}")
    endforeach()
    list(JOIN _pynari_install_env_list ";" _pynari_install_env_list)

    add_test(NAME install.pynari_triangle
        COMMAND "${_venv_python}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_triangle.py")
    set_tests_properties(install.pynari_triangle PROPERTIES
        FIXTURES_REQUIRED install_tree
        LABELS "pynari;install"
        ENVIRONMENT_MODIFICATION "${_pynari_install_env_list}"
    )
endif()
