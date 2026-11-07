add_library(rippl_library STATIC
    block_cache.c
    covers.c
    favorites.c
    library.c
    library_browse.c
    mp4_chapters.c
    music_fingerprint.c
    music_folders.c
    segment_store.c
    segment_stream.c
    sort_initial.c
    spoken_path.c
)
target_include_directories(rippl_library PUBLIC include)
if(CMAKE_CROSSCOMPILING)
    target_compile_definitions(rippl_library PRIVATE RIPPL_RESIDENT_TABLES)
endif()
target_link_libraries(rippl_library
    PUBLIC rippl_core
    PRIVATE rippl_options rippl_compact
)

add_library(rippl_library_scan STATIC
    builder_sort.c
    builder_store.c
    cover_build.c
    library_builder.c
    library_builder_albums.c
    library_builder_encode.c
    library_builder_folders.c
    library_builder_model.c
    library_builder_paths.c
    library_builder_rank.c
    library_rebuild.c
    library_scan.c
    library_scan_entry.c
    segment_scratch.c
    playlist_reader.c
    sort_fold_table.c
    sort_key.c
    tags_ape.c
    tags_asf.c
    tags_genres.c
    tags_id3.c
    tags_mp4.c
    tags_mpeg.c
    tags_qoa.c
    tags_riff.c
    tags_vorbis.c
    track_fields.c
    track_tags.c
)
target_include_directories(rippl_library_scan PUBLIC include)
target_link_libraries(rippl_library_scan
    PUBLIC rippl_core rippl_image rippl_library
    PRIVATE rippl_options rippl_compact
)
target_link_libraries(rippl_library PRIVATE rippl_lto)
