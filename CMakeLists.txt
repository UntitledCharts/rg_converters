cmake_minimum_required(VERSION 3.15...3.30)
project(rg_converters LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

file(GLOB_RECURSE RG_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cpp/src/*.cpp")

pybind11_add_module(_rg_converters ${RG_SRCS})
target_include_directories(_rg_converters PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp/src/bindings")

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
target_link_libraries(_rg_converters PRIVATE zlibstatic)
target_include_directories(_rg_converters PRIVATE
  "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")

install(TARGETS _rg_converters DESTINATION rg_converters)
