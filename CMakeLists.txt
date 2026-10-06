cmake_minimum_required(VERSION 3.24)
project(Lumen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LUMEN_BUILD_TESTS "Build Lumen unit tests" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(Lumen)
add_subdirectory(Apps/LumenAgent)
add_subdirectory(Apps/LumenRenderDemo)

if(LUMEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
