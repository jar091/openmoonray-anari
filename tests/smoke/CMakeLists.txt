# Milestone 0 smoke tests: prove the toolchain, CTest wiring and the pinned
# baseline plumbing without any rendering.

add_test(NAME smoke.version_info COMMAND openmoonray_anari_diag)
set_tests_properties(smoke.version_info PROPERTIES
    PASS_REGULAR_EXPRESSION "OpenMoonRay baseline: v[0-9]+\\.[0-9]+\\.[0-9]+"
    LABELS "smoke"
)

add_test(NAME smoke.pointer_width COMMAND openmoonray_anari_diag)
set_tests_properties(smoke.pointer_width PROPERTIES
    PASS_REGULAR_EXPRESSION "pointer width: 64 bits"
    LABELS "smoke"
)
