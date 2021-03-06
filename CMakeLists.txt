cmake_minimum_required(VERSION 3.10)
project(reminiscence)

set(CMAKE_CXX_STANDARD 98)
list(APPEND CMAKE_MODULE_PATH ${reminiscence_SOURCE_DIR}/cmake)

# Allow the developer to select if Dynamic or Static libraries are built
option(BUILD_EXE "Build executable" ON)
option(BUILD_CORE "Build libretro core" ON)
option(USE_MODPLUG "Use modplug" ON)
set(TESTS_DATA_PATH "${CMAKE_SOURCE_DIR}/data" CACHE STRING "Path to data files for unit tests (./data)")

if (USE_MODPLUG)
    add_definitions(-DUSE_MODPLUG -DMODPLUG_BASIC_SUPPORT)
    include_directories(3rdparty/libmodplug/src)
endif ()

add_subdirectory(3rdparty/libco)
add_subdirectory(3rdparty/libmodplug)
add_subdirectory(3rdparty/libretro-common)

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

if (BUILD_EXE)
    set(EXE_SOURCES
        src/sdlmain.cpp)

    find_package(SDL2 REQUIRED)
    include_directories(${SDL2_INCLUDE_DIR})

    add_executable(reminiscence-sdl ${EXE_SOURCES})

    target_link_libraries(reminiscence-sdl reminiscence libretro-common modplug libco ${SDL2_LIBRARY})
endif ()

if (BUILD_CORE)
    set(CORE_SOURCES
        src/libretro.cpp)

    add_library(reminiscence_libretro SHARED ${CORE_SOURCES})
    set_target_properties(reminiscence_libretro PROPERTIES PREFIX "")
    target_link_libraries(reminiscence_libretro reminiscence libretro-common modplug libco)
endif ()

