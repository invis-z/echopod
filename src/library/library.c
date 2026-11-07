#include "library/library.h"

#include "core/hash.h"
#include "core/utf8.h"

enum {
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    SECTION_COUNT_FIELD = 6,
    SECTION_TABLE = 8,
    SECTION_ENTRY_SIZE = 12,
    TRACK_SIZE = 24,
    ALBUM_SIZE = 16,
    GROUP_SIZE = 12,
    FOLDER_SIZE = 16,
    INDEX_SIZE = 2,
    STRING_SIZE = 1,
    BITS_PER_BYTE = 8,
    WIDE_SECTION_ENTRY_SIZE = 16,
    WIDE_TRACK_SIZE = 32,
    WIDE_ALBUM_SIZE = 24,
    WIDE_GROUP_SIZE = 12,
    WIDE_FOLDER_SIZE = 20,
    WIDE_INDEX_SIZE = 4,
    MAX_RECORD = WIDE_TRACK_SIZE,
    NARROW_NONE = 0xFFFF
};

#define NARROW_MAGIC UINT32_C(0x42444D45)
#define WIDE_MAGIC UINT32_C(0x42494C52)

static const uint16_t WIDE_RECORD_SIZES[LIBRARY_SECTION_COUNT] = {
    [LIBRARY_TRACKS] = WIDE_TRACK_SIZE,      [LIBRARY_ALBUMS] = WIDE_ALBUM_SIZE,
    [LIBRARY_ARTISTS] = WIDE_GROUP_SIZE,     [LIBRARY_ALBUM_ARTISTS] = WIDE_GROUP_SIZE,
    [LIBRARY_GENRES] = WIDE_GROUP_SIZE,      [LIBRARY_FOLDERS] = WIDE_FOLDER_SIZE,
    [LIBRARY_TITLE_ORDER] = WIDE_INDEX_SIZE, [LIBRARY_PLAYLISTS] = WIDE_GROUP_SIZE,
    [LIBRARY_LINKS] = WIDE_INDEX_SIZE,       [LIBRARY_STRINGS] = STRING_SIZE,
};

static const uint16_t RECORD_SIZES[LIBRARY_SECTION_COUNT] = {
    [LIBRARY_TRACKS] = TRACK_SIZE,      [LIBRARY_ALBUMS] = ALBUM_SIZE,
    [LIBRARY_ARTISTS] = GROUP_SIZE,     [LIBRARY_ALBUM_ARTISTS] = GROUP_SIZE,
    [LIBRARY_GENRES] = GROUP_SIZE,      [LIBRARY_FOLDERS] = FOLDER_SIZE,
    [LIBRARY_TITLE_ORDER] = INDEX_SIZE, [LIBRARY_PLAYLISTS] = GROUP_SIZE,
    [LIBRARY_LINKS] = INDEX_SIZE,       [LIBRARY_STRINGS] = STRING_SIZE,
};

static uint16_t u16_at(const uint8_t *bytes, uint32_t offset)
{
    return (uint16_t)(bytes[offset] | bytes[offset + 1] << BITS_PER_BYTE);
}

static uint32_t u32_at(const uint8_t *bytes, uint32_t offset)
{
    return (uint32_t)u16_at(bytes, offset) | (uint32_t)u16_at(bytes, offset + 2) << 16;
}

static bool read_source(const Library *library, uint32_t offset, void *buffer, uint32_t length)
{
    return library->source.read(library->source.context, offset, buffer, length);
}

static LibrarySectionInfo decode_section(const uint8_t *header, int section, bool wide)
{
    uint32_t size = wide ? WIDE_SECTION_ENTRY_SIZE : SECTION_ENTRY_SIZE;
    uint32_t entry = (uint32_t)SECTION_TABLE + (uint32_t)section * size;
    return (LibrarySectionInfo){
        .offset = u32_at(header, entry),
        .count = u32_at(header, entry + 4),
        .record_size = u16_at(header, entry + 8),
        .music = wide ? u32_at(header, entry + 12) : u16_at(header, entry + 10),
    };
}

static bool section_fits(LibrarySectionInfo section, uint16_t expected_size, uint32_t header_size,
                         uint32_t size)
{
    uint64_t end = (uint64_t)section.offset + (uint64_t)section.count * section.record_size;
    return section.record_size == expected_size && section.offset >= header_size && end <= size;
}

static LibraryStatus decode_header(Library *library, const uint8_t *header, uint32_t size)
{
    uint32_t magic = u32_at(header, 0);
    bool wide = magic == WIDE_MAGIC;
    if (!wide && magic != NARROW_MAGIC) {
        return LIBRARY_BAD_MAGIC;
    }
    if (u16_at(header, VERSION_FIELD) != (wide ? LIBRARY_WIDE_VERSION : LIBRARY_VERSION)) {
        return LIBRARY_BAD_VERSION;
    }
    if (u16_at(header, SECTION_COUNT_FIELD) !=
        (wide ? LIBRARY_WIDE_SECTION_COUNT : LIBRARY_SECTION_COUNT)) {
        return LIBRARY_BAD_LAYOUT;
    }
    const uint16_t *sizes = wide ? WIDE_RECORD_SIZES : RECORD_SIZES;
    uint32_t header_size = wide ? LIBRARY_WIDE_HEADER_SIZE : LIBRARY_HEADER_SIZE;
    for (int section = 0; section < LIBRARY_SECTION_COUNT; section++) {
        LibrarySectionInfo info = decode_section(header, section, wide);
        if (!section_fits(info, sizes[section], header_size, size)) {
            return LIBRARY_BAD_LAYOUT;
        }
        library->sections[section] = info;
    }
    if (wide) {
        library->ids = decode_section(header, LIBRARY_SECTION_COUNT, true);
        if (!section_fits(library->ids, LIBRARY_ID_SIZE, header_size, size) ||
            library->ids.count != library->sections[LIBRARY_TRACKS].count) {
            return LIBRARY_BAD_LAYOUT;
        }
    }
    library->wide = wide;
    return LIBRARY_OK;
}

static uint32_t narrow_index(uint16_t value)
{
    return value == NARROW_NONE ? LIBRARY_NONE : value;
}

static bool read_record(const Library *library, LibrarySection section, uint32_t index,
                        uint8_t *record)
{
    LibrarySectionInfo info = library->sections[section];
    if (index >= info.count) {
        return false;
    }
    return read_source(library, info.offset + index * info.record_size, record, info.record_size);
}

static bool is_group_section(LibrarySection section)
{
    return section == LIBRARY_ARTISTS || section == LIBRARY_ALBUM_ARTISTS ||
           section == LIBRARY_GENRES || section == LIBRARY_PLAYLISTS;
}

static bool is_index_section(LibrarySection section)
{
    return section == LIBRARY_TITLE_ORDER || section == LIBRARY_LINKS;
}

static void clear(Library *library, ByteSource source)
{
    *library = (Library){.source = source};
}

LibraryStatus library_open(Library *library, ByteSource source, uint32_t size)
{
    clear(library, source);
    uint8_t header[LIBRARY_WIDE_HEADER_SIZE];
    uint32_t wanted =
        size < LIBRARY_WIDE_HEADER_SIZE ? LIBRARY_HEADER_SIZE : LIBRARY_WIDE_HEADER_SIZE;
    if (size < LIBRARY_HEADER_SIZE || !read_source(library, 0, header, wanted)) {
        return LIBRARY_UNREADABLE;
    }
    LibraryStatus status = decode_header(library, header, size);
    if (status != LIBRARY_OK) {
        clear(library, source);
    }
    return status;
}

uint32_t library_count(const Library *library, LibrarySection section)
{
    return library->sections[section].count;
}

uint32_t library_music_count(const Library *library, LibrarySection section)
{
    LibrarySectionInfo info = library->sections[section];
    bool counted = section <= LIBRARY_GENRES || section == LIBRARY_TITLE_ORDER;
    return counted && info.music < info.count ? info.music : info.count;
}

enum {
    FIELD_OFFSET = 0x3F,
    FIELD_U8 = 0x00,
    FIELD_U16 = 0x40,
    FIELD_U32 = 0x80,
    FIELD_INDEX = 0xC0,
    FIELD_NONE = 0xFF,
    TRACK_FIELDS = 9,
    ALBUM_FIELDS = 7,
    GROUP_FIELDS = 3,
    FOLDER_FIELDS = 5
};

static const uint8_t TRACK_LAYOUT[2][TRACK_FIELDS] = {
    {FIELD_U32 | 0, FIELD_U32 | 4, FIELD_U16 | 8, FIELD_U16 | 10, FIELD_U16 | 12, FIELD_U16 | 14,
     FIELD_U32 | 16, FIELD_U8 | 20, FIELD_U8 | 21},
    {FIELD_U32 | 0, FIELD_U32 | 4, FIELD_U32 | 8, FIELD_U32 | 12, FIELD_U32 | 16, FIELD_U32 | 20,
     FIELD_U32 | 24, FIELD_U8 | 28, FIELD_U8 | 29},
};

static const uint8_t ALBUM_LAYOUT[2][ALBUM_FIELDS] = {
    {FIELD_U32 | 0, FIELD_U16 | 4, FIELD_U16 | 6, FIELD_U16 | 8, FIELD_U16 | 10, FIELD_U16 | 12,
     FIELD_INDEX | 14},
    {FIELD_U32 | 0, FIELD_U32 | 4, FIELD_U32 | 8, FIELD_U16 | 20, FIELD_U32 | 12, FIELD_U32 | 16,
     FIELD_NONE},
};

static const uint8_t GROUP_LAYOUT[2][GROUP_FIELDS] = {
    {FIELD_U32 | 0, FIELD_U32 | 4, FIELD_U16 | 8},
    {FIELD_U32 | 0, FIELD_U32 | 4, FIELD_U32 | 8},
};

static const uint8_t FOLDER_LAYOUT[2][FOLDER_FIELDS] = {
    {FIELD_U32 | 0, FIELD_INDEX | 4, FIELD_U16 | 6, FIELD_U32 | 8, FIELD_U16 | 12},
    {FIELD_U32 | 0, FIELD_U32 | 4, FIELD_U32 | 8, FIELD_U32 | 12, FIELD_U32 | 16},
};

static uint32_t field_value(const uint8_t *record, uint8_t field)
{
    uint32_t offset = field & FIELD_OFFSET;
    if (field == FIELD_NONE) {
        return LIBRARY_NONE;
    }
    switch (field & (uint8_t)~FIELD_OFFSET) {
    case FIELD_U8:
        return record[offset];
    case FIELD_U16:
        return u16_at(record, offset);
    case FIELD_U32:
        return u32_at(record, offset);
    default:
        return narrow_index(u16_at(record, offset));
    }
}

static bool read_fields(const Library *library, LibrarySection section, uint32_t index,
                        const uint8_t *layout, uint32_t count, uint32_t *values)
{
    uint8_t record[MAX_RECORD];
    if (!read_record(library, section, index, record)) {
        return false;
    }
    for (uint32_t field = 0; field < count; field++) {
        values[field] = field_value(record, layout[field]);
    }
    return true;
}

bool library_read_track(const Library *library, uint32_t index, LibraryTrack *track)
{
    uint32_t v[TRACK_FIELDS];
    if (!read_fields(library, LIBRARY_TRACKS, index, TRACK_LAYOUT[library->wide], TRACK_FIELDS,
                     v)) {
        return false;
    }
    *track = (LibraryTrack){.title = v[0],
                            .path = v[1],
                            .album = v[2],
                            .artist = v[3],
                            .genre = v[4],
                            .folder = v[5],
                            .duration_ms = v[6],
                            .disc = (uint8_t)v[7],
                            .number = (uint8_t)v[8]};
    return true;
}

bool library_read_album(const Library *library, uint32_t index, LibraryAlbum *album)
{
    uint32_t v[ALBUM_FIELDS];
    if (!read_fields(library, LIBRARY_ALBUMS, index, ALBUM_LAYOUT[library->wide], ALBUM_FIELDS,
                     v)) {
        return false;
    }
    *album = (LibraryAlbum){.title = v[0],
                            .album_artist = v[1],
                            .genre = v[2],
                            .year = (uint16_t)v[3],
                            .first_track = v[4],
                            .track_count = v[5],
                            .cover = v[6]};
    return true;
}

bool library_read_group(const Library *library, LibrarySection section, uint32_t index,
                        LibraryGroup *group)
{
    uint32_t v[GROUP_FIELDS];
    if (!is_group_section(section) ||
        !read_fields(library, section, index, GROUP_LAYOUT[library->wide], GROUP_FIELDS, v)) {
        return false;
    }
    *group = (LibraryGroup){.name = v[0], .first_link = v[1], .link_count = v[2]};
    return true;
}

bool library_read_folder(const Library *library, uint32_t index, LibraryFolder *folder)
{
    uint32_t v[FOLDER_FIELDS];
    if (!read_fields(library, LIBRARY_FOLDERS, index, FOLDER_LAYOUT[library->wide], FOLDER_FIELDS,
                     v)) {
        return false;
    }
    *folder = (LibraryFolder){.name = v[0],
                              .parent = v[1],
                              .subfolder_count = v[2],
                              .first_link = v[3],
                              .track_count = v[4]};
    return true;
}

bool library_read_index(const Library *library, LibrarySection section, uint32_t position,
                        uint32_t *value)
{
    static const uint8_t INDEX_LAYOUT[2] = {FIELD_U16 | 0, FIELD_U32 | 0};
    return is_index_section(section) &&
           read_fields(library, section, position, &INDEX_LAYOUT[library->wide], 1, value);
}

static size_t terminated_length(const char *buffer, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (buffer[index] == '\0') {
            return index;
        }
    }
    return utf8_complete_length(buffer, length);
}

enum {
    STRING_PIECE = 32
};

static bool has_end(const char *text, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (text[index] == '\0') {
            return true;
        }
    }
    return false;
}

static bool read_until_end(const Library *library, uint32_t start, char *buffer, uint32_t *wanted)
{
    for (uint32_t done = 0; done < *wanted; done += STRING_PIECE) {
        uint32_t piece = *wanted - done < STRING_PIECE ? *wanted - done : STRING_PIECE;
        if (!read_source(library, start + done, buffer + done, piece)) {
            return false;
        }
        if (has_end(buffer + done, piece)) {
            *wanted = done + piece;
            return true;
        }
    }
    return true;
}

bool library_read_string(const Library *library, uint32_t offset, char *buffer, size_t capacity)
{
    LibrarySectionInfo strings = library->sections[LIBRARY_STRINGS];
    if (capacity == 0) {
        return false;
    }
    buffer[0] = '\0';
    if (offset >= strings.count) {
        return false;
    }
    uint32_t available = strings.count - offset;
    uint32_t wanted = capacity - 1 < available ? (uint32_t)(capacity - 1) : available;
    if (!read_until_end(library, strings.offset + offset, buffer, &wanted)) {
        buffer[0] = '\0';
        return false;
    }
    buffer[terminated_length(buffer, wanted)] = '\0';
    return true;
}

static uint32_t hash_word(uint32_t hash, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 24)};
    return hash_update(hash, bytes, sizeof bytes);
}

uint32_t library_fingerprint(const Library *library)
{
    uint32_t hash = HASH_START;
    for (int section = 0; section < LIBRARY_SECTION_COUNT; section++) {
        hash = hash_word(hash, library->sections[section].offset);
        hash = hash_word(hash, library->sections[section].count);
    }
    return hash;
}

enum {
    KEY_TEXT_CAPACITY = 256,
    KEY_CHUNK = 32,
    KEY_SEPARATOR = 0x1F
};

static uint32_t chunk_wanted(uint32_t strings_count, uint32_t offset, uint32_t done)
{
    uint32_t wanted = KEY_CHUNK;
    if (KEY_TEXT_CAPACITY - 1 - done < wanted) {
        wanted = KEY_TEXT_CAPACITY - 1 - done;
    }
    return strings_count - offset - done < wanted ? strings_count - offset - done : wanted;
}

static uint32_t hash_string(const Library *library, uint32_t hash, uint32_t offset)
{
    LibrarySectionInfo strings = library->sections[LIBRARY_STRINGS];
    char text[KEY_CHUNK];
    for (uint32_t done = 0; done < KEY_TEXT_CAPACITY - 1 && offset + done < strings.count;
         done += KEY_CHUNK) {
        uint32_t wanted = chunk_wanted(strings.count, offset, done);
        if (!read_source(library, strings.offset + offset + done, text, wanted)) {
            break;
        }
        uint32_t length = 0;
        while (length < wanted && text[length] != '\0') {
            length++;
        }
        hash = hash_update(hash, text, length);
        if (length < wanted) {
            break;
        }
    }
    return hash;
}

uint32_t library_album_key(const Library *library, uint32_t album_index)
{
    LibraryAlbum album;
    LibraryGroup artist;
    uint8_t separator = KEY_SEPARATOR;
    if (!library_read_album(library, album_index, &album) ||
        !library_read_group(library, LIBRARY_ALBUM_ARTISTS, album.album_artist, &artist)) {
        return 0;
    }
    uint32_t hash = hash_string(library, HASH_START, artist.name);
    hash = hash_update(hash, &separator, sizeof separator);
    return hash_string(library, hash, album.title);
}

enum {
    ID_CHUNK = 32
};

uint64_t library_path_id(uint64_t hash, const char *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        uint8_t byte = (uint8_t)bytes[index];
        byte = byte >= 'A' && byte <= 'Z' ? (uint8_t)(byte - 'A' + 'a') : byte;
        hash = hash64_update(hash, &byte, 1);
    }
    return hash;
}

bool library_track_id(const Library *library, uint32_t track, uint64_t *id)
{
    LibraryTrack record;
    LibrarySectionInfo strings = library->sections[LIBRARY_STRINGS];
    if (!library_read_track(library, track, &record) || record.path >= strings.count) {
        return false;
    }
    uint64_t hash = HASH64_START;
    char text[ID_CHUNK];
    for (uint32_t done = 0; record.path + done < strings.count; done += ID_CHUNK) {
        uint32_t left = strings.count - record.path - done;
        uint32_t wanted = left < ID_CHUNK ? left : ID_CHUNK;
        if (!read_source(library, strings.offset + record.path + done, text, wanted)) {
            return false;
        }
        uint32_t length = 0;
        while (length < wanted && text[length] != '\0') {
            length++;
        }
        hash = library_path_id(hash, text, length);
        if (length < wanted) {
            break;
        }
    }
    *id = hash;
    return true;
}

static bool read_id(const Library *library, uint32_t row, uint64_t *id, uint32_t *track)
{
    uint8_t bytes[LIBRARY_ID_SIZE];
    if (!read_source(library, library->ids.offset + row * LIBRARY_ID_SIZE, bytes, sizeof bytes)) {
        return false;
    }
    *id = (uint64_t)u32_at(bytes, 4) << 32 | u32_at(bytes, 0);
    *track = u32_at(bytes, 8);
    return true;
}

bool library_find_track(const Library *library, uint64_t id, uint32_t *track)
{
    uint32_t low = 0;
    uint32_t high = library->ids.count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        uint64_t value = 0;
        uint32_t found = 0;
        if (!read_id(library, middle, &value, &found)) {
            return false;
        }
        if (value < id) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    uint64_t value = 0;
    return low < library->ids.count && read_id(library, low, &value, track) && value == id &&
           *track < library->sections[LIBRARY_TRACKS].count;
}
