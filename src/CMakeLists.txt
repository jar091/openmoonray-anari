# Milestone 0: version header + diagnostics tool only.
# The ANARI device library (library/, device/, core/, ... — see ARCHITECTURE.md)
# is added starting at Milestone 5, after the native MoonRay render path is
# proven on Windows (Milestones 1-3).

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/openmoonray_anari/Version.h"
    @ONLY
)

add_library(openmoonray_anari_version INTERFACE)
target_include_directories(openmoonray_anari_version
    INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

add_subdirectory(portable)
add_subdirectory(diagnostics)

if(OPENMOONRAY_ANARI_BUILD_DEVICE AND anari_FOUND)
    add_subdirectory(anari_library_moonray)
endif()
