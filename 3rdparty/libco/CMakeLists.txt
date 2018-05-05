project(libco)

IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SOURCES amd64.c)
ELSE()
    set(SOURCES libco.c)
ENDIF()

add_library(libco STATIC ${SOURCES})
target_include_directories(libco PUBLIC .)