include(GNUInstallDirs)

add_executable(tb main.cpp)
target_link_libraries(tb PRIVATE termbench)

# Multi-protocol image benchmark: an animated plasma rendered via Sixel / Kitty / iTerm2.
add_executable(image-bench image_bench.cpp image_protocol.cpp)
target_link_libraries(image-bench PRIVATE termbench ZLIB::ZLIB)

# Set the RPATH so that the executables can find the shared libraries
# when installed in a non-standard location
set_target_properties(tb image-bench PROPERTIES
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

install(TARGETS tb image-bench RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(TERMBENCH_TESTING)
    add_executable(image_bench_test image_bench_test.cpp image_protocol.cpp)
    target_link_libraries(image_bench_test PRIVATE termbench Catch2::Catch2WithMain ZLIB::ZLIB)
    # Catch2's TEST_CASE macro expands __COUNTER__, which recent Clang flags as a C2y extension
    # under -pedantic. It is a universally-supported builtin used only by the test framework, so
    # silence just this one diagnostic on the test target (Clang only; GCC does not flag it).
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(image_bench_test PRIVATE -Wno-c2y-extensions)
    endif()
    add_test(NAME image_bench_test COMMAND image_bench_test)
endif()

add_custom_target(Xvfb-bench-run
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/Xvfb-bench-run.sh $<TARGET_FILE:tb>
    VERBATIM
    USES_TERMINAL
    DEPENDS tb
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
