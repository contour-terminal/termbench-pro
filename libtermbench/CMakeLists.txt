add_library(termbench
    termbench.cpp termbench.h
)

target_link_libraries(termbench PUBLIC fmt::fmt-header-only)

target_compile_features(termbench PUBLIC cxx_std_17)
target_include_directories(termbench PUBLIC
    ${CMAKE_SOURCE_DIR}
    $<BUILD_INTERFACE:${${PROJECT_NAME}_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/include>
)
