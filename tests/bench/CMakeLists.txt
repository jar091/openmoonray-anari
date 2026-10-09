# Milestone 16: the adapter's own cost, measured and asserted.
#
# The timings are printed rather than asserted - they are hardware-dependent,
# and a test that fails on a busy machine teaches nothing. What *is* asserted
# is the property that makes the numbers meaningful: an update stays
# incremental as the scene grows (docs/PERFORMANCE.md).

if(NOT TARGET anari_library_moonray)
    return()
endif()

add_executable(bench_updates bench_updates.cpp)
target_link_libraries(bench_updates PRIVATE anari::anari)
target_include_directories(bench_updates
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common")
openmoonray_anari_set_warnings(bench_updates)

# Two sizes: the assertions are about what does *not* grow with the scene, so
# the test is only meaningful when it runs at more than one size.
foreach(_instances 8 64)
    add_test(NAME bench.update_cost_${_instances}
        COMMAND bench_updates ${_instances})
    set_tests_properties(bench.update_cost_${_instances} PROPERTIES
        LABELS "bench;render"
        ENVIRONMENT_MODIFICATION "${OPENMOONRAY_ANARI_LOADER_ENV}"
    )
endforeach()

# And one with a prototype big enough for a copy to be visible: 80k triangles
# shared by 8 instances. This is the size at which "moving an instance does not
# rewrite its prototype" stops being a count and becomes a duration - the sync
# for a moved instance has to stay within a small factor of a sync with nothing
# to do (docs/PERFORMANCE.md).
add_test(NAME bench.update_cost_large_prototype
    COMMAND bench_updates 8 200)
set_tests_properties(bench.update_cost_large_prototype PROPERTIES
    LABELS "bench;render"
    ENVIRONMENT_MODIFICATION "${OPENMOONRAY_ANARI_LOADER_ENV}"
)
