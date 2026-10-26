# `cmake --install` lays out the engine and both headers under the prefix,
# with a package config, so that a project that does not vendor this one can
# `find_package(RippleEffect 1.6 REQUIRED)` and link `Ripple::cpp`.
#
# The engine is an imported library, which CMake cannot export, so the
# config re-imports it from where it was installed (RippleImport.cmake, the
# same function the build tree uses) rather than through install(EXPORT).
# The version is SameMinorVersion: MAJOR.MINOR is the engine's contract, and
# a host of 1.6 is not a host of 1.7.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_ripple_config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/RippleEffect")

install(FILES "${RIPPLE_ENGINE_ROOT}/include/ripple/ripple.h"
              "${PROJECT_SOURCE_DIR}/include/ripple/ripple.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ripple")
get_target_property(_ripple_location ripple_effect_engine IMPORTED_LOCATION)
if(WIN32 AND RIPPLE_ENGINE_SHARED)
    install(FILES "${_ripple_location}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    get_target_property(_ripple_implib ripple_effect_engine IMPORTED_IMPLIB)
    install(FILES "${_ripple_implib}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
else()
    install(FILES "${_ripple_location}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
if(NOT RIPPLE_ENGINE_SHARED)
    install(FILES "${RIPPLE_ENGINE_ROOT}/lib/native-static-libs.txt"
            DESTINATION "${_ripple_config_dir}")
endif()
foreach(_ripple_notice NOTICE UNVERIFIED)
    if(EXISTS "${RIPPLE_ENGINE_ROOT}/${_ripple_notice}")
        install(FILES "${RIPPLE_ENGINE_ROOT}/${_ripple_notice}"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/ripple_effect"
                RENAME "ENGINE-${_ripple_notice}")
    endif()
endforeach()

set(RIPPLE_CONFIG_SHARED ${RIPPLE_ENGINE_SHARED})
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/RippleEffectConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/RippleEffectConfig.cmake" INSTALL_DESTINATION "${_ripple_config_dir}"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_BINDIR)
write_basic_package_version_file("${PROJECT_BINARY_DIR}/RippleEffectConfigVersion.cmake"
                                 COMPATIBILITY SameMinorVersion)
install(FILES "${PROJECT_BINARY_DIR}/RippleEffectConfig.cmake"
              "${PROJECT_BINARY_DIR}/RippleEffectConfigVersion.cmake"
              "${PROJECT_SOURCE_DIR}/cmake/RippleImport.cmake" DESTINATION "${_ripple_config_dir}")
