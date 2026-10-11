# ANARI integration tests (master prompt Â§31.2). Only built when the device
# library target exists. Tests load the device dynamically via the ANARI
# loader; PATH/LD_LIBRARY_PATH is extended so the loader finds both anari and
# anari_library_moonray from the build tree.

if(NOT TARGET anari_library_moonray)
    return()
endif()

add_executable(integration_load_library test_load_library.cpp)
target_link_libraries(integration_load_library PRIVATE anari::anari)
openmoonray_anari_set_warnings(integration_load_library)

# Loader search-path environment computed in tests/CMakeLists.txt (shared
# with the loader-based unit tests).
set(_loader_env_vars "${OPENMOONRAY_ANARI_LOADER_ENV_VARS}")
set(_loader_env "${OPENMOONRAY_ANARI_LOADER_ENV}")

add_test(NAME anari.load_library COMMAND integration_load_library)
set_tests_properties(anari.load_library PROPERTIES
    LABELS "integration;anari"
    ENVIRONMENT_MODIFICATION "${_loader_env}"
)

# --- Milestone 7: the first end-to-end ANARI + MoonRay renders -------------
# One executable, one mode per test, so a crash in one render cannot take the
# others' results with it.
add_executable(integration_render test_render.cpp)
target_link_libraries(integration_render PRIVATE anari::anari)
target_include_directories(integration_render PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(integration_render)

foreach(_mode triangle_matte frame_color_float frame_depth frame_resize)
    add_test(NAME anari.${_mode} COMMAND integration_render ${_mode})
    set_tests_properties(anari.${_mode} PROPERTIES
        LABELS "integration;anari;render"
        ENVIRONMENT_MODIFICATION "${_loader_env}"
    )
endforeach()

# --- Milestone 8: retained-mode updates ------------------------------------
add_executable(integration_updates test_updates.cpp)
target_link_libraries(integration_updates PRIVATE anari::anari)
target_include_directories(integration_updates
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(integration_updates)

foreach(_mode camera_update material_update light_update instance_updates
        shared_group shared_group_attributes)
    add_test(NAME anari.${_mode} COMMAND integration_updates ${_mode})
    set_tests_properties(anari.${_mode} PROPERTIES
        LABELS "integration;anari;render"
        ENVIRONMENT_MODIFICATION "${_loader_env}"
    )
endforeach()

# --- Milestone 9: materials and textures -----------------------------------
add_executable(integration_materials test_materials.cpp)
target_link_libraries(integration_materials PRIVATE anari::anari)
target_include_directories(integration_materials
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(integration_materials)

foreach(_mode physically_based image_texture)
    add_test(NAME anari.${_mode} COMMAND integration_materials ${_mode})
    set_tests_properties(anari.${_mode} PROPERTIES
        LABELS "integration;anari;render"
        ENVIRONMENT_MODIFICATION "${_loader_env}"
    )
endforeach()

# --- Milestone 10: cameras, lights, frame channels --------------------------
add_executable(integration_cameras_lights test_cameras_lights.cpp)
target_link_libraries(integration_cameras_lights PRIVATE anari::anari)
target_include_directories(integration_cameras_lights
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(integration_cameras_lights)

foreach(_mode orthographic_camera point_light quad_light hdri_light)
    add_test(NAME anari.${_mode} COMMAND integration_cameras_lights ${_mode})
    set_tests_properties(anari.${_mode} PROPERTIES
        LABELS "integration;anari;render"
        ENVIRONMENT_MODIFICATION "${_loader_env}"
    )
endforeach()

# --- Milestone 11: geometry coverage ---------------------------------------
add_executable(integration_geometry test_geometry.cpp)
target_link_libraries(integration_geometry PRIVATE anari::anari)
target_include_directories(integration_geometry
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(integration_geometry)

foreach(_mode sphere_geometry curve_geometry vertex_normals vertex_colors
        unusable_attributes)
    add_test(NAME anari.${_mode} COMMAND integration_geometry ${_mode})
    set_tests_properties(anari.${_mode} PROPERTIES
        LABELS "integration;anari;render"
        ENVIRONMENT_MODIFICATION "${_loader_env}"
    )
endforeach()

# --- Milestone 12: volumes -------------------------------------------------
add_executable(integration_volume test_volume.cpp)
target_link_libraries(integration_volume PRIVATE anari::anari)
target_include_directories(integration_volume
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(integration_volume)

foreach(_mode volume_absorption volume_transfer_function)
    add_test(NAME anari.${_mode} COMMAND integration_volume ${_mode})
    set_tests_properties(anari.${_mode} PROPERTIES
        LABELS "integration;anari;render"
        ENVIRONMENT_MODIFICATION "${_loader_env}"
    )
endforeach()

# --- Milestone 13: compliance, introspection, robustness -------------------
add_executable(integration_compliance test_compliance.cpp)
target_link_libraries(integration_compliance PRIVATE anari::anari)
target_include_directories(integration_compliance
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(integration_compliance)

foreach(_mode debug_layer malformed_input advertised_subtypes scene_churn)
    add_test(NAME anari.${_mode} COMMAND integration_compliance ${_mode})
    set_tests_properties(anari.${_mode} PROPERTIES
        LABELS "integration;anari;render"
        ENVIRONMENT_MODIFICATION "${_loader_env}"
    )
endforeach()

# --- Milestone 13: device lifecycle ----------------------------------------
# The CTS makes one device per test case in a single process; this is that,
# without the CTS. The generation count is a compromise between catching a
# leak and keeping the suite quick - raise it by hand when chasing one.
add_executable(integration_device_lifecycle test_device_lifecycle.cpp)
target_link_libraries(integration_device_lifecycle PRIVATE anari::anari)
target_include_directories(integration_device_lifecycle
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(integration_device_lifecycle)

add_test(NAME anari.device_lifecycle COMMAND integration_device_lifecycle 64)
set_tests_properties(anari.device_lifecycle PROPERTIES
    LABELS "integration;anari;render"
    ENVIRONMENT_MODIFICATION "${_loader_env}"
)

# The other half: one device, many scenes. This is what caught the recycled
# addresses - a new ANARI object where a destroyed one used to be inheriting
# its RDL2 object, and the frame coming back with no geometry in it.
add_test(NAME anari.scene_generations
    COMMAND integration_device_lifecycle 64 shared)
set_tests_properties(anari.scene_generations PROPERTIES
    LABELS "integration;anari;render"
    ENVIRONMENT_MODIFICATION "${_loader_env}"
)

# --- Milestone 15: MoonRay's execution modes -------------------------------
# One process per mode: MoonRay has one renderer per process and the execution
# mode lives on its global options (docs/adr/0005).
add_executable(integration_execution_mode test_execution_mode.cpp)
target_link_libraries(integration_execution_mode PRIVATE anari::anari)
target_include_directories(integration_execution_mode
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(integration_execution_mode)

foreach(_mode scalar vectorized xpu)
    add_test(NAME anari.execution_mode_${_mode}
        COMMAND integration_execution_mode ${_mode})
    set_tests_properties(anari.execution_mode_${_mode} PROPERTIES
        LABELS "integration;anari;render"
        ENVIRONMENT_MODIFICATION "${_loader_env}"
    )
endforeach()

# --- emitting surfaces sampled by lights (emissionSampling) -----------------
# A closed room lit by a lamp, as an emitting surface and with MeshLights on
# it: the same light with far less noise, both sides, a textured emission, an
# `rdla` material, edits, the bound on the number of lights, ray visibility,
# a thousand instances in one light, and one scene after the other on one
# device.
add_executable(integration_mesh_lights test_mesh_lights.cpp)
target_link_libraries(integration_mesh_lights PRIVATE anari::anari)
target_include_directories(integration_mesh_lights
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(integration_mesh_lights)

foreach(_mode noise sides reflecting textured rdla spaces updates limit
        visibility instances churn)
    add_test(NAME anari.mesh_lights_${_mode}
        COMMAND integration_mesh_lights ${_mode})
    set_tests_properties(anari.mesh_lights_${_mode} PROPERTIES
        LABELS "integration;anari;render"
        ENVIRONMENT_MODIFICATION "${_loader_env}"
    )
endforeach()

# --- sdk.anari_info --------------------------------------------------------
# Runs the SDK's introspection tool against the moonray device (master prompt
# Â§31.4). PASS_REGULAR_EXPRESSION requires the device to actually enumerate;
# a load failure prints nothing matching. sdk.anari_cat is deferred: anariCat
# is a GUI tool built on anari_viewer (glfw/OpenGL) and cannot run headless.
find_program(OPENMOONRAY_ANARI_INFO_EXECUTABLE anariInfo
    HINTS
        "${anari_DIR}/../../../${CMAKE_INSTALL_BINDIR}"
        "${CMAKE_SOURCE_DIR}/external/install/anari-sdk/bin"
)
if(OPENMOONRAY_ANARI_INFO_EXECUTABLE)
    add_test(NAME sdk.anari_info
        COMMAND "${OPENMOONRAY_ANARI_INFO_EXECUTABLE}" -l moonray)
    set_tests_properties(sdk.anari_info PROPERTIES
        LABELS "sdk"
        ENVIRONMENT_MODIFICATION "${_loader_env}"
        PASS_REGULAR_EXPRESSION "Device \"default\""
    )
else()
    message(STATUS "anariInfo not found - sdk.anari_info test not added")
endif()

# --- install.load_library --------------------------------------------------
# Install-tree loading test (master prompt Â§29): install the project into a
# test-local prefix, then load the device from *that* tree â€” the installed
# lib/bin dirs are prepended ahead of the build tree so the installed copy
# wins the loader search.
set(_install_test_prefix "${CMAKE_BINARY_DIR}/install-tree-test")
add_test(NAME install.tree
    COMMAND "${CMAKE_COMMAND}"
        --install "${CMAKE_BINARY_DIR}" --config $<CONFIG>
        --prefix "${_install_test_prefix}")
set_tests_properties(install.tree PROPERTIES
    FIXTURES_SETUP install_tree
    LABELS "install"
)

set(_install_env "")
foreach(_var IN LISTS _loader_env_vars)
    list(APPEND _install_env
        "${_var}=path_list_prepend:${_install_test_prefix}/${CMAKE_INSTALL_LIBDIR}"
        "${_var}=path_list_prepend:${_install_test_prefix}/${CMAKE_INSTALL_BINDIR}"
        "${_var}=path_list_prepend:$<TARGET_FILE_DIR:anari::anari>")
    # MoonRay and its third-party stack are not part of this project's install
    # tree, so they still come from where they were built.
    foreach(_dir IN LISTS OPENMOONRAY_ANARI_RUNTIME_DIRS)
        list(APPEND _install_env "${_var}=path_list_prepend:${_dir}")
    endforeach()
endforeach()
if(OPENMOONRAY_ANARI_RDL2DSO_DIR)
    list(APPEND _install_env "RDL2_DSO_PATH=set:${OPENMOONRAY_ANARI_RDL2DSO_DIR}")
endif()
list(JOIN _install_env ";" _install_env)

add_test(NAME install.load_library COMMAND integration_load_library)
set_tests_properties(install.load_library PROPERTIES
    FIXTURES_REQUIRED install_tree
    LABELS "integration;install"
    ENVIRONMENT_MODIFICATION "${_install_env}"
)
