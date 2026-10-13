# `Ripple::engine` from an unpacked ripple-engine-c archive: the one place
# that knows the archive's layout, shared by the build tree
# (RippleEngine.cmake) and an installed package (RippleEffectConfig.cmake).
#
#   ripple_effect_import_engine(SHARED <bool> INCLUDE <dir> LIB <dir>
#                               [RUNTIME <dir>] [STATIC_LIBS <file>])
#
# INCLUDE holds ripple/ripple.h and LIB the libraries — an archive's
# `include/` and `lib/`. RUNTIME is where a Windows DLL is, LIB unless said.
# SHARED picks the shared library over the static one. A static link also
# needs the system libraries rustc named for the triple, which the archive
# carries as lib/native-static-libs.txt (STATIC_LIBS) and this turns into
# link items.

include_guard(GLOBAL)

# The system libraries a static link needs, as CMake link items.
function(_ripple_static_libs listed out)
    set(items)
    if(listed AND EXISTS "${listed}")
        file(READ "${listed}" said)
        string(STRIP "${said}" said)
        separate_arguments(tokens UNIX_COMMAND "${said}")
        set(framework FALSE)
        foreach(token IN LISTS tokens)
            if(framework)
                list(APPEND items "-framework ${token}")
                set(framework FALSE)
            elseif(token STREQUAL "-framework")
                set(framework TRUE)
            elseif(token MATCHES "^-l(.+)$")
                list(APPEND items "${CMAKE_MATCH_1}")
            elseif(token MATCHES "\\.lib$")
                string(REGEX REPLACE "\\.lib$" "" name "${token}")
                list(APPEND items "${name}")
            elseif(token MATCHES "^/")
                # An MSVC linker option rustc asks for, such as
                # /defaultlib:msvcrt; the CRT is the consumer's to pick.
            endif()
        endforeach()
        list(REMOVE_DUPLICATES items)
    endif()
    set(${out} "${items}" PARENT_SCOPE)
endfunction()

function(ripple_effect_import_engine)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "SHARED;INCLUDE;LIB;RUNTIME;STATIC_LIBS" "")
    if(TARGET Ripple::engine)
        return()
    endif()
    if(NOT arg_RUNTIME)
        set(arg_RUNTIME "${arg_LIB}")
    endif()
    if(arg_SHARED)
        add_library(ripple_effect_engine SHARED IMPORTED GLOBAL)
        if(WIN32)
            set_target_properties(
                ripple_effect_engine
                PROPERTIES IMPORTED_LOCATION "${arg_RUNTIME}/ripple_effect_engine.dll"
                           IMPORTED_IMPLIB "${arg_LIB}/ripple_effect_engine.dll.lib")
        elseif(APPLE)
            set_target_properties(
                ripple_effect_engine
                PROPERTIES IMPORTED_LOCATION "${arg_LIB}/libripple_effect_engine.dylib"
                           IMPORTED_SONAME "@rpath/libripple_effect_engine.dylib")
        else()
            set_target_properties(
                ripple_effect_engine
                PROPERTIES IMPORTED_LOCATION "${arg_LIB}/libripple_effect_engine.so"
                           IMPORTED_SONAME "libripple_effect_engine.so")
        endif()
    else()
        add_library(ripple_effect_engine STATIC IMPORTED GLOBAL)
        if(WIN32)
            set(location "${arg_LIB}/ripple_effect_engine.lib")
        else()
            set(location "${arg_LIB}/libripple_effect_engine.a")
        endif()
        _ripple_static_libs("${arg_STATIC_LIBS}" system)
        set_target_properties(ripple_effect_engine PROPERTIES IMPORTED_LOCATION "${location}"
                                                              INTERFACE_LINK_LIBRARIES "${system}")
    endif()
    get_target_property(location ripple_effect_engine IMPORTED_LOCATION)
    if(NOT EXISTS "${location}")
        message(FATAL_ERROR "Ripple Effect: ${location} is missing — there is no such library "
                            "for this target.")
    endif()
    set_target_properties(ripple_effect_engine PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                          "${arg_INCLUDE}")
    add_library(Ripple::engine ALIAS ripple_effect_engine)
endfunction()

# Copies the engine's DLL beside `target` after it links, on Windows, where a
# DLL is found beside the executable or not at all. Does nothing elsewhere,
# and nothing for a static link.
function(ripple_effect_copy_runtime target)
    if(WIN32)
        get_target_property(type ripple_effect_engine TYPE)
        if(type STREQUAL "SHARED_LIBRARY")
            add_custom_command(
                TARGET ${target}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Ripple::engine>
                        $<TARGET_FILE_DIR:${target}>
                VERBATIM)
        endif()
    endif()
endfunction()
