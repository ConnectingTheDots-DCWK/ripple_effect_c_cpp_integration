# The C and C++ bindings of the Ripple Effect engine.
#
# The engine is a released binary, fetched and verified at configure time
# (cmake/RippleEngine.cmake); this project builds nothing of it. What it adds
# is `Ripple::cpp`, a header-only C++17 layer over the engine's C ABI, and —
# when it is the top-level project — its own tests and examples. A consumer
# links one of:
#
#   Ripple::engine   the C library and include/ripple/ripple.h
#   Ripple::cpp      the same, and include/ripple/ripple.hpp over it
#
# as a subdirectory, through FetchContent, or after `cmake --install` with
# `find_package(RippleEffect 1.5)`. README.md has all three.
cmake_minimum_required(VERSION 3.24)

project(
    RippleEffect
    VERSION 1.5.0
    DESCRIPTION "C and C++ bindings of the Ripple Effect engine"
    HOMEPAGE_URL "https://github.com/ConnectingTheDots-DCWK/ripple_effect_c_cpp_integration"
    LANGUAGES C CXX)

option(RIPPLE_ENGINE_PYTHON "Link the engine built with the Python interpreter" ON)
option(RIPPLE_ENGINE_SHARED "Link the engine's shared library rather than its static one" ON)
set(RIPPLE_ENGINE_TARGET "" CACHE STRING
                                  "The engine's triple; empty works it out from the toolchain")
set(RIPPLE_ENGINE_DIR ""
    CACHE PATH "An unpacked ripple-engine-c archive, instead of downloading the pinned one")
set(RIPPLE_ENGINE_TREE "" CACHE PATH "A ripple_effect checkout to build an unreleased engine from")
set(RIPPLE_ENGINE_CACHE "" CACHE PATH
                                 "Where downloaded archives are kept; the build tree by default")
option(RIPPLE_BUILD_TESTS "Build this repository's tests" ${PROJECT_IS_TOP_LEVEL})
option(RIPPLE_BUILD_EXAMPLES "Build the example players" ${PROJECT_IS_TOP_LEVEL})
option(RIPPLE_INSTALL "Install the engine and the headers with a package config"
       ${PROJECT_IS_TOP_LEVEL})
option(RIPPLE_WARNINGS_AS_ERRORS "Treat this repository's own warnings as errors" OFF)
set(RIPPLE_SANITIZE "" CACHE STRING "address or thread, for this repository's own targets")

include(cmake/RippleEngine.cmake)

add_library(ripple_cpp INTERFACE)
add_library(Ripple::cpp ALIAS ripple_cpp)
target_include_directories(
    ripple_cpp INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                         $<INSTALL_INTERFACE:include>)
target_compile_features(ripple_cpp INTERFACE cxx_std_17)
target_link_libraries(ripple_cpp INTERFACE Ripple::engine)

if(RIPPLE_BUILD_TESTS OR RIPPLE_BUILD_EXAMPLES)
    include(cmake/RippleWarnings.cmake)
endif()
if(RIPPLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(RIPPLE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(RIPPLE_INSTALL)
    include(cmake/RippleInstall.cmake)
endif()

if(RIPPLE_ENGINE_SHARED)
    set(_ripple_linkage shared)
else()
    set(_ripple_linkage static)
endif()
message(STATUS "Ripple Effect ${PROJECT_VERSION}: engine ${RIPPLE_ENGINE_VERSION}, "
               "${RIPPLE_ENGINE_TRIPLE}, ${_ripple_variant}, ${_ripple_linkage}")
