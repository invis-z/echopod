add_library(rippl_sound STATIC equalizer_file.c equalizer_presets.c equalizer_text.c)
target_include_directories(rippl_sound PUBLIC include)
if(CMAKE_CROSSCOMPILING)
    target_compile_definitions(rippl_sound PRIVATE RIPPL_RESIDENT_TABLES)
endif()
target_link_libraries(rippl_sound
    PUBLIC rippl_core rippl_dsp
    PRIVATE rippl_options rippl_compact rippl_lto
)
