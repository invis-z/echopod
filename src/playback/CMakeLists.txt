add_library(rippl_playback STATIC
    bookmarks.c
    lyrics.c
    play_queue.c
    resume_state.c
    spoken.c
    up_next.c
)
target_include_directories(rippl_playback PUBLIC include)
target_link_libraries(rippl_playback
    PUBLIC rippl_core rippl_library rippl_hal
    PRIVATE rippl_options rippl_compact rippl_lto
)
