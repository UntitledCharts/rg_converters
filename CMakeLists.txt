cmake_minimum_required(VERSION 3.15...3.30)
project(rg_converters LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# no C++20 modules here; skip CMake's per-file module dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# vendored static zlib is linked into a shared module, so everything needs -fPIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

file(GLOB_RECURSE RG_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cpp/src/*.cpp")

# THIN_LTO: clang links in parallel instead of one serial full-LTO pass.
pybind11_add_module(_rg_converters THIN_LTO ${RG_SRCS})
target_include_directories(_rg_converters PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp/src/bindings")

# rg_converters/version.py is the single source of the version (pyproject reads it too).
set(RG_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/rg_converters/version.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RG_VERSION_FILE}")
file(STRINGS "${RG_VERSION_FILE}" RG_VERSION REGEX "^__version__ = \"[^\"]+\"")
string(REGEX REPLACE "^__version__ = \"([^\"]+)\".*" "\\1" RG_VERSION "${RG_VERSION}")
if(NOT RG_VERSION)
  message(FATAL_ERROR "could not read __version__ from ${RG_VERSION_FILE}")
endif()
target_compile_definitions(_rg_converters PRIVATE RG_VERSION="${RG_VERSION}")

# zlib vendored, pinned to 1.3.1, static: no system zlib, identical gzip on every platform.
include(FetchContent)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  zlib
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG v1.3.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zlib)
# only zlibstatic is linked; don't also build the shared zlib.
set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_link_libraries(_rg_converters PRIVATE zlibstatic)
target_include_directories(_rg_converters PRIVATE
  "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")

install(TARGETS _rg_converters DESTINATION rg_converters)
