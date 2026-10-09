# Sanitizer configuration.
# Linux/macOS: ASan/UBSan via the usual -fsanitize flags.
# Windows/MSVC: ASan is supported by recent MSVC via /fsanitize=address; UBSan is not.
# Coverage intentionally differs per platform (master prompt §36).

function(openmoonray_anari_enable_sanitizers target)
    if(NOT (OPENMOONRAY_ANARI_ENABLE_ASAN OR OPENMOONRAY_ANARI_ENABLE_UBSAN))
        return()
    endif()

    if(MSVC)
        if(OPENMOONRAY_ANARI_ENABLE_ASAN)
            target_compile_options(${target} PRIVATE /fsanitize=address)
        endif()
        if(OPENMOONRAY_ANARI_ENABLE_UBSAN)
            message(WARNING "UBSan is not available with MSVC; ignoring for target ${target}")
        endif()
        return()
    endif()

    set(_san "")
    if(OPENMOONRAY_ANARI_ENABLE_ASAN)
        list(APPEND _san address)
    endif()
    if(OPENMOONRAY_ANARI_ENABLE_UBSAN)
        list(APPEND _san undefined)
    endif()
    list(JOIN _san "," _san_joined)
    target_compile_options(${target} PRIVATE -fsanitize=${_san_joined} -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=${_san_joined})
endfunction()
