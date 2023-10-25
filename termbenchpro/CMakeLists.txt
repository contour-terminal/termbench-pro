include(GNUInstallDirs)

add_executable(tbp main.cpp)
target_link_libraries(tbp PRIVATE termbench::termbench fmt::fmt-header-only)

# Set the RPATH so that the executable can find the shared libraries
# when installed in a non-standard location
set_target_properties(tbp PROPERTIES
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

install(TARGETS tbp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

