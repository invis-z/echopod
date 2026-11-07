#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library/byte_source.h"

typedef enum LibrarySection {
    LIBRARY_TRACKS,
    LIBRARY_ALBUMS,
    LIBRARY_ARTISTS,
    LIBRARY_ALBUM_ARTISTS,
    LIBRARY_GENRES,
    LIBRARY_FOLDERS,
    LIBRARY_TITLE_ORDER,
    LIBRARY_PLAYLISTS,
    LIBRARY_LINKS,
    LIBRARY_STRINGS
} LibrarySection;

enum {
    LIBRARY_SECTION_COUNT = LIBRARY_STRINGS + 1,
    LIBRARY_VERSION = 3,
    LIBRARY_HEADER_SIZE = 128,
    LIBRARY_WIDE_VERSION = 1,
    LIBRARY_WIDE_HEADER_SIZE = 256,
    LIBRARY_WIDE_SECTION_COUNT = LIBRARY_SECTION_COUNT + 1,
    LIBRARY_ID_SIZE = 12
};

#define LIBRARY_NONE UINT32_C(0xFFFFFFFF)

typedef enum LibraryStatus {
    LIBRARY_OK,
    LIBRARY_UNREADABLE,
    LIBRARY_BAD_MAGIC,
    LIBRARY_BAD_VERSION,
    LIBRARY_BAD_LAYOUT
} LibraryStatus;

typedef struct LibrarySectionInfo {
    uint32_t offset;
    uint32_t count;
    uint16_t record_size;
    uint32_t music;
} LibrarySectionInfo;

typedef struct Library {
    ByteSource source;
    bool wide;
    LibrarySectionInfo sections[LIBRARY_SECTION_COUNT];
    LibrarySectionInfo ids;
    uint32_t generation;
} Library;

typedef struct LibraryTrack {
    uint32_t title;
    uint32_t path;
    uint32_t album;
    uint32_t artist;
    uint32_t genre;
    uint32_t folder;
    uint32_t duration_ms;
    uint8_t disc;
    uint8_t number;
    uint16_t chapters;
} LibraryTrack;

typedef struct LibraryAlbum {
    uint32_t title;
    uint32_t album_artist;
    uint32_t genre;
    uint16_t year;
    uint32_t first_track;
    uint32_t track_count;
    uint32_t cover;
} LibraryAlbum;

typedef struct LibraryGroup {
    uint32_t name;
    uint32_t first_link;
    uint32_t link_count;
} LibraryGroup;

typedef struct LibraryFolder {
    uint32_t name;
    uint32_t parent;
    uint32_t subfolder_count;
    uint32_t first_link;
    uint32_t track_count;
} LibraryFolder;

LibraryStatus library_open(Library *library, ByteSource source, uint32_t size);
uint32_t library_count(const Library *library, LibrarySection section);
uint32_t library_music_count(const Library *library, LibrarySection section);
bool library_read_track(const Library *library, uint32_t index, LibraryTrack *track);
bool library_read_album(const Library *library, uint32_t index, LibraryAlbum *album);
bool library_read_group(const Library *library, LibrarySection section, uint32_t index,
                        LibraryGroup *group);
bool library_read_folder(const Library *library, uint32_t index, LibraryFolder *folder);
bool library_read_index(const Library *library, LibrarySection section, uint32_t position,
                        uint32_t *value);
bool library_read_string(const Library *library, uint32_t offset, char *buffer, size_t capacity);
uint32_t library_fingerprint(const Library *library);
uint32_t library_album_key(const Library *library, uint32_t album_index);
uint64_t library_path_id(uint64_t hash, const char *bytes, uint32_t length);
bool library_track_id(const Library *library, uint32_t track, uint64_t *id);
bool library_find_track(const Library *library, uint64_t id, uint32_t *track);
