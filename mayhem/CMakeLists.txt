# Mayhem wrapper build: only the engine-independent libraries (Colobot-Common + CBot) and, with
# MAYHEM_TESTS=ON, upstream's gtest unit tests for them. The top-level CMakeLists.txt hard-requires
# SDL2/GL/OpenAL/PhysFS for the game, none of which CBot or Colobot-Common use.
cmake_minimum_required(VERSION 3.21)
project(colobot_mayhem CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CBOT_STATIC ON CACHE BOOL "" FORCE)

get_filename_component(COLOBOT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
add_subdirectory("${COLOBOT_ROOT}/colobot-common" colobot-common)
add_subdirectory("${COLOBOT_ROOT}/CBot" CBot)

option(MAYHEM_TESTS "Build upstream's CBot/stringutils unit tests" OFF)
if(MAYHEM_TESTS)
    find_package(GTest REQUIRED)
    add_executable(cbot-unit-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/harnesses/unit_tests_main.cpp"
        "${COLOBOT_ROOT}/test/src/CBot/CBot_test.cpp"
        "${COLOBOT_ROOT}/test/src/CBot/CBotFileUtils_test.cpp"
        "${COLOBOT_ROOT}/test/src/CBot/CBotToken_test.cpp"
        "${COLOBOT_ROOT}/test/src/common/stringutils_test.cpp"
    )
    target_include_directories(cbot-unit-tests PRIVATE "${COLOBOT_ROOT}/CBot/src")
    target_link_libraries(cbot-unit-tests PRIVATE GTest::gtest CBot Colobot-Common)
endif()
