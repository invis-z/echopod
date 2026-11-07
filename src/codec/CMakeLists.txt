set(RIPPL_CODEC_SOURCES
    alac.c
    flac.c
    flac_bits.c
    mp4.c
    ogg.c
)
add_library(rippl_codec STATIC ${RIPPL_CODEC_SOURCES})
if(CMAKE_CROSSCOMPILING)
    set_source_files_properties(mp4.c ogg.c PROPERTIES COMPILE_OPTIONS -Os)
    target_compile_definitions(rippl_codec PRIVATE RIPPL_RESIDENT_TABLES)
endif()
target_include_directories(rippl_codec PUBLIC include)
target_link_libraries(rippl_codec
    PUBLIC rippl_core
    PRIVATE rippl_options rippl_lto
)

if(CMAKE_CROSSCOMPILING)
    add_library(rippl_codec_plain STATIC ${RIPPL_CODEC_SOURCES})
    target_include_directories(rippl_codec_plain PUBLIC include)
    target_link_libraries(rippl_codec_plain
        PUBLIC rippl_core_plain
        PRIVATE rippl_options
    )
endif()
