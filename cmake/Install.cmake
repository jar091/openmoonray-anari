# Install rules.
#
# Milestone 0 installs only the diagnostics tool and license/third-party
# notices. Later milestones add: anari_library_moonray, required MoonRay
# runtime libraries, DSO/plugin libraries or static-registry equivalents,
# shader/material resources, generated introspection metadata, and CMake
# package metadata (master prompt §29). Install-tree tests are mandatory
# once the ANARI library exists.

include(GNUInstallDirs)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/openmoonray-anari"
)
