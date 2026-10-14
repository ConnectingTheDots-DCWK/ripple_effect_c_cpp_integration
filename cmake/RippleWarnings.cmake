# The warnings and sanitizers this repository's own targets are built with —
# the tests and the examples, never a consumer's. The shape is codexx_dtdk's
# ConfigureForBuildType, trimmed to what a binding needs: warnings everywhere,
# errors on CI, and an address-and-undefined-behaviour build.
#
#   ripple_effect_warnings(<target>)
#
# RIPPLE_WARNINGS_AS_ERRORS makes every warning an error; RIPPLE_SANITIZE
# (`address` or `thread`) adds the sanitizer to the compile and the link.

include_guard(GLOBAL)

function(ripple_effect_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:preprocessor)
        if(RIPPLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE -Wall
                    -Wextra
                    -Wpedantic
                    -Wshadow
                    -Wformat=2
                    -Wconversion
                    -Wsign-conversion
                    $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor
                    -Woverloaded-virtual>)
        if(RIPPLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    if(RIPPLE_SANITIZE STREQUAL "address")
        if(MSVC)
            target_compile_options(${target} PRIVATE /fsanitize=address)
        else()
            target_compile_options(
                ${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
                                  -fno-sanitize-recover=all)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    elseif(RIPPLE_SANITIZE STREQUAL "thread")
        target_compile_options(${target} PRIVATE -fsanitize=thread)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    elseif(RIPPLE_SANITIZE)
        message(
            FATAL_ERROR "RIPPLE_SANITIZE is `address`, `thread` or empty, not `${RIPPLE_SANITIZE}`."
        )
    endif()
endfunction()
