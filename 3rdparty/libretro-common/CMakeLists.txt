project(libretro-common)

set(SOURCES
    compat/compat_strcasestr.c
    encodings/encoding_utf.c
    file/file_path.c
    string/stdstring.c
    )

add_library(libretro-common STATIC ${SOURCES})
target_include_directories(libretro-common PUBLIC include)