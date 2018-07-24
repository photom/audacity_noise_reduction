cmake_minimum_required(VERSION 3.5)

# The version number.
set (AudacityNoiseReduction_VERSION_MAJOR 2)
set (AudacityNoiseReduction_VERSION_MINOR 2)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra -Wno-unused-parameter -Wno-unused-variable -Wimplicit-fallthrough=1")

# Renaming.  Could just use the official name throughout.
set(top_dir ${CMAKE_SOURCE_DIR})

find_package(PkgConfig REQUIRED)
find_package(PythonLibs 3 REQUIRED)

find_library(SNDFILE_LIBRARY sndfile REQUIRE)
find_library(SOXR_LIBRARY soxr REQUIRE)
find_library(PYTHON_LIBRARY python3 REQUIRE)

pkg_check_modules(SNDFILE REQUIRED sndfile)
pkg_check_modules(SOXR REQUIRED soxr)
pkg_check_modules(PYTHON REQUIRED python3)

add_subdirectory("src")

add_subdirectory("test")
