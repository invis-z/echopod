add_library(rippl_fonts STATIC fonts.c)
target_include_directories(rippl_fonts PUBLIC include)
if(CMAKE_CROSSCOMPILING)
    target_compile_definitions(rippl_fonts PRIVATE RIPPL_RESIDENT_TABLES)
endif()
target_link_libraries(rippl_fonts
    PUBLIC rippl_gfx
    PRIVATE rippl_options rippl_lto
)

file(READ ${PROJECT_SOURCE_DIR}/assets/fonts/glyph_pack.json RIPPL_GLYPH_PACK_CONFIG)
string(JSON RIPPL_GLYPH_FACE_COUNT LENGTH "${RIPPL_GLYPH_PACK_CONFIG}" faces)
math(EXPR RIPPL_GLYPH_LAST_FACE "${RIPPL_GLYPH_FACE_COUNT} - 1")
set(RIPPL_GLYPH_FACE_FILES "")
foreach(face RANGE ${RIPPL_GLYPH_LAST_FACE})
    string(JSON face_name GET "${RIPPL_GLYPH_PACK_CONFIG}" faces ${face} name)
    list(APPEND RIPPL_GLYPH_FACE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/packs/${face_name}.bin)
endforeach()
set(RIPPL_GLYPH_FACE_FILES "${RIPPL_GLYPH_FACE_FILES}" CACHE INTERNAL "glyph pack faces in face order")
set_property(DIRECTORY ${PROJECT_SOURCE_DIR} APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/assets/fonts/glyph_pack.json
)
