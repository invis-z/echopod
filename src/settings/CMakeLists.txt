add_library(rippl_settings STATIC settings.c rocker.c)
target_include_directories(rippl_settings PUBLIC include)
target_link_libraries(rippl_settings
    PUBLIC rippl_core rippl_playback
    PRIVATE rippl_options rippl_compact rippl_lto
)
