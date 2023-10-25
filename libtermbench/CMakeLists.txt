include(GNUInstallDirs)

set(_TERMBENCH_LIB_BUILD_MODE)
if(MASTER_PROJECT)
    set(_TERMBENCH_LIB_BUILD_MODE "SHARED")
else()
    set(_TERMBENCH_LIB_BUILD_MODE "STATIC")
endif()

set(TERMBENCH_LIB_BUILD_MODE ${_TERMBENCH_LIB_BUILD_MODE} CACHE STRING "Choose the build mode." FORCE)
set_property(CACHE TERMBENCH_LIB_BUILD_MODE PROPERTY STRINGS "STATIC" "SHARED")
message(STATUS "termbench library build mode: ${TERMBENCH_LIB_BUILD_MODE}")

set(_PUBLIC_HEADERS termbench.h)
add_library(termbench ${TERMBENCH_LIB_BUILD_MODE} termbench.cpp)
add_library(termbench::termbench ALIAS termbench)
target_compile_features(termbench PUBLIC cxx_std_20)
set_target_properties(termbench PROPERTIES
    PUBLIC_HEADER "${_PUBLIC_HEADERS}"
)
set_target_properties(termbench PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
)
target_link_libraries(termbench PUBLIC fmt::fmt-header-only)

target_include_directories(termbench PUBLIC $<BUILD_INTERFACE:${${PROJECT_NAME}_SOURCE_DIR}>
                                            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

install(TARGETS termbench
        EXPORT termbench-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/termbench"
        PRIVATE_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/termbench"
        FRAMEWORK DESTINATION "."
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# {{{ Generate the version, config and target files
include(CMakePackageConfigHelpers)

write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/termbench-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(termbench-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termbench-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termbench
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/termbench-config.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/termbench-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termbench
)

install(EXPORT termbench-targets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termbench
    NAMESPACE termbench::
)
# }}}
