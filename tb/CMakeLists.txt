include(GNUInstallDirs)

add_executable(tb main.cpp)
target_link_libraries(tb PRIVATE termbench)

# Set the RPATH so that the executable can find the shared libraries
# when installed in a non-standard location
set_target_properties(tb PROPERTIES
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

install(TARGETS tb RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_custom_target(Xvfb-bench-run
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/Xvfb-bench-run.sh $<TARGET_FILE:tb>
    VERBATIM
    USES_TERMINAL
    DEPENDS tb
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
