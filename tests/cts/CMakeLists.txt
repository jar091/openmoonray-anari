# The ANARI CTS's introspection commands, checked against what this device is
# recorded as answering (tests/cts/README.md). The image-comparison run is not
# a CTest: it takes about ten minutes and it scores against another device's
# renders, so its baseline is a recorded file and a documented command.

if(NOT TARGET anari_library_moonray)
    return()
endif()

find_program(OPENMOONRAY_ANARI_CTS_EXECUTABLE anariCts
    HINTS
        "${anari_DIR}/../../../${CMAKE_INSTALL_BINDIR}"
        "${CMAKE_SOURCE_DIR}/external/install/anari-sdk/bin"
)

if(NOT OPENMOONRAY_ANARI_CTS_EXECUTABLE)
    message(STATUS
        "anariCts not found: the cts.* tests are skipped. Build it with "
        "scripts/build-anari-sdk-windows.ps1 -Cts")
    return()
endif()

set(_compare "${CMAKE_CURRENT_SOURCE_DIR}/CompareOutput.cmake")

foreach(_check query_features check_properties)
    if(_check STREQUAL "query_features")
        set(_command "query-features")
        set(_expected "${CMAKE_CURRENT_SOURCE_DIR}/expected-features.txt")
    else()
        set(_command "check-properties")
        set(_expected "${CMAKE_CURRENT_SOURCE_DIR}/expected-properties.txt")
    endif()

    add_test(NAME cts.${_check}
        COMMAND "${CMAKE_COMMAND}"
            "-DTOOL=${OPENMOONRAY_ANARI_CTS_EXECUTABLE}"
            "-DTOOL_ARGS=${_command} moonray"
            "-DEXPECTED_FILE=${_expected}"
            -P "${_compare}"
    )
    set_tests_properties(cts.${_check} PROPERTIES
        LABELS "cts;anari"
        ENVIRONMENT_MODIFICATION "${OPENMOONRAY_ANARI_LOADER_ENV}"
    )
endforeach()
