# Native in-process MoonRay render tests — no ANARI (Milestone 1/3).
#
# MoonRay is found once at top level (cmake/OpenMoonRay.cmake); the default
# developer flow (scripts/build-moonray-*) installs it into
# external/installs/openmoonray, an existing installation is supplied via
# CMAKE_PREFIX_PATH / Moonray_DIR.
#
# These tests separate MoonRay portability problems from ANARI adapter
# problems and must pass before any ANARI rendering work (TESTING.md).

if(NOT OPENMOONRAY_ANARI_MOONRAY_FOUND)
    return()
endif()

# Shared library directories (MoonRay + its third-party stack) and the RDL2
# DSO path; the native tests do not go through the ANARI loader, so they take
# the runtime dirs directly rather than the loader environment.
set(_native_env "")
foreach(_dir IN LISTS OPENMOONRAY_ANARI_RUNTIME_DIRS)
    list(APPEND _native_env "PATH=path_list_prepend:${_dir}")
    list(APPEND _native_env "LD_LIBRARY_PATH=path_list_prepend:${_dir}")
    if(APPLE)
        list(APPEND _native_env "DYLD_LIBRARY_PATH=path_list_prepend:${_dir}")
    endif()
endforeach()
list(APPEND _native_env "RDL2_DSO_PATH=set:${OPENMOONRAY_ANARI_RDL2DSO_DIR}")
list(JOIN _native_env ";" _native_env)

add_executable(native_smoke_render test_smoke_render.cpp)
target_link_libraries(native_smoke_render
    PRIVATE
        Moonray::rendering_rndr
        SceneRdl2::scene_rdl2
)
# MoonRay requires C++17 with its pinned toolchain set; match upstream.
target_compile_features(native_smoke_render PRIVATE cxx_std_17)

add_test(NAME native.smoke_render COMMAND native_smoke_render)
set_tests_properties(native.smoke_render PROPERTIES
    LABELS "native"
    ENVIRONMENT_MODIFICATION "${_native_env}"
)

# ---------------------------------------------------------------------------
# Cross-platform framebuffer reference (Milestone 3 acceptance)
# ---------------------------------------------------------------------------
add_executable(native_render_reference render_reference.cpp)
target_link_libraries(native_render_reference
    PRIVATE
        Moonray::rendering_rndr
        SceneRdl2::scene_rdl2
        OpenImageIO::OpenImageIO
)
target_compile_features(native_render_reference PRIVATE cxx_std_17)

set(_reference_dir "${CMAKE_SOURCE_DIR}/tests/references")

# Tolerances, set from measured Windows-vs-macOS data (session 7) rather than
# from the Windows-only guess they started as.
#
# The guess was that SCALAR takes ISPC out of the picture and therefore admits
# a tight per-pixel bar. It does not. Measured against the committed
# Windows-produced references, an arm64/AppleClang render differs by
#
#   mode        rmse      max       mean |diff|
#   scalar      5.7e-3    2.6e-2    2.3e-5 .. 7.7e-5
#   vectorized  5.6e-3    2.5e-2    2.6e-6 .. 6.0e-5
#
# - the same per-pixel spread in both modes, over ~96% of the pixels. What
# differs across platforms is not the arithmetic but the *sample sequence*
# (the renders use 2x2 samples per pixel, and each platform is bit-identical
# with itself), so the residual is Monte Carlo noise. Noise is zero-mean,
# which is why the image means agree to five decimal places while individual
# pixels are 10% apart.
#
# So the per-pixel bars only catch gross divergence, and are the same for both
# modes; the *mean* is what carries a tight cross-platform assertion, and a
# real shading regression (a wrong albedo, a wrong light intensity, a missing
# term) moves it immediately.
set(_rmse_tol_scalar     2e-2)
set(_max_tol_scalar      1e-1)
set(_mean_tol_scalar     5e-4)
set(_rmse_tol_vectorized 2e-2)
set(_max_tol_vectorized  1e-1)
set(_mean_tol_vectorized 5e-4)

# Run-to-run reproducibility on ONE machine, checked before the cross-platform
# comparison is allowed to mean anything. SCALAR is bit-identical. VECTORIZED
# is not: the per-lane/per-tile accumulation order follows thread scheduling,
# measured at max 7.5e-8 (~1 ULP near 1.0) between two renders in one process.
set(_selfcheck_tol_scalar     0)
set(_selfcheck_tol_vectorized 1e-6)

foreach(_mode scalar vectorized)
    foreach(_size 64 128)
        set(_ref "${_reference_dir}/smoke_${_size}_${_mode}.exr")
        if(EXISTS "${_ref}")
            add_test(NAME native.reference_${_size}_${_mode}
                COMMAND native_render_reference
                    --mode ${_mode} --width ${_size} --height ${_size}
                    --compare "${_ref}"
                    --rmse-tol ${_rmse_tol_${_mode}}
                    --max-tol ${_max_tol_${_mode}}
                    --mean-tol ${_mean_tol_${_mode}}
                    --selfcheck
                    --selfcheck-tol ${_selfcheck_tol_${_mode}}
            )
            set_tests_properties(native.reference_${_size}_${_mode} PROPERTIES
                LABELS "native"
                ENVIRONMENT_MODIFICATION "${_native_env}"
            )
        else()
            message(STATUS
                "No reference ${_ref} - test native.reference_${_size}_${_mode} not added "
                "(generate it with the native_reference_generate target)")
        endif()
    endforeach()
endforeach()

# Regenerating the references is deliberately a manual target, never part of
# the test run: a test that rewrites its own expectation can never fail.
add_custom_target(native_reference_generate
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_reference_dir}"
    COMMAND ${CMAKE_COMMAND} -E env "RDL2_DSO_PATH=${OPENMOONRAY_ANARI_RDL2DSO_DIR}"
        $<TARGET_FILE:native_render_reference> --mode scalar --width 64 --height 64
        --write "${_reference_dir}/smoke_64_scalar.exr"
    COMMAND ${CMAKE_COMMAND} -E env "RDL2_DSO_PATH=${OPENMOONRAY_ANARI_RDL2DSO_DIR}"
        $<TARGET_FILE:native_render_reference> --mode scalar --width 128 --height 128
        --write "${_reference_dir}/smoke_128_scalar.exr"
    COMMAND ${CMAKE_COMMAND} -E env "RDL2_DSO_PATH=${OPENMOONRAY_ANARI_RDL2DSO_DIR}"
        $<TARGET_FILE:native_render_reference> --mode vectorized --width 64 --height 64
        --write "${_reference_dir}/smoke_64_vectorized.exr"
    COMMAND ${CMAKE_COMMAND} -E env "RDL2_DSO_PATH=${OPENMOONRAY_ANARI_RDL2DSO_DIR}"
        $<TARGET_FILE:native_render_reference> --mode vectorized --width 128 --height 128
        --write "${_reference_dir}/smoke_128_vectorized.exr"
    DEPENDS native_render_reference
    COMMENT "Regenerating MoonRay framebuffer references in tests/references"
    VERBATIM
)
