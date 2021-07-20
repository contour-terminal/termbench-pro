cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

project(termbench-pro VERSION "${CONTOUR_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(EnableCcache)
include(ClangTidy)
include(PedanticCompiler)

if(DEFINED MSVC)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

add_subdirectory(libtermbench)
add_subdirectory(termbenchpro)
include(ThirdParties)
