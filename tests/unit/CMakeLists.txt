# Unit tests for the project-owned portability helpers (master prompt §31.1).
# Test names follow TESTING.md: unit.<area>, labels "unit;portability".

add_library(omra_test_plugin MODULE test_plugin.cpp)
set_target_properties(omra_test_plugin PROPERTIES PREFIX "")

function(omra_add_unit_test name)
    add_executable(unit_${name} test_${name}.cpp)
    target_link_libraries(unit_${name} PRIVATE openmoonray_anari_portable)
    target_include_directories(unit_${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    openmoonray_anari_set_warnings(unit_${name})
    openmoonray_anari_enable_sanitizers(unit_${name})
    add_test(NAME unit.${name} COMMAND unit_${name} ${ARGN})
    set_tests_properties(unit.${name} PROPERTIES LABELS "unit;portability")
endfunction()

omra_add_unit_test(bits)
omra_add_unit_test(varint)
omra_add_unit_test(endian)
omra_add_unit_test(aligned_memory)
omra_add_unit_test(sincos)
omra_add_unit_test(dynamic_library $<TARGET_FILE:omra_test_plugin>)

# --- Milestone 6 groundwork: retained-object-model tests -------------------
# Exercised through the PUBLIC ANARI API (anariLoadLibrary) — no internal
# adapter classes are linked. unit.commit_snapshot / unit.dirty_propagation
# land with Milestone 7, when rendering makes their effects observable.
if(TARGET anari_library_moonray)
    add_executable(unit_object_model test_object_model.cpp)
    target_link_libraries(unit_object_model PRIVATE anari::anari)
    openmoonray_anari_set_warnings(unit_object_model)

    foreach(_mode parameter_store array_lifetime object_references)
        add_test(NAME unit.${_mode} COMMAND unit_object_model ${_mode})
        set_tests_properties(unit.${_mode} PROPERTIES
            LABELS "unit"
            ENVIRONMENT_MODIFICATION "${OPENMOONRAY_ANARI_LOADER_ENV}")
    endforeach()

    # --- Milestone 6 completion (unblocked by the Milestone 7 render) ------
    # commit_snapshot and dirty_propagation judge "did the change take
    # effect?" by the rendered image, which is the only externally visible
    # answer - so they could not exist before rendering did.
    add_executable(unit_retained_updates test_retained_updates.cpp)
    target_link_libraries(unit_retained_updates PRIVATE anari::anari)
    target_include_directories(unit_retained_updates
        PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
    openmoonray_anari_set_warnings(unit_retained_updates)

    foreach(_mode commit_snapshot dirty_propagation)
        add_test(NAME unit.${_mode} COMMAND unit_retained_updates ${_mode})
        set_tests_properties(unit.${_mode} PROPERTIES
            LABELS "unit;render"
            ENVIRONMENT_MODIFICATION "${OPENMOONRAY_ANARI_LOADER_ENV}")
    endforeach()
endif()
