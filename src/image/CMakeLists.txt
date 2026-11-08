add_library(rippl_image STATIC
    cover_grid.c
    cover_image.c
    image_arena.c
    image_reader.c
    inflate.c
    jpeg.c
    jpeg_blocks.c
    jpeg_entropy.c
    png.c
)
target_include_directories(rippl_image PUBLIC include)
target_link_libraries(rippl_image
    PUBLIC rippl_core
    PRIVATE rippl_options
)
