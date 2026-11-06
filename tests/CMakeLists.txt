add_library(rippl_check STATIC support/check.c)
target_include_directories(rippl_check PUBLIC support)
target_link_libraries(rippl_check PRIVATE rippl_options)

function(rippl_add_unit_test)
    cmake_parse_arguments(TEST "" "NAME" "SOURCES;LIBRARIES;PRIVATE_INCLUDES" ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    if(WIN32)
        target_sources(${TEST_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/cmake/windows_utf8.rc)
    endif()
    target_link_libraries(${TEST_NAME} PRIVATE rippl_check rippl_options ${TEST_LIBRARIES})
    target_include_directories(${TEST_NAME} PRIVATE ${TEST_PRIVATE_INCLUDES})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

rippl_add_unit_test(NAME test_deadline SOURCES core/test_deadline.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_frame_stats SOURCES core/test_frame_stats.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_key_tracker SOURCES core/test_key_tracker.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_utf8 SOURCES core/test_utf8.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_volume_path SOURCES core/test_volume_path.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_i18n SOURCES core/test_i18n.c LIBRARIES rippl_core)
string(REPLACE ";" "|" RIPPL_LANGUAGE_PACK_PATHS "${RIPPL_LANGUAGE_PACK_FILES}")
rippl_add_unit_test(NAME test_language_packs SOURCES core/test_language_packs.c LIBRARIES rippl_core rippl_settings)
target_compile_definitions(test_language_packs PRIVATE RIPPL_LANGUAGE_PACK_PATHS="${RIPPL_LANGUAGE_PACK_PATHS}")
rippl_add_unit_test(NAME test_string_builder SOURCES core/test_string_builder.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_format SOURCES core/test_format.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_random SOURCES core/test_random.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_permutation SOURCES core/test_permutation.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_hash SOURCES core/test_hash.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_power_policy SOURCES core/test_power_policy.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_low_battery SOURCES core/test_low_battery.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_wall_clock SOURCES core/test_wall_clock.c LIBRARIES rippl_core)
rippl_add_unit_test(NAME test_color SOURCES gfx/test_color.c LIBRARIES rippl_gfx)
rippl_add_unit_test(NAME test_rect SOURCES gfx/test_rect.c LIBRARIES rippl_gfx)
rippl_add_unit_test(NAME test_canvas SOURCES gfx/test_canvas.c LIBRARIES rippl_gfx)
rippl_add_unit_test(NAME test_image SOURCES gfx/test_image.c LIBRARIES rippl_gfx)
rippl_add_unit_test(NAME test_strip_renderer SOURCES gfx/test_strip_renderer.c LIBRARIES rippl_gfx)
rippl_add_unit_test(NAME test_frame_diff SOURCES gfx/test_frame_diff.c LIBRARIES rippl_gfx)
rippl_add_unit_test(NAME test_font SOURCES gfx/test_font.c gfx/tiny_font.c LIBRARIES rippl_gfx)
rippl_add_unit_test(NAME test_text SOURCES gfx/test_text.c gfx/tiny_font.c LIBRARIES rippl_gfx)
rippl_add_unit_test(NAME test_font_pack SOURCES gfx/test_font_pack.c gfx/tiny_font.c LIBRARIES rippl_gfx)
rippl_add_unit_test(NAME test_fonts SOURCES fonts/test_fonts.c LIBRARIES rippl_fonts)

add_library(rippl_library_fixtures STATIC
    library/memory_source.c
    library/mp4_book.c
    library/fixture_library.c
    library/big_library.c
    library/spoken_library.c
)
target_include_directories(rippl_library_fixtures PUBLIC library)
target_compile_definitions(rippl_library_fixtures PRIVATE
    RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(rippl_library_fixtures PUBLIC rippl_library rippl_library_scan PRIVATE rippl_options)

foreach(playback_test play_queue resume_state up_next bookmarks spoken lyrics)
    rippl_add_unit_test(NAME test_${playback_test}
        SOURCES playback/test_${playback_test}.c
        LIBRARIES rippl_playback rippl_library_fixtures
    )
endforeach()

rippl_add_unit_test(NAME test_settings SOURCES settings/test_settings.c LIBRARIES rippl_settings)
rippl_add_unit_test(NAME test_rocker SOURCES settings/test_rocker.c LIBRARIES rippl_settings)
rippl_add_unit_test(NAME test_equalizer
    SOURCES dsp/test_equalizer.c
    LIBRARIES rippl_dsp $<$<PLATFORM_ID:Linux>:m>
)
rippl_add_unit_test(NAME test_equalizer_file SOURCES sound/test_equalizer_file.c LIBRARIES rippl_sound)
rippl_add_unit_test(NAME test_equalizer_text SOURCES sound/test_equalizer_text.c LIBRARIES rippl_sound)

foreach(library_test block_cache library library_browse favorites covers sort_key track_fields playlist_reader music_fingerprint music_folders mp4_chapters)
    rippl_add_unit_test(NAME test_${library_test}
        SOURCES library/test_${library_test}.c
        LIBRARIES rippl_library_fixtures
    )
endforeach()
add_library(rippl_reference_builder STATIC
    library/reference/reference_builder.c
    library/reference/reference_builder_encode.c
    library/reference/reference_builder_model.c
    library/reference/reference_builder_paths.c
)
target_include_directories(rippl_reference_builder PUBLIC library/reference)
target_link_libraries(rippl_reference_builder PUBLIC rippl_library_scan PRIVATE rippl_options)

add_library(rippl_library_checks STATIC library/library_compare.c library/random_library.c)
target_include_directories(rippl_library_checks PUBLIC library)
target_link_libraries(rippl_library_checks PUBLIC rippl_library_scan PRIVATE rippl_options)

rippl_add_unit_test(NAME test_library_builder
    SOURCES library/test_library_builder.c
    LIBRARIES rippl_library_fixtures rippl_library_checks rippl_reference_builder
)
set_tests_properties(test_library_builder PROPERTIES TIMEOUT 600)
target_compile_definitions(test_library_builder PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_track_tags
    SOURCES library/test_track_tags.c
    LIBRARIES rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/library
)
target_compile_definitions(test_track_tags PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_cover_build
    SOURCES library/test_cover_build.c
    LIBRARIES rippl_library_scan
)
target_compile_definitions(test_cover_build PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

rippl_add_unit_test(NAME test_screen_stack SOURCES ui/test_screen_stack.c LIBRARIES rippl_ui)
rippl_add_unit_test(NAME test_navigation SOURCES ui/test_navigation.c LIBRARIES rippl_ui)
rippl_add_unit_test(NAME test_marquee SOURCES ui/test_marquee.c LIBRARIES rippl_ui)
rippl_add_unit_test(NAME test_fade SOURCES ui/test_fade.c LIBRARIES rippl_ui)
rippl_add_unit_test(NAME test_slide SOURCES ui/test_slide.c LIBRARIES rippl_ui)
rippl_add_unit_test(NAME test_cover_flow SOURCES ui/test_cover_flow.c LIBRARIES rippl_ui)
rippl_add_unit_test(NAME test_label
    SOURCES ui/test_label.c gfx/tiny_font.c
    LIBRARIES rippl_ui
    PRIVATE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/gfx
)

add_library(rippl_ui_fixtures STATIC gfx/tiny_font.c ui/ui_test_theme.c ui/ui_test_canvas.c)
target_include_directories(rippl_ui_fixtures PUBLIC gfx ui)
target_link_libraries(rippl_ui_fixtures PUBLIC rippl_ui PRIVATE rippl_options)

foreach(ui_test theme theme_layout list_view list_navigation fast_scroll header menu_screen message_screen
        choice_screen toast status_bar)
    rippl_add_unit_test(NAME test_${ui_test}
        SOURCES ui/test_${ui_test}.c
        LIBRARIES rippl_ui_fixtures
    )
endforeach()

add_library(rippl_np_faces_fixtures STATIC np_faces/np_faces_test.c)
target_include_directories(rippl_np_faces_fixtures PUBLIC np_faces)
target_link_libraries(rippl_np_faces_fixtures PUBLIC rippl_np_faces rippl_ui_fixtures PRIVATE rippl_options)

foreach(np_faces_test np_faces face_cassette face_vu face_turntable face_lcd)
    rippl_add_unit_test(NAME test_${np_faces_test}
        SOURCES np_faces/test_${np_faces_test}.c
        LIBRARIES rippl_np_faces_fixtures
    )
endforeach()
rippl_add_unit_test(NAME test_face_contrast
    SOURCES np_faces/test_face_contrast.c
    LIBRARIES rippl_np_faces_fixtures rippl_fonts m
)
rippl_add_unit_test(NAME test_face_shapes
    SOURCES np_faces/test_face_shapes.c
    LIBRARIES rippl_np_faces_fixtures
)
rippl_add_unit_test(NAME test_face_text
    SOURCES np_faces/test_face_text.c
    LIBRARIES rippl_np_faces_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/np_faces
)
rippl_add_unit_test(NAME test_app
    SOURCES app/test_app.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures m
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_release_diagnostics
    SOURCES app/test_release_diagnostics.c ${PROJECT_SOURCE_DIR}/src/app/diagnostics_menu_release.c
        app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_draw_profile
    SOURCES app/test_draw_profile.c
    LIBRARIES rippl_app
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_player
    SOURCES app/test_player.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_spoken_word
    SOURCES app/test_spoken_word.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_cover_cache
    SOURCES app/test_cover_cache.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_home_art
    SOURCES app/test_home_art.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_stores
    SOURCES app/test_stores.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_update_record
    SOURCES app/test_update_record.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_browse_screen
    SOURCES app/test_browse_screen.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_up_next_screen
    SOURCES app/test_up_next_screen.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_cover_flow_order
    SOURCES app/test_cover_flow_order.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_equalizer_files
    SOURCES app/test_equalizer_files.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_equalizer_screen
    SOURCES app/test_equalizer_screen.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_now_playing_screen
    SOURCES app/test_now_playing_screen.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_diagnostics_values
    SOURCES app/test_diagnostics_values.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_bluetooth_link
    SOURCES app/test_bluetooth_link.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_bluetooth_screen
    SOURCES app/test_bluetooth_screen.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_stopwatch_screen
    SOURCES app/test_stopwatch_screen.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_clock_store
    SOURCES app/test_clock_store.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_date_time_screen
    SOURCES app/test_date_time_screen.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_settings_screens
    SOURCES app/test_settings_screens.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_folders_screen
    SOURCES app/test_folders_screen.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
rippl_add_unit_test(NAME test_test_pattern_screen
    SOURCES app/test_test_pattern_screen.c app/fake_hal.c
    LIBRARIES rippl_app rippl_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

foreach(device_test audio_math backlight image_date battery_gauge watchdog panic_text stall_watch bt_module card_write core1_control start_record stack_mark dac_sequence dma_loop flac_accel jack_sense key_ladder key_scanner msc np_faces_gate panel pcf8563 pll sd_card sleep_meter stream_drift tick_clock updater usb_audio usb_audio_sink usb_device usb_fallback wav_format vbus_debounce)
    rippl_add_unit_test(NAME test_${device_test}
        SOURCES device/test_${device_test}.c
        LIBRARIES rippl_device_logic
    )
endforeach()

rippl_add_unit_test(NAME test_emmc_layout
    SOURCES device/test_emmc_layout.c device/fake_disk.c
    LIBRARIES rippl_device_logic
)
rippl_add_unit_test(NAME test_flac
    SOURCES codec/test_flac.c codec/flac_writer.c
    LIBRARIES rippl_codec
)
target_compile_definitions(test_flac PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_alac
    SOURCES codec/test_alac.c
    LIBRARIES rippl_codec
)
target_compile_definitions(test_alac PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_mp4
    SOURCES codec/test_mp4.c
    LIBRARIES rippl_codec
)
target_compile_definitions(test_mp4 PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_ogg
    SOURCES codec/test_ogg.c
    LIBRARIES rippl_codec
)
target_compile_definitions(test_ogg PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_qoa
    SOURCES codec/test_qoa.c
    LIBRARIES rippl_codec
)
target_compile_definitions(test_qoa PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_flac_bits
    SOURCES codec/test_flac_bits.c
    LIBRARIES rippl_codec
)
target_include_directories(test_flac_bits PRIVATE ${PROJECT_SOURCE_DIR}/src/codec)
rippl_add_unit_test(NAME test_audio_engine
    SOURCES device/test_audio_engine.c
    LIBRARIES rippl_device_logic
)
target_compile_definitions(test_audio_engine PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_mp3_probe
    SOURCES device/test_mp3_probe.c
    LIBRARIES rippl_device_logic
)
target_compile_definitions(test_mp3_probe PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_mp3_decoder
    SOURCES device/test_mp3_decoder.c
    LIBRARIES rippl_device_logic rippl_libmad
)
target_compile_definitions(test_mp3_decoder PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_core1_stream
    SOURCES device/test_core1_stream.c codec/flac_writer.c
    LIBRARIES rippl_device_logic $<$<PLATFORM_ID:Linux>:m>
    PRIVATE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/codec
)
target_compile_definitions(test_core1_stream PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
rippl_add_unit_test(NAME test_cover_grid
    SOURCES image/test_cover_grid.c
    LIBRARIES rippl_image
)
add_executable(cover_decode_tool image/cover_decode_tool.c)
target_link_libraries(cover_decode_tool PRIVATE rippl_image rippl_options)
rippl_add_unit_test(NAME test_fat
    SOURCES device/test_fat.c device/sparse_card.c
    LIBRARIES rippl_device_logic
)
target_compile_definitions(test_fat PRIVATE RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_tests_properties(test_fat PROPERTIES TIMEOUT 20)
set(RIPPL_CREATED_CARDS ${CMAKE_CURRENT_BINARY_DIR}/created_cards)
file(MAKE_DIRECTORY ${RIPPL_CREATED_CARDS})
rippl_add_unit_test(NAME test_fat_create
    SOURCES device/test_fat_create.c device/sparse_card.c
    LIBRARIES rippl_device_logic
)
target_compile_definitions(test_fat_create PRIVATE
    RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    RIPPL_CREATED_DIR="${RIPPL_CREATED_CARDS}"
)
set_tests_properties(test_fat_create PROPERTIES TIMEOUT 30 FIXTURES_SETUP created_cards)

if(RIPPL_BUILD_SIM)
    rippl_add_unit_test(NAME test_sim_options
        SOURCES sim/test_sim_options.c
        LIBRARIES rippl_sim_options
    )
    rippl_add_unit_test(NAME test_sim_script
        SOURCES sim/test_sim_script.c
        LIBRARIES rippl_sim_options
    )
    rippl_add_unit_test(NAME test_sim_decoder
        SOURCES sim/test_sim_decoder.c
        LIBRARIES rippl_sim_decoder
    )
    target_compile_definitions(test_sim_decoder PRIVATE
        RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    rippl_add_unit_test(NAME test_sim_storage
        SOURCES sim/test_sim_storage.c
        LIBRARIES rippl_sim_storage
    )
    rippl_add_unit_test(NAME test_sim_glyphs
        SOURCES sim/test_sim_glyphs.c
        LIBRARIES rippl_sim_glyphs rippl_fonts
    )
    target_compile_definitions(test_sim_storage PRIVATE
        RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        RIPPL_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    )
    rippl_add_unit_test(NAME test_library_scan
        SOURCES library/test_library_scan.c
        LIBRARIES rippl_library_scan rippl_sim_storage rippl_library_checks
    )
    target_compile_definitions(test_library_scan PRIVATE
        RIPPL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    add_executable(cover_build_tool library/cover_build_tool.c)
    target_link_libraries(cover_build_tool PRIVATE rippl_library_scan rippl_sim_storage
        rippl_options)
    if(EXISTS ${PROJECT_SOURCE_DIR}/.venv/bin/python)
        add_test(NAME cover_build_matches_reference
            COMMAND ${PROJECT_SOURCE_DIR}/.venv/bin/python
                ${PROJECT_SOURCE_DIR}/tools/check_cover_build.py
                --tool $<TARGET_FILE:cover_build_tool>
                --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
                --work ${CMAKE_CURRENT_BINARY_DIR}/cover_build
        )
        set_tests_properties(cover_build_matches_reference PROPERTIES TIMEOUT 600)
    endif()
    add_test(NAME sim_headless_smoke
        COMMAND rippl_sim --headless --frames 30 --screenshot ${CMAKE_BINARY_DIR}/sim_smoke.bmp
    )
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

find_program(FSCK_MSDOS fsck_msdos)
find_program(FSCK_EXFAT fsck_exfat)
if(FSCK_MSDOS AND FSCK_EXFAT)
    add_test(NAME created_cards_pass_fsck
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_card_images.py
            ${RIPPL_CREATED_CARDS} --expect 7
    )
    set_tests_properties(created_cards_pass_fsck PROPERTIES
        FIXTURES_REQUIRED created_cards
        RESOURCE_LOCK disk_images
        TIMEOUT 120
    )
else()
    message(STATUS "fsck_msdos or fsck_exfat not found: created_cards_pass_fsck is skipped")
endif()

add_test(NAME no_comments
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_no_comments.py ${PROJECT_SOURCE_DIR}
)
add_test(NAME np_faces_sections
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_np_faces_sections.py ${PROJECT_SOURCE_DIR}
)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMATTED_SOURCES CONFIGURE_DEPENDS
        ${PROJECT_SOURCE_DIR}/src/*.c ${PROJECT_SOURCE_DIR}/src/*.h
        ${PROJECT_SOURCE_DIR}/platform/*.c ${PROJECT_SOURCE_DIR}/platform/*.h
        ${PROJECT_SOURCE_DIR}/tests/*.c ${PROJECT_SOURCE_DIR}/tests/*.h
    )
    add_test(NAME format COMMAND ${CLANG_FORMAT} --dry-run --Werror ${FORMATTED_SOURCES})
else()
    message(STATUS "clang-format not found: the format test is skipped")
endif()

find_program(ARM_GCC arm-none-eabi-gcc)
if(ARM_GCC)
    add_test(NAME target_audit
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/target_audit.py
            --source-root ${PROJECT_SOURCE_DIR}/src
            --budget-bytes ${RIPPL_APP_RAM_BUDGET_BYTES}
    )
    add_test(NAME device_build
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${PROJECT_SOURCE_DIR}/cmake/check_device_build.cmake
    )
    set_tests_properties(device_build PROPERTIES FIXTURES_SETUP device_image)
    add_executable(check_device_image device/check_device_image.c)
    target_link_libraries(check_device_image PRIVATE rippl_device_logic rippl_options)
    add_test(NAME device_image_passes_the_updater_check
        COMMAND check_device_image ${PROJECT_SOURCE_DIR}/build/device/RIPPL.IMG
    )
    set_tests_properties(device_image_passes_the_updater_check PROPERTIES
        FIXTURES_REQUIRED device_image
        SKIP_RETURN_CODE 77
    )
else()
    message(STATUS "arm-none-eabi-gcc not found: the target_audit test is skipped")
endif()

set(RIPPL_TOOLS_PYTHON ${PROJECT_SOURCE_DIR}/.venv/bin/python)
if(EXISTS ${RIPPL_TOOLS_PYTHON})
    add_test(NAME tools_unit_tests
        COMMAND ${RIPPL_TOOLS_PYTHON} -m unittest discover -s ${PROJECT_SOURCE_DIR}/tools/tests
    )
    add_test(NAME cover_decode_matches_pillow
        COMMAND ${RIPPL_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/check_cover_decode.py
            --tool $<TARGET_FILE:cover_decode_tool>
            --work ${CMAKE_CURRENT_BINARY_DIR}/cover_decode
    )
    set_tests_properties(cover_decode_matches_pillow PROPERTIES TIMEOUT 1200)
    add_test(NAME fonts_up_to_date
        COMMAND ${RIPPL_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/font_build.py
            --config ${PROJECT_SOURCE_DIR}/assets/fonts/fonts.json
            --output ${PROJECT_SOURCE_DIR}/src/fonts
            --style ${PROJECT_SOURCE_DIR}/.clang-format
            --check
    )
    add_test(NAME glyph_packs_up_to_date
        COMMAND ${RIPPL_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/glyph_pack.py
            --config ${PROJECT_SOURCE_DIR}/assets/fonts/glyph_pack.json
            --output ${PROJECT_SOURCE_DIR}/src/fonts/packs
            --check
    )
    add_test(NAME language_packs_up_to_date
        COMMAND ${RIPPL_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/lang_pack.py
            --catalogue ${PROJECT_SOURCE_DIR}/src/core/include/core
            --languages ${PROJECT_SOURCE_DIR}/assets/lang
            --output ${PROJECT_SOURCE_DIR}/src/lang/packs
            --check
    )
    add_test(NAME cover_fixture_up_to_date
        COMMAND ${RIPPL_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/cover_fixture.py
            --output ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/small_covers.bin
            --check
    )
    add_test(NAME tags_library_up_to_date
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON=${RIPPL_TOOLS_PYTHON}
            -DINDEXER=${PROJECT_SOURCE_DIR}/tools/library_index.py
            -DMUSIC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tags
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tags_library.db
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tags_library.db
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_library_index.cmake
    )
    add_test(NAME scan_music_up_to_date
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON=${RIPPL_TOOLS_PYTHON}
            -DINDEXER=${PROJECT_SOURCE_DIR}/tools/library_index.py
            -DMUSIC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/scan_music
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/scan_music.db
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/scan_music.db
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_library_index.cmake
    )
    add_test(NAME scan_music_folders_up_to_date
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON=${RIPPL_TOOLS_PYTHON}
            -DINDEXER=${PROJECT_SOURCE_DIR}/tools/library_index.py
            -DMUSIC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/scan_music
            -DFOLDERS=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/scan_music_folders.bin
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/scan_music_folders.db
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/scan_music_folders.db
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_library_index.cmake
    )
    add_test(NAME two_drives_up_to_date
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON=${RIPPL_TOOLS_PYTHON}
            -DINDEXER=${PROJECT_SOURCE_DIR}/tools/library_index.py
            -DMUSIC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/scan_music
            -DINTERNAL=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tags
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/two_drives.db
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/two_drives.db
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_library_index.cmake
    )
    add_test(NAME builder_fixture_up_to_date
        COMMAND ${RIPPL_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/builder_fixture.py
            --output ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/builder_random
            --check
    )
    add_test(NAME sort_fold_up_to_date
        COMMAND ${RIPPL_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/fold_table.py
            --output ${PROJECT_SOURCE_DIR}/src/library
            --style ${PROJECT_SOURCE_DIR}/.clang-format
            --check
    )
    add_test(NAME library_fixture_up_to_date
        COMMAND ${RIPPL_TOOLS_PYTHON} ${PROJECT_SOURCE_DIR}/tools/library_fixture.py
            --output ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/small_library.db
            --tracks-only-output ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/small_library_tracks.db
            --check
    )
else()
    message(STATUS "tools venv not found: run python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt")
endif()
