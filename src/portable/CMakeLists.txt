# Portable platform-abstraction helpers (master prompt §9, §10.2).
# Clean reimplementations of the audited historical Windows-port areas,
# regression-tested in tests/unit. These helpers are candidates for the
# Milestone 2 scene_rdl2 patch content.

add_library(openmoonray_anari_portable STATIC
    DynamicLibrary.cpp
    DynamicLibrary.h
    AlignedMemory.h
    Bits.h
    Endian.h
    MathCompat.h
)

target_include_directories(openmoonray_anari_portable PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

if(NOT WIN32)
    target_link_libraries(openmoonray_anari_portable PUBLIC ${CMAKE_DL_LIBS})
endif()

openmoonray_anari_set_warnings(openmoonray_anari_portable)
openmoonray_anari_enable_sanitizers(openmoonray_anari_portable)
