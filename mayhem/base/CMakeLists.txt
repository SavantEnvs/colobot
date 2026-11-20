# Mayhem build of the Colobot-Base fuzz targets: configures the whole upstream project (its own
# top-level CMakeLists, unchanged) and links the harnesses against its Colobot-Base library.
# Only the targets below and their dependencies are built (no game executable, no data, no tests).
cmake_minimum_required(VERSION 3.21)
# Upstream's generated version.h uses CMAKE_PROJECT_VERSION_*, i.e. the TOP-LEVEL project's version
# (this one); without a version it is empty on a re-configure and version.h does not compile.
# Take it from upstream's own project() call.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../CMakeLists.txt" _colobot_top)
string(REGEX MATCH "project\\(colobot[ \t\r\n]+VERSION[ \t\r\n]+([0-9.]+)" _m "${_colobot_top}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "cannot read colobot's project VERSION from its CMakeLists.txt")
endif()
project(colobot_mayhem_base VERSION ${CMAKE_MATCH_1} LANGUAGES CXX C)

set(CBOT_STATIC ON CACHE BOOL "" FORCE)
set(TESTS OFF CACHE BOOL "" FORCE)
set(TOOLS OFF CACHE BOOL "" FORCE)
set(DESKTOP OFF CACHE BOOL "" FORCE)
set(ASSERTS ON CACHE BOOL "" FORCE)   # keep upstream's assert()s live (no -DNDEBUG)

get_filename_component(COLOBOT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${COLOBOT_ROOT}" colobot)

# Upstream's directory-scoped definitions (TESTS=OFF) do not propagate to this parent directory;
# repeat the one its headers need.
add_definitions(-DTEST_VIRTUAL=)

set(MAYHEM_FUZZ_ENGINE "-fsanitize=fuzzer" CACHE STRING "libFuzzer link flag(s)")
set(MAYHEM_STANDALONE_OBJ "" CACHE FILEPATH "compiled standalone run-once driver (C object)")
separate_arguments(_engine UNIX_COMMAND "${MAYHEM_FUZZ_ENGINE}")
foreach(h model_input_fuzzer level_parser_fuzzer)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/../harnesses/${h}.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/../lsan_off.cc")
    add_executable(${h} ${_src})
    target_link_libraries(${h} PRIVATE Colobot-Base)
    target_link_options(${h} PRIVATE ${_engine})
    add_executable(${h}-standalone ${_src} "${MAYHEM_STANDALONE_OBJ}")
    target_link_libraries(${h}-standalone PRIVATE Colobot-Base)
endforeach()
