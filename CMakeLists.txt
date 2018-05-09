cmake_minimum_required(VERSION 3.10)
project(reminiscence)

set(CMAKE_CXX_STANDARD 98)
list(APPEND CMAKE_MODULE_PATH ${reminiscence_SOURCE_DIR}/cmake)

# Allow the developer to select if Dynamic or Static libraries are built
option(BUILD_EXE "Build executable" ON)
option(BUILD_CORE "Build libretro core" ON)
option(USE_MODPLUG "Use modplug" ON)
option(BUILD_TESTS "Build all unit tests." ON)
set(TESTS_DATA_PATH "${CMAKE_SOURCE_DIR}/data" CACHE STRING "Path to data files for unit tests (./data)")

if (USE_MODPLUG)
    add_definitions(-DUSE_MODPLUG)
    include_directories(3rdparty/libmodplug/src)
endif ()

add_subdirectory(3rdparty/libco)
add_subdirectory(3rdparty/libmodplug)

include_directories(src)
include_directories(3rdparty/libco)
include_directories(3rdparty/libretro-common/include)

set(LIB_SOURCES
    src/intern.h
    src/collision.cpp
    src/cutscene.cpp
    src/file.cpp
    src/fs.cpp
    src/game.cpp
    src/graphics.cpp
    src/menu.cpp
    src/mixer.cpp
    src/mod_player.cpp
    src/piege.cpp
    src/resource.cpp
    src/resource_aba.cpp
    src/seq_player.cpp
    src/sfx_player.cpp
    src/staticres.cpp
    src/unpack.c
    src/util.c
    src/video.cpp)

add_library(reminiscence ${LIB_SOURCES})

IF (BUILD_TESTS)
    IF (APPLE)
        add_definitions(-DGTEST_USE_OWN_TR1_TUPLE)
        add_definitions(-D__GLIBCXX__)
    ENDIF ()
    add_subdirectory(3rdparty/googletest)
    enable_testing()

    add_definitions(-DDATA_PATH="${TESTS_DATA_PATH}")

    ##############
    # Unit Tests
    ##############
    add_executable(runUnitTests tests/GameTests.cpp)

    # Standard linking to gtest stuff.
    target_link_libraries(runUnitTests reminiscence modplug gtest gtest_main libco)

    # This is so you can do 'make test' to see all your tests run, instead of
    # manually running the executable runUnitTests to see those specific tests.
    add_test(Game-One runUnitTests)

ENDIF ()

if (BUILD_EXE)
    set(EXE_SOURCES
        src/sdlmain.cpp)

    find_package(SDL2 REQUIRED)
    include_directories(${SDL2_INCLUDE_DIR})

    add_executable(reminiscence-sdl ${EXE_SOURCES})

    target_link_libraries(reminiscence-sdl reminiscence modplug libco ${SDL2_LIBRARY})
endif ()

if (BUILD_CORE)
    set(CORE_SOURCES
        src/libretro.cpp)

    add_library(reminiscence_libretro SHARED ${CORE_SOURCES})
    set_target_properties(reminiscence_libretro PROPERTIES PREFIX "")
    target_link_libraries(reminiscence_libretro reminiscence modplug libco)
endif ()

