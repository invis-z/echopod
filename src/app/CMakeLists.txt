add_library(rippl_app STATIC
    app.c
    card_files.c
    clock_store.c
    app_screens.c
    bluetooth_link.c
    bluetooth_store.c
    browse_screen.c
    cover_cache.c
    default_theme.c
    favorites_store.c
    diagnostics_values.c
    diagnostics_rows.c
    $<IF:$<BOOL:${RIPPL_RELEASE}>,diagnostics_menu_release.c,diagnostics_menu.c>
    draw_profile.c
    equalizer_files.c
    equalizer_store.c
    folders_store.c
    frame_overlay.c
    home_art.c
    library_rescan.c
    music_check.c
    library_store.c
    player.c
    settings_screens.c
    settings_store.c
    spoken_store.c
    update_record.c
    screens/bluetooth_screen.c
    screens/charge_screen.c
    screens/cover_flow_screen.c
    screens/date_time_screen.c
    screens/equalizer_screen.c
    screens/folders_screen.c
    screens/font_sample_screen.c
    screens/now_playing_screen.c
    screens/up_next_screen.c
    screens/stopwatch_screen.c
    screens/test_pattern_screen.c
)
target_include_directories(rippl_app PUBLIC include PRIVATE .)
if(CMAKE_CROSSCOMPILING)
    target_compile_definitions(rippl_app PRIVATE RIPPL_RESIDENT_TABLES)
endif()
target_link_libraries(rippl_app
    PUBLIC rippl_core rippl_gfx rippl_ui rippl_fonts rippl_library rippl_playback rippl_settings rippl_sound rippl_hal
    PRIVATE rippl_options rippl_compact rippl_lto
)
