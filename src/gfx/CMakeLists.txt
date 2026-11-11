add_library(rippl_gfx STATIC
    canvas.c
    color.c
    font.c
    font_pack.c
    frame_diff.c
    image.c
    rect.c
    strip_renderer.c
    text.c
)
target_include_directories(rippl_gfx PUBLIC include)
if(CMAKE_CROSSCOMPILING)
    set_source_files_properties(font_pack.c PROPERTIES COMPILE_OPTIONS -Os)
endif()
target_link_libraries(rippl_gfx
    PUBLIC rippl_core
    PRIVATE rippl_options rippl_lto
)
