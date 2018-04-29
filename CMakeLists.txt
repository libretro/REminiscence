cmake_minimum_required(VERSION 3.10)
project(reminiscence)

set(CMAKE_CXX_STANDARD 11)
list(APPEND CMAKE_MODULE_PATH ${reminiscence_SOURCE_DIR}/cmake)

add_subdirectory(3rdparty/libmodplug)

find_package(SDL2 REQUIRED)

include_directories(src)
include_directories(${SDL2_INCLUDE_DIR})

set(BASE_SOURCES
        src/collision.cpp
        src/cutscene.cpp
        src/dynlib.cpp
        src/file.cpp
        src/fs.cpp
        src/game.cpp
        src/graphics.cpp
        src/menu.cpp
        src/mixer.cpp
        src/mod_player.cpp
        src/ogg_player.cpp
        src/piege.cpp
        src/resource.cpp
        src/resource_aba.cpp
        src/scaler.cpp
        src/screenshot.cpp
        src/seq_player.cpp
        src/sfx_player.cpp
        src/staticres.cpp
        src/unpack.cpp
        src/util.cpp
        src/video.cpp)

set(EXE_SOURCES
        src/systemstub_sdl.cpp
        src/main.cpp)

add_executable(reminiscence
        ${BASE_SOURCES} ${EXE_SOURCES})

target_link_libraries(reminiscence modplug ${SDL2_LIBRARY})
