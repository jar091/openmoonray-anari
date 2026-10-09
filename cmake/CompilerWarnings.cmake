# Warning configuration applied to project-owned targets only.
# Third-party code (OpenMoonRay, ANARI-SDK) must never inherit these flags.

function(openmoonray_anari_set_warnings target)
    if(MSVC)
        set(_warnings /W4 /permissive-)
        if(OPENMOONRAY_ANARI_WARNINGS_AS_ERRORS)
            list(APPEND _warnings /WX)
        endif()
    else()
        set(_warnings -Wall -Wextra -Wpedantic)
        if(OPENMOONRAY_ANARI_WARNINGS_AS_ERRORS)
            list(APPEND _warnings -Werror)
        endif()
    endif()
    target_compile_options(${target} PRIVATE ${_warnings})
endfunction()
