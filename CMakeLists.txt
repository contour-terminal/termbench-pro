cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

project(termbench VERSION "0.2.0" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT WIN32 AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the build mode." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()

include(FetchContent)
FetchContent_Declare(
  glaze
  GIT_REPOSITORY https://github.com/stephenberry/glaze.git
  GIT_TAG v3.4.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glaze)

set(MASTER_PROJECT OFF)
if(${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
    set(MASTER_PROJECT ON)
endif()

# Unit tests (Catch2). Fetched BEFORE the pedantic warning flags below so the third-party
# framework is not compiled under our -Werror wall (mirrors how glaze is handled above).
option(TERMBENCH_TESTING "Build the termbench unit tests" ${MASTER_PROJECT})
if(TERMBENCH_TESTING)
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v3.7.1
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    # Treat Catch2 as a system dependency so its macro expansions (e.g. __COUNTER__) are not
    # judged by our -pedantic/-Werror wall in consuming test targets.
    foreach(_catch2_target Catch2 Catch2WithMain)
        if(TARGET ${_catch2_target})
            set_target_properties(${_catch2_target} PROPERTIES SYSTEM ON)
        endif()
    endforeach()
    enable_testing()
endif()

include(EnableCcache)
include(ClangTidy)
include(PedanticCompiler)

if(DEFINED MSVC)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

if((CMAKE_CXX_COMPILER_ID MATCHES "Clang") AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17))
    include(CheckCXXSymbolExists)
    if(cxx_std_20 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
        set(header version)
    else()
        set(header ciso646)
    endif()

    check_cxx_symbol_exists(_LIBCPP_VERSION ${header} LIBCPP)
    if(LIBCPP)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexperimental-library")
    endif()
endif()

add_subdirectory(libtermbench)
add_subdirectory(tb)
