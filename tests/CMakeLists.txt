# This repository's own tests. Nothing here is fetched unless they are built:
# a consumer configuring this project as a subdirectory gets RIPPLE_BUILD_TESTS
# off, and downloads neither GoogleTest nor nlohmann/json.
#
#   conformance   every fixture of the pinned release, through Session and Player
#   c99           a program in C99 alone, playing a fixture to its end
#   driver        the soundscape driver, against a speaker that writes down
#   values        a value through the arena and back

include(FetchContent)
# Emscripten catches no exception unless asked, and these tests expect the
# throwing ripple.hpp and GoogleTest's own. Every object in one link has to
# agree on how, so the whole directory says so, GoogleTest included — the
# tests' choice, not a host's: the engine itself asks for none. setjmp has to
# be the wasm one alongside: Emscripten 4.0.10 refuses wasm exceptions with
# its default JavaScript setjmp, where later ones switch it themselves.
if(EMSCRIPTEN)
    add_compile_options(-fwasm-exceptions -sSUPPORT_LONGJMP=wasm)
    add_link_options(-fwasm-exceptions -sSUPPORT_LONGJMP=wasm)
endif()
# Pinned by digest, the way the engine is: a test dependency is code that runs
# on every machine that builds this, CI's included.
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest nlohmann_json)
# Its headers are a dependency's, so this repository's warnings stop at them:
# on a 32-bit target (Emscripten) its serializer converts an `unsigned` to a
# `long` that -Wsign-conversion reports. FetchContent's SYSTEM is 3.25's, one
# past the floor, so the include directories are marked by hand.
get_target_property(_ripple_json_includes nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(nlohmann_json PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
                                               "${_ripple_json_includes}")
include(GoogleTest)

ripple_engine_fixtures(RIPPLE_FIXTURES)
message(STATUS "Ripple Effect: conformance fixtures in ${RIPPLE_FIXTURES}")

add_executable(ripple_tests conformance/conformance_test.cpp driver/driver_test.cpp values_test.cpp)
target_link_libraries(ripple_tests PRIVATE Ripple::cpp GTest::gtest_main
                                           nlohmann_json::nlohmann_json)
target_compile_definitions(ripple_tests PRIVATE RIPPLE_FIXTURES="${RIPPLE_FIXTURES}")
ripple_effect_warnings(ripple_tests)
ripple_effect_copy_runtime(ripple_tests)
# Discovery runs the test binary once to enumerate its cases; under the iOS
# simulator's crosscompiling emulator the first `simctl spawn` pays Core
# Simulator's cold start, which on a slow runner overran the old 60s ceiling.
gtest_discover_tests(ripple_tests DISCOVERY_TIMEOUT 300 DISCOVERY_MODE PRE_TEST)

# The C header alone, in the oldest C it promises, with every warning an
# error: proof that a host with no C++ at all can play a story.
add_executable(ripple_c99 c99/smoke.c)
set_target_properties(ripple_c99 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(ripple_c99 PRIVATE Ripple::engine)
ripple_effect_warnings(ripple_c99)
if(NOT MSVC)
    target_compile_options(ripple_c99 PRIVATE -Werror)
endif()
ripple_effect_copy_runtime(ripple_c99)
add_test(NAME c99_plays_a_fixture COMMAND ripple_c99 "${RIPPLE_FIXTURES}/the-choir/bundle")

# ripple.hpp with exceptions off, as a game built with -fno-exceptions has it.
add_executable(ripple_no_exceptions no_exceptions.cpp)
target_link_libraries(ripple_no_exceptions PRIVATE Ripple::cpp)
target_compile_definitions(ripple_no_exceptions PRIVATE RIPPLE_HPP_NO_EXCEPTIONS)
if(MSVC)
    target_compile_options(ripple_no_exceptions PRIVATE /EHs-c- /D_HAS_EXCEPTIONS=0)
else()
    target_compile_options(ripple_no_exceptions PRIVATE -fno-exceptions)
endif()
ripple_effect_warnings(ripple_no_exceptions)
ripple_effect_copy_runtime(ripple_no_exceptions)
add_test(NAME no_exceptions_plays_a_fixture COMMAND ripple_no_exceptions
                                                    "${RIPPLE_FIXTURES}/the-choir/bundle")
