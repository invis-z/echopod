add_library(rippl_np_faces STATIC
    np_faces.c
    face_cassette.c
    face_shapes.c
    face_text.c
    face_lcd.c
    face_turntable.c
    face_vu.c
)
target_include_directories(rippl_np_faces PUBLIC include PRIVATE .)
if(CMAKE_CROSSCOMPILING)
    target_compile_definitions(rippl_np_faces PRIVATE RIPPL_NP_FACES_SECTIONS)
endif()
target_link_libraries(rippl_np_faces
    PUBLIC rippl_core rippl_gfx rippl_ui rippl_playback rippl_settings
    PRIVATE rippl_options rippl_compact rippl_lto
)
