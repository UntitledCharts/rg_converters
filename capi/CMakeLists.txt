option(RG_BUILD_CAPI_TESTS "Build the C library test (run with ctest)" ON)
set(RG_CAPI_TEST_ARGS "" CACHE STRING
  "Extra rg_capi_test arguments, e.g. \"path/to/yume.sus;1257\" (a yume_sus chart and its combo)")

# The core without the Python bindings (cpp/src/bindings).
file(GLOB_RECURSE RG_CORE_SRCS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/cpp/src/*.cpp")
list(FILTER RG_CORE_SRCS EXCLUDE REGEX "/cpp/src/bindings/")

add_library(rg_converters SHARED ${RG_CORE_SRCS} src/rg_converters.cpp src/score_json.cpp)
target_include_directories(rg_converters
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  PRIVATE "${PROJECT_SOURCE_DIR}/cpp/include" "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
target_compile_definitions(rg_converters PRIVATE RG_CAPI_BUILD RG_VERSION="${RG_VERSION}")
target_link_libraries(rg_converters PRIVATE zlibstatic)
set_target_properties(rg_converters PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(WIN32)
  # rg_converters.dll with MinGW too, the name P/Invoke and FFI loaders look for.
  set_target_properties(rg_converters PROPERTIES PREFIX "")
endif()

# Only the rg_ functions are exported (not zlib's, nor the std templates the core instantiates, so
# they can't clash with a host's own), and MinGW builds need no runtime DLLs. MSVC: configure with
# -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded for a static CRT. Linux: build in a manylinux image,
# whose toolchain links the newer libstdc++ parts statically, for a library that runs anywhere.
if(MINGW)
  target_link_options(rg_converters PRIVATE -static $<$<CONFIG:Release>:-s>)
elseif(APPLE)
  target_link_options(rg_converters PRIVATE "LINKER:-exported_symbol,_rg_*")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(rg_converters PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/rg_converters.map" $<$<CONFIG:Release>:-s>)
  set_property(TARGET rg_converters APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/rg_converters.map")
endif()

install(TARGETS rg_converters
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)
install(FILES include/rg_converters.h DESTINATION include)

if(RG_BUILD_CAPI_TESTS)
  enable_language(C)
  add_executable(rg_capi_test tests/test_capi.c)
  set_target_properties(rg_capi_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  target_compile_definitions(rg_capi_test PRIVATE
    RG_EXPECTED_VERSION="${RG_VERSION}" $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
  target_link_libraries(rg_capi_test PRIVATE rg_converters)
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(rg_capi_test PRIVATE Threads::Threads)
  endif()
  add_test(NAME rg_capi COMMAND rg_capi_test "${PROJECT_SOURCE_DIR}/test_files" ${RG_CAPI_TEST_ARGS})
endif()
