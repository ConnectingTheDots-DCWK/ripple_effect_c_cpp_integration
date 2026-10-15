# Where the engine comes from, and the targets it becomes.
#
# The engine is not built here: it is released from the private ripple_effect
# repository as `engine-v<version>` in the public ripple_engine_releases, and
# engine.json pins one release — its version, its signing key and the digest
# of every archive. At configure time this file picks the archive for the
# target being built, downloads it, refuses it unless its SHA-256 is the
# pinned one, and unpacks it into the build tree. Three ways to get it:
#
#   (default)                  the pinned release, verified by digest
#   -DRIPPLE_ENGINE_DIR=<dir>  an archive already unpacked: offline, vendored,
#                              or a target this file cannot name
#   -DRIPPLE_ENGINE_TREE=<dir> a ripple_effect checkout, built with cargo:
#                              an unreleased engine, marked UNVERIFIED
#
# The pin is the trust anchor here. CMake cannot check an Ed25519 signature,
# so the manifest's is checked when the pin is written (scripts/pin-engine.sh)
# and again in CI, and a digest in a committed file is what a configure trusts.
#
# What it defines:
#
#   Ripple::engine             the C library, shared or static, with ripple.h
#   RIPPLE_ENGINE_ROOT         where the archive was unpacked
#   RIPPLE_ENGINE_UNVERIFIED   TRUE when it came from RIPPLE_ENGINE_TREE
#   ripple_engine_fixtures(<var>)  the conformance fixtures, for the tests
#   ripple_effect_copy_runtime(<target>)  the DLL beside a Windows executable

include_guard(GLOBAL)

set(_RIPPLE_ENGINE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_RIPPLE_PIN "${CMAKE_CURRENT_LIST_DIR}/../engine.json")

# ---------------------------------------------------------------- the target

# The engine's name for the target being built — a rustc triple. A Windows
# build must be MSVC's ABI, and macOS one architecture at a time: the release
# ships no universal library, and two configures make one with `lipo`.
function(_ripple_triple out)
    if(RIPPLE_ENGINE_TARGET)
        set(${out} "${RIPPLE_ENGINE_TARGET}" PARENT_SCOPE)
        return()
    endif()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" cpu)
    if(cpu MATCHES "^(x86_64|amd64|x64)$")
        set(arch x86_64)
    elseif(cpu MATCHES "^(aarch64|arm64|armv8.*)$")
        set(arch aarch64)
    endif()
    if(ANDROID)
        if(ANDROID_ABI STREQUAL "arm64-v8a")
            set(triple aarch64-linux-android)
        elseif(ANDROID_ABI STREQUAL "x86_64")
            set(triple x86_64-linux-android)
        else()
            message(FATAL_ERROR "Ripple Effect: the engine ships for arm64-v8a and x86_64 on "
                                "Android, not ${ANDROID_ABI}.")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            set(triple aarch64-apple-ios-sim)
        else()
            set(triple aarch64-apple-ios)
        endif()
    elseif(APPLE)
        list(LENGTH CMAKE_OSX_ARCHITECTURES archs)
        if(archs GREATER 1)
            message(FATAL_ERROR "Ripple Effect: one architecture per configure on macOS — the "
                                "engine ships no universal library. Configure twice and `lipo`.")
        elseif(archs EQUAL 1)
            string(REPLACE arm64 aarch64 arch "${CMAKE_OSX_ARCHITECTURES}")
        endif()
        set(triple ${arch}-apple-darwin)
    elseif(WIN32)
        if(NOT MSVC)
            message(FATAL_ERROR "Ripple Effect: the Windows engine is built for MSVC's ABI; "
                                "MinGW cannot link it.")
        endif()
        if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$" OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID
                                                                  MATCHES "ARM64")
            set(arch aarch64)
        endif()
        set(triple ${arch}-pc-windows-msvc)
    elseif(EMSCRIPTEN)
        # A page built with Emscripten 4.0 or later, single-threaded: the
        # archive is a static library and nothing else (docs/consuming.md).
        set(arch wasm32)
        set(triple wasm32-unknown-emscripten)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(triple ${arch}-unknown-linux-gnu)
    endif()
    if(NOT triple OR NOT arch)
        message(
            FATAL_ERROR
                "Ripple Effect: no engine for ${CMAKE_SYSTEM_NAME} on "
                "${CMAKE_SYSTEM_PROCESSOR}. Set RIPPLE_ENGINE_TARGET to a triple the "
                "release ships, or RIPPLE_ENGINE_DIR to an unpacked archive.")
    endif()
    set(${out} "${triple}" PARENT_SCOPE)
endfunction()

_ripple_triple(RIPPLE_ENGINE_TRIPLE)
if(RIPPLE_ENGINE_PYTHON)
    set(_ripple_variant python)
else()
    set(_ripple_variant no-python)
endif()

if(RIPPLE_ENGINE_TRIPLE MATCHES "apple-ios|emscripten")
    # iOS and Emscripten link statically or not at all: their archives carry
    # no shared library.
    set(RIPPLE_ENGINE_SHARED OFF)
endif()

file(READ "${_RIPPLE_PIN}" _ripple_pin)
string(JSON RIPPLE_ENGINE_VERSION GET "${_ripple_pin}" engine)

if(NOT RIPPLE_ENGINE_CACHE)
    set(RIPPLE_ENGINE_CACHE "${CMAKE_BINARY_DIR}/_ripple_engine")
endif()

# ------------------------------------------------------- the pinned release

# Downloads `file` from the first of the pin's URLs that answers, refusing it
# unless its SHA-256 is `sha256`, into `dest`.
function(_ripple_download file sha256 dest)
    if(EXISTS "${dest}")
        file(SHA256 "${dest}" have)
        if(have STREQUAL sha256)
            return()
        endif()
    endif()
    string(JSON count LENGTH "${_ripple_pin}" urls)
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
        string(JSON template GET "${_ripple_pin}" urls ${i})
        string(REPLACE "{tag}" "engine-v${RIPPLE_ENGINE_VERSION}" url "${template}")
        string(REPLACE "{file}" "${file}" url "${url}")
        message(STATUS "Ripple Effect: downloading ${url}")
        # The digest is checked here rather than by EXPECTED_HASH, which turns a
        # failed download into a configure error even when the next URL would
        # have answered.
        file(DOWNLOAD "${url}" "${dest}.part" TLS_VERIFY ON STATUS status)
        list(GET status 0 code)
        if(code EQUAL 0)
            file(SHA256 "${dest}.part" have)
            if(have STREQUAL sha256)
                file(RENAME "${dest}.part" "${dest}")
                return()
            endif()
            message(STATUS "Ripple Effect: ${url} is not the pinned archive (sha256 ${have})")
        else()
            list(GET status 1 why)
            message(STATUS "Ripple Effect: ${url}: ${why}")
        endif()
        file(REMOVE "${dest}.part")
    endforeach()
    message(
        FATAL_ERROR
            "Ripple Effect: could not download ${file} with the pinned digest from "
            "any of the pin's URLs. Offline? Unpack it yourself and set " "RIPPLE_ENGINE_DIR.")
endfunction()

# The pinned archive for `key` (a variant's triple, or the fixtures), as
# `file;sha256`, or a refusal when the pin names none.
function(_ripple_pinned out)
    string(
        JSON
        entry
        ERROR_VARIABLE
        missing
        GET
        "${_ripple_pin}"
        archives
        ${ARGN})
    if(missing)
        message(FATAL_ERROR "Ripple Effect: engine.json pins no archive for ${ARGN}.")
    endif()
    string(JSON file GET "${entry}" file)
    string(JSON sha256 GET "${entry}" sha256)
    if(sha256 STREQUAL "" OR sha256 STREQUAL "NOTFOUND")
        message(
            FATAL_ERROR
                "Ripple Effect: engine.json pins no release yet (its digests are "
                "null). Configure with RIPPLE_ENGINE_TREE=<a ripple_effect checkout> "
                "to build one, or RIPPLE_ENGINE_DIR=<an unpacked archive>.")
    endif()
    set(${out} "${file};${sha256}" PARENT_SCOPE)
endfunction()

# Unpacks a downloaded archive once, keyed by its digest.
function(_ripple_unpack archive sha256 into)
    if(EXISTS "${into}/.sha256")
        file(READ "${into}/.sha256" had)
        if(had STREQUAL sha256)
            return()
        endif()
    endif()
    file(REMOVE_RECURSE "${into}")
    file(MAKE_DIRECTORY "${into}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${into}")
    file(WRITE "${into}/.sha256" "${sha256}")
endfunction()

# -------------------------------------------------------- a development tree

# The cargo invocation that builds a variant into the tree, and the directory
# it builds into: `target/` with the interpreter, `target/no-python/` without,
# as the engine's own release does.
function(_ripple_cargo_args out_args out_dir)
    set(args --release -p ripple-capi --lib --target ${RIPPLE_ENGINE_TRIPLE})
    set(dir "${RIPPLE_ENGINE_TREE}/target")
    if(NOT RIPPLE_ENGINE_PYTHON)
        set(dir "${RIPPLE_ENGINE_TREE}/target/no-python")
        list(APPEND args --target-dir "${dir}" --no-default-features)
    endif()
    set(${out_args} "${args}" PARENT_SCOPE)
    set(${out_dir} "${dir}/${RIPPLE_ENGINE_TRIPLE}/release" PARENT_SCOPE)
endfunction()

# Builds the C ABI in a ripple_effect checkout and lays it out as an archive
# would be, so everything below reads one layout.
function(_ripple_from_tree into)
    find_program(_ripple_cargo cargo HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)
    _ripple_cargo_args(args out)
    message(
        STATUS "Ripple Effect: building the engine in ${RIPPLE_ENGINE_TREE} (cargo build ${args})")
    execute_process(COMMAND "${_ripple_cargo}" build --quiet ${args}
                    WORKING_DIRECTORY "${RIPPLE_ENGINE_TREE}" RESULT_VARIABLE built)
    if(NOT built EQUAL 0)
        message(
            FATAL_ERROR "Ripple Effect: cargo could not build the engine in ${RIPPLE_ENGINE_TREE}.")
    endif()
    # rustc names a static link's system libraries only while it writes the
    # staticlib; the extra flag makes cargo run it again to say so.
    execute_process(
        COMMAND "${_ripple_cargo}" rustc --quiet ${args} --crate-type staticlib -- --print
                native-static-libs
        WORKING_DIRECTORY "${RIPPLE_ENGINE_TREE}"
        ERROR_VARIABLE said
        OUTPUT_VARIABLE said_out)
    string(REGEX MATCH "native-static-libs: ([^\r\n]*)" _ "${said}${said_out}")
    file(REMOVE_RECURSE "${into}")
    file(MAKE_DIRECTORY "${into}/lib" "${into}/include/ripple")
    file(COPY "${RIPPLE_ENGINE_TREE}/crates/ripple-capi/include/ripple/ripple.h"
         DESTINATION "${into}/include/ripple")
    foreach(lib libripple_effect_engine.so libripple_effect_engine.dylib libripple_effect_engine.a
                ripple_effect_engine.dll ripple_effect_engine.dll.lib ripple_effect_engine.lib)
        if(EXISTS "${out}/${lib}")
            file(COPY "${out}/${lib}" DESTINATION "${into}/lib")
        endif()
    endforeach()
    file(WRITE "${into}/lib/native-static-libs.txt" "${CMAKE_MATCH_1}\n")
    file(WRITE "${into}/UNVERIFIED" "built from ${RIPPLE_ENGINE_TREE}, not a release\n")
endfunction()

# ------------------------------------------------------------ choose a source

set(RIPPLE_ENGINE_UNVERIFIED FALSE)
if(RIPPLE_ENGINE_DIR)
    set(RIPPLE_ENGINE_ROOT "${RIPPLE_ENGINE_DIR}")
elseif(RIPPLE_ENGINE_TREE)
    set(RIPPLE_ENGINE_ROOT "${RIPPLE_ENGINE_CACHE}/tree/${_ripple_variant}/${RIPPLE_ENGINE_TRIPLE}")
    _ripple_from_tree("${RIPPLE_ENGINE_ROOT}")
    set(RIPPLE_ENGINE_UNVERIFIED TRUE)
    message(WARNING "Ripple Effect: the engine was built from ${RIPPLE_ENGINE_TREE} and is "
                    "UNVERIFIED — not a release. Never ship this build.")
else()
    _ripple_pinned(pinned ${_ripple_variant} ${RIPPLE_ENGINE_TRIPLE})
    list(GET pinned 0 file)
    list(GET pinned 1 sha256)
    set(archive "${RIPPLE_ENGINE_CACHE}/downloads/${file}")
    _ripple_download("${file}" "${sha256}" "${archive}")
    set(RIPPLE_ENGINE_ROOT
        "${RIPPLE_ENGINE_CACHE}/${RIPPLE_ENGINE_VERSION}/${_ripple_variant}/${RIPPLE_ENGINE_TRIPLE}"
    )
    _ripple_unpack("${archive}" "${sha256}" "${RIPPLE_ENGINE_ROOT}")
endif()

if(NOT EXISTS "${RIPPLE_ENGINE_ROOT}/include/ripple/ripple.h")
    message(FATAL_ERROR "Ripple Effect: ${RIPPLE_ENGINE_ROOT} holds no include/ripple/ripple.h — "
                        "not an unpacked ripple-engine-c archive.")
endif()

# ------------------------------------------------------------- the library

include("${_RIPPLE_ENGINE_CMAKE_DIR}/RippleImport.cmake")
ripple_effect_import_engine(
    SHARED
    ${RIPPLE_ENGINE_SHARED}
    INCLUDE
    "${RIPPLE_ENGINE_ROOT}/include"
    LIB
    "${RIPPLE_ENGINE_ROOT}/lib"
    STATIC_LIBS
    "${RIPPLE_ENGINE_ROOT}/lib/native-static-libs.txt")

# ----------------------------------------------------- the conformance suite

# The conformance fixtures of the same engine, for this repository's tests:
# the pinned archive, or the tree's own fixtures.
function(ripple_engine_fixtures out)
    if(RIPPLE_ENGINE_TREE)
        set(${out} "${RIPPLE_ENGINE_TREE}/packages/ripple_runtime/test/fixtures" PARENT_SCOPE)
        return()
    endif()
    if(RIPPLE_ENGINE_FIXTURES)
        set(${out} "${RIPPLE_ENGINE_FIXTURES}" PARENT_SCOPE)
        return()
    endif()
    _ripple_pinned(pinned conformance)
    list(GET pinned 0 file)
    list(GET pinned 1 sha256)
    set(archive "${RIPPLE_ENGINE_CACHE}/downloads/${file}")
    _ripple_download("${file}" "${sha256}" "${archive}")
    set(into "${RIPPLE_ENGINE_CACHE}/${RIPPLE_ENGINE_VERSION}/conformance")
    _ripple_unpack("${archive}" "${sha256}" "${into}")
    set(${out} "${into}/fixtures" PARENT_SCOPE)
endfunction()
