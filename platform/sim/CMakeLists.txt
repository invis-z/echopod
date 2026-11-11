add_library(rippl_sim_options STATIC sim_options.c sim_script.c)
target_include_directories(rippl_sim_options PUBLIC .)
target_link_libraries(rippl_sim_options PUBLIC rippl_core PRIVATE rippl_options)

find_package(SDL2 CONFIG REQUIRED)

add_library(rippl_sim_codecs STATIC sim_codecs.c)
target_include_directories(rippl_sim_codecs SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/dr_libs)
target_compile_options(rippl_sim_codecs PRIVATE -w)

add_library(rippl_sim_decoder STATIC sim_decoder.c sim_mp4.c sim_opus.c)
target_include_directories(rippl_sim_decoder PUBLIC .)
target_link_libraries(rippl_sim_decoder PUBLIC rippl_hal rippl_codec rippl_faad2 rippl_opus rippl_sim_codecs PRIVATE rippl_core rippl_options)

string(REPLACE ";" "|" RIPPL_GLYPH_FACE_PATHS "${RIPPL_GLYPH_FACE_FILES}")
add_library(rippl_sim_glyphs STATIC sim_glyphs.c)
target_include_directories(rippl_sim_glyphs PUBLIC .)
target_compile_definitions(rippl_sim_glyphs PRIVATE RIPPL_GLYPH_FACE_PATHS="${RIPPL_GLYPH_FACE_PATHS}")
target_link_libraries(rippl_sim_glyphs PUBLIC rippl_gfx PRIVATE rippl_options)

string(REPLACE ";" "|" RIPPL_LANGUAGE_PACK_PATHS "${RIPPL_LANGUAGE_PACK_FILES}")
add_library(rippl_sim_language STATIC sim_language.c)
target_compile_definitions(rippl_sim_language PRIVATE RIPPL_LANGUAGE_PACK_PATHS="${RIPPL_LANGUAGE_PACK_PATHS}")
target_link_libraries(rippl_sim_language PUBLIC rippl_hal PRIVATE rippl_options)

add_library(rippl_sim_storage STATIC sim_storage.c)
target_include_directories(rippl_sim_storage PUBLIC .)
target_link_libraries(rippl_sim_storage PUBLIC rippl_hal PRIVATE rippl_core rippl_options)

add_executable(rippl_sim
    main.c
    sim_audio.c
    sim_bluetooth.c
    sim_clock.c
    sim_display.c
    sim_input.c
    sim_power.c
    sim_system.c
    sim_time.c
)
string(TIMESTAMP RIPPL_SIM_BUILD_DATE "%Y%m%d" UTC)
target_compile_definitions(rippl_sim PRIVATE SDL_MAIN_HANDLED RIPPL_SIM_BUILD_DATE=${RIPPL_SIM_BUILD_DATE})
target_link_libraries(rippl_sim PRIVATE rippl_app rippl_sim_glyphs rippl_sim_language rippl_library_scan rippl_sim_options rippl_sim_decoder rippl_sim_storage rippl_options SDL2::SDL2 $<$<PLATFORM_ID:Linux>:m>)
