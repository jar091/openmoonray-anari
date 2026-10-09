# Runs a command and compares its output to a recorded file, line by line.
#
# The CTS tools print to stdout and exit 0 whatever they find, so "it ran" is
# not the check - what it said is. Differences are printed as +/- lines so a
# failure names the extension or the test that changed rather than dumping two
# lists to compare by eye.
#
# cmake -DTOOL=... -DTOOL_ARGS=... -DEXPECTED_FILE=... -P CompareOutput.cmake

# TOOL, not COMMAND: COMMAND is a keyword to if(), so `if(NOT COMMAND ...)`
# asks whether a *CMake command* by that name exists.
if(NOT DEFINED TOOL OR NOT DEFINED EXPECTED_FILE)
    message(FATAL_ERROR "CompareOutput.cmake needs -DTOOL and -DEXPECTED_FILE")
endif()

separate_arguments(_args UNIX_COMMAND "${TOOL_ARGS}")

execute_process(
    COMMAND "${TOOL}" ${_args}
    OUTPUT_VARIABLE _actual
    ERROR_VARIABLE _stderr
    RESULT_VARIABLE _result
)

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "${TOOL} ${TOOL_ARGS} failed (${_result}):\n${_stderr}")
endif()

file(READ "${EXPECTED_FILE}" _expected)

# Normalize line endings and trailing blank lines; neither is the point.
string(REPLACE "\r\n" "\n" _actual "${_actual}")
string(REPLACE "\r\n" "\n" _expected "${_expected}")
string(STRIP "${_actual}" _actual)
string(STRIP "${_expected}" _expected)

if(_actual STREQUAL _expected)
    return()
endif()

string(REPLACE "\n" ";" _actualLines "${_actual}")
string(REPLACE "\n" ";" _expectedLines "${_expected}")

set(_report "")
foreach(_line IN LISTS _expectedLines)
    if(NOT "${_line}" IN_LIST _actualLines)
        string(APPEND _report "  - ${_line}\n")
    endif()
endforeach()
foreach(_line IN LISTS _actualLines)
    if(NOT "${_line}" IN_LIST _expectedLines)
        string(APPEND _report "  + ${_line}\n")
    endif()
endforeach()

message(FATAL_ERROR
    "${COMMAND} ${ARGS} no longer matches ${EXPECTED}\n"
    "  (- expected but missing, + reported but not expected)\n"
    "${_report}"
    "If the change is intended, update the recorded file and say so in "
    "SUPPORTED_FEATURES.md.")
