#include <stddef.h>

#include "builder_internal.h"
#include "core/hash.h"
#include "library/library.h"
#include "library/sort_key.h"

enum {
    HEADER_SIZE = 128,
    WIDE_HEADER_SIZE = 256,
    SECTION_COUNT = 10,
    WIDE_SECTION_COUNT = 13,
    KEY_SEPARATOR = 0x1F,
    ID_RECORD = 12,
    ID_ENTRY = 14,
    VERSION = 3,
    WIDE_VERSION = 1,
    WIDE_TRACK_RECORD = 32,
    WIDE_ALBUM_RECORD = 24,
    WIDE_FOLDER_RECORD = 20,
    WIDE_INDEX_RECORD = 4,
    ALIGNMENT = 4,
    TRACK_RECORD = 24,
    ALBUM_RECORD = 16,
    GROUP_RECORD = 12,
    FOLDER_RECORD = 16,
    INDEX_RECORD = 2,
    BUFFER_SIZE = 512,
    STREAM_BUFFER = 4096,
    ALBUM_FINAL = 28,
    GROUP_ENTRY = 8,
    DISPLAY_SIZE = 8,
    FOLDER_FINAL = 24,
    NAME_HEADER = 6,
    PLACE_SIZE = 16,
    PLACED_SIZE = 40,
    COLUMN_BLOCK = 1024,
    COLUMN_BLOCKS = 4
};

typedef enum Section {
    SECTION_TRACKS,
    SECTION_ALBUMS,
    SECTION_ARTISTS,
    SECTION_ALBUM_ARTISTS,
    SECTION_GENRES,
    SECTION_FOLDERS,
    SECTION_TITLE_ORDER,
    SECTION_PLAYLISTS,
    SECTION_LINKS,
    SECTION_STRINGS
} Section;

typedef struct Writer {
    ByteSink sink;
    uint32_t offset;
    uint32_t used;
    bool failed;
    uint8_t buffer[BUFFER_SIZE];
} Writer;

typedef struct Layout {
    uint32_t offset[SECTION_COUNT];
    uint32_t count[SECTION_COUNT];
    uint16_t record_size[SECTION_COUNT];
    uint32_t music[SECTION_COUNT];
    uint32_t ids;
    uint32_t album_keys;
    uint32_t album_ranks;
    uint32_t end;
} Layout;

typedef struct Encode {
    BuilderState *state;
    Prepared *prepared;
    Writer *writer;
    bool wide;
    Region placed;
    uint16_t *playlist_order;
    uint8_t *stream;
    char *text;
    char *folder_text;
    uint32_t folder_text_node;
    uint32_t folder_text_length;
    uint32_t strings;
    Region ids;
} Encode;

static void flush(Writer *writer)
{
    if (writer->used > 0 && !writer->failed) {
        writer->failed = !writer->sink.write(writer->sink.context, writer->offset - writer->used,
                                             writer->buffer, writer->used);
    }
    writer->used = 0;
}

static void put_byte(Writer *writer, uint8_t value)
{
    writer->buffer[writer->used++] = value;
    writer->offset++;
    if (writer->used == BUFFER_SIZE) {
        flush(writer);
    }
}

static void put_u16(Writer *writer, uint32_t value)
{
    put_byte(writer, (uint8_t)value);
    put_byte(writer, (uint8_t)(value >> 8));
}

static void put_u32(Writer *writer, uint32_t value)
{
    put_u16(writer, value & 0xFFFF);
    put_u16(writer, value >> 16);
}

static void put_bytes(Writer *writer, const char *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        put_byte(writer, (uint8_t)bytes[index]);
    }
}

static void put_index(Encode *encode, uint32_t value)
{
    if (encode->wide) {
        put_u32(encode->writer, value);
    } else {
        put_u16(encode->writer, value);
    }
}

static void pad_to(Writer *writer, uint32_t offset)
{
    while (writer->offset < offset) {
        put_byte(writer, 0);
    }
}

static uint32_t take_string(Encode *encode, uint32_t length)
{
    if (length == 0) {
        return 0;
    }
    uint32_t offset = encode->strings;
    encode->strings += length + 1;
    return offset;
}

static bool read_region(Encode *encode, Region region, uint32_t offset, void *bytes,
                        uint32_t length)
{
    return store_read(&encode->state->store, region.offset + offset, bytes, length);
}

static int compare_names(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    uint32_t left_key = builder_get16(left + 4);
    uint32_t right_key = builder_get16(right + 4);
    const uint8_t *a = left + NAME_HEADER;
    const uint8_t *b = right + NAME_HEADER;
    uint32_t left_total = builder_get16(left) - (uint32_t)NAME_HEADER;
    uint32_t right_total = builder_get16(right) - (uint32_t)NAME_HEADER;
    uint32_t shorter = left_key < right_key ? left_key : right_key;
    for (uint32_t index = 0; index < shorter; index++) {
        if (a[index] != b[index]) {
            return (int)a[index] - (int)b[index];
        }
    }
    if (left_key != right_key) {
        return left_key < right_key ? -1 : 1;
    }
    uint32_t left_raw = left_total - left_key;
    uint32_t right_raw = right_total - right_key;
    uint32_t raw = left_raw < right_raw ? left_raw : right_raw;
    for (uint32_t index = 0; index < raw; index++) {
        if (a[left_key + index] != b[right_key + index]) {
            return (int)a[left_key + index] - (int)b[right_key + index];
        }
    }
    return left_raw < right_raw ? -1 : left_raw > right_raw ? 1 : 0;
}

static bool read_playlist_name(Encode *encode, const PlaylistRow *playlist, char *text)
{
    text[playlist->length] = '\0';
    return read_region(encode, encode->prepared->playlist_names,
                       playlist->name - encode->prepared->playlist_names.offset, text,
                       playlist->length);
}

static bool order_playlists(Encode *encode)
{
    BuilderState *state = encode->state;
    Prepared *prepared = encode->prepared;
    uint32_t count = prepared->playlist_count;
    encode->playlist_order = arena_take(&state->arena, count * 2 + 2);
    char *key = arena_take(&state->arena, SORT_KEY_CAPACITY);
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (state->arena.failed) {
        return false;
    }
    uint32_t mark = state->arena.used;
    uint32_t start = state->store.end;
    Region all = store_take(&state->store, count * (NAME_HEADER + 2 * BUILDER_TEXT_CAPACITY));
    if (state->store.failed) {
        return false;
    }
    Out out;
    out_open(&out, &state->store, all, buffer, STREAM_BUFFER);
    for (uint32_t index = 0; index < count; index++) {
        const PlaylistRow *playlist = &prepared->playlists[index];
        if (!read_playlist_name(encode, playlist, encode->text)) {
            return false;
        }
        uint32_t key_length = (uint32_t)sort_key_make(encode->text, key, SORT_KEY_CAPACITY);
        uint8_t header[NAME_HEADER];
        builder_put16(header, NAME_HEADER + key_length + playlist->length);
        builder_put16(header + 2, index);
        builder_put16(header + 4, key_length);
        if (!out_put(&out, header, sizeof header) || !out_put(&out, key, key_length) ||
            !out_put(&out, encode->text, playlist->length)) {
            return false;
        }
    }
    if (!out_close(&out)) {
        return false;
    }
    Region input = {.offset = all.offset, .size = out.written};
    Region sorted;
    SortJob job = {.store = &state->store,
                   .arena = &state->arena,
                   .input = input,
                   .compare = compare_names,
                   .context = NULL};
    if (!sort_records(&job, &sorted)) {
        return false;
    }
    uint8_t *record = arena_take(&state->arena, BUILDER_MAX_RECORD);
    if (record == NULL) {
        return false;
    }
    In in;
    in_open(&in, &state->store, sorted.offset, sorted.offset + sorted.size, buffer, STREAM_BUFFER);
    for (uint32_t index = 0; index < count; index++) {
        if (!in_record(&in, record)) {
            return false;
        }
        encode->playlist_order[index] = builder_get16(record + 2);
    }
    state->arena.used = mark;
    state->store.end = start;
    return true;
}

static uint32_t string_total(Encode *encode)
{
    BuilderState *state = encode->state;
    Prepared *prepared = encode->prepared;
    uint32_t total = 1 + state->text_bytes;
    for (uint32_t album = 0; album < prepared->album_count; album++) {
        uint8_t record[ALBUM_FINAL];
        if (!read_region(encode, prepared->albums, album * ALBUM_FINAL, record, sizeof record)) {
            return 0;
        }
        uint32_t length = builder_get16(record + 4);
        total += length > 0 ? length + 1 : 0;
    }
    const GroupTable *tables[3] = {&prepared->artists, &prepared->album_artists, &prepared->genres};
    for (int table = 0; table < 3; table++) {
        for (uint32_t group = 0; group < tables[table]->count; group++) {
            uint8_t entry[DISPLAY_SIZE];
            if (!read_region(encode, tables[table]->display, group * DISPLAY_SIZE, entry,
                             sizeof entry)) {
                return 0;
            }
            uint32_t length = builder_get16(entry + 4);
            total += length > 0 ? length + 1 : 0;
        }
    }
    for (uint32_t folder = 0; folder < state->folder_count; folder++) {
        uint8_t record[FOLDER_FINAL];
        if (!read_region(encode, prepared->folders_final, folder * FOLDER_FINAL, record,
                         sizeof record)) {
            return 0;
        }
        uint32_t length = builder_get16(record + 4);
        total += length > 0 ? length + 1 : 0;
    }
    for (uint32_t playlist = 0; playlist < prepared->playlist_count; playlist++) {
        uint32_t length = prepared->playlists[playlist].length;
        total += length > 0 ? length + 1 : 0;
    }
    return total;
}

static void plan(Encode *encode, Layout *layout, uint32_t strings)
{
    const Prepared *prepared = encode->prepared;
    const BuilderState *state = encode->state;
    uint32_t links = prepared->artists.link_count + prepared->album_artists.link_count +
                     prepared->genres.link_count + prepared->folder_link_count +
                     prepared->playlist_link_count;
    const uint32_t counts[SECTION_COUNT] = {state->track_count,
                                            prepared->album_count,
                                            prepared->artists.count,
                                            prepared->album_artists.count,
                                            prepared->genres.count,
                                            state->folder_count,
                                            state->track_count,
                                            prepared->playlist_count,
                                            links,
                                            strings};
    const uint16_t narrow[SECTION_COUNT] = {
        TRACK_RECORD,  ALBUM_RECORD, GROUP_RECORD, GROUP_RECORD, GROUP_RECORD,
        FOLDER_RECORD, INDEX_RECORD, GROUP_RECORD, INDEX_RECORD, 1};
    const uint16_t wide[SECTION_COUNT] = {
        WIDE_TRACK_RECORD,  WIDE_ALBUM_RECORD, GROUP_RECORD, GROUP_RECORD,      GROUP_RECORD,
        WIDE_FOLDER_RECORD, WIDE_INDEX_RECORD, GROUP_RECORD, WIDE_INDEX_RECORD, 1};
    const uint16_t *sizes = encode->wide ? wide : narrow;
    const uint32_t music[SECTION_COUNT] = {prepared->music_tracks,
                                           prepared->music_albums,
                                           prepared->music_ranks[BUILDER_ARTIST],
                                           prepared->music_ranks[BUILDER_ALBUM_ARTIST],
                                           prepared->music_ranks[BUILDER_GENRE],
                                           0,
                                           prepared->music_tracks,
                                           0,
                                           0,
                                           0};
    uint32_t offset = encode->wide ? WIDE_HEADER_SIZE : HEADER_SIZE;
    for (int section = 0; section < SECTION_COUNT; section++) {
        offset = (offset + ALIGNMENT - 1) & ~(uint32_t)(ALIGNMENT - 1);
        layout->offset[section] = offset;
        layout->count[section] = counts[section];
        layout->record_size[section] = sizes[section];
        layout->music[section] = music[section];
        offset += counts[section] * sizes[section];
    }
    layout->ids = (offset + ALIGNMENT - 1) & ~(uint32_t)(ALIGNMENT - 1);
    uint32_t ids_end = layout->ids + state->track_count * ID_RECORD;
    layout->album_keys = (ids_end + ALIGNMENT - 1) & ~(uint32_t)(ALIGNMENT - 1);
    layout->album_ranks = layout->album_keys + prepared->album_count * ID_RECORD;
    layout->end =
        encode->wide ? layout->album_ranks + prepared->album_count * WIDE_INDEX_RECORD : offset;
}

static void put_extra(Writer *writer, uint32_t offset, uint32_t count, uint32_t size)
{
    put_u32(writer, offset);
    put_u32(writer, count);
    put_u16(writer, size);
    put_u16(writer, 0);
    put_u32(writer, 0);
}

static void write_header(Writer *writer, const Layout *layout, bool wide, uint32_t tracks,
                         uint32_t albums)
{
    put_u32(writer, 0);
    put_u16(writer, wide ? WIDE_VERSION : VERSION);
    put_u16(writer, wide ? WIDE_SECTION_COUNT : SECTION_COUNT);
    for (int section = 0; section < SECTION_COUNT; section++) {
        put_u32(writer, layout->offset[section]);
        put_u32(writer, layout->count[section]);
        put_u16(writer, layout->record_size[section]);
        if (wide) {
            put_u16(writer, 0);
            put_u32(writer, layout->music[section]);
        } else {
            put_u16(writer, layout->music[section]);
        }
    }
    if (wide) {
        put_extra(writer, layout->ids, tracks, ID_RECORD);
        put_extra(writer, layout->album_keys, albums, ID_RECORD);
        put_extra(writer, layout->album_ranks, albums, WIDE_INDEX_RECORD);
    }
    pad_to(writer, wide ? WIDE_HEADER_SIZE : HEADER_SIZE);
}

typedef struct Placed {
    uint32_t title;
    uint32_t title_length;
    uint32_t file;
    uint32_t file_length;
    uint32_t folder;
    uint32_t album;
    uint32_t artist;
    uint32_t genre;
    uint32_t folder_index;
    uint32_t path_length;
    uint32_t duration_ms;
    uint8_t disc;
    uint8_t number;
} Placed;

static void placed_encode(const Placed *placed, uint8_t *bytes)
{
    builder_put32(bytes, placed->title);
    builder_put16(bytes + 4, placed->title_length);
    builder_put32(bytes + 6, placed->file);
    builder_put16(bytes + 10, placed->file_length);
    builder_put32(bytes + 12, placed->folder);
    builder_put32(bytes + 16, placed->album);
    builder_put32(bytes + 20, placed->artist);
    builder_put32(bytes + 24, placed->genre);
    builder_put32(bytes + 28, placed->folder_index);
    builder_put16(bytes + 32, placed->path_length);
    builder_put32(bytes + 34, placed->duration_ms);
    bytes[38] = placed->disc;
    bytes[39] = placed->number;
}

static void placed_decode(const uint8_t *bytes, Placed *placed)
{
    *placed = (Placed){.title = builder_get32(bytes),
                       .title_length = builder_get16(bytes + 4),
                       .file = builder_get32(bytes + 6),
                       .file_length = builder_get16(bytes + 10),
                       .folder = builder_get32(bytes + 12),
                       .album = builder_get32(bytes + 16),
                       .artist = builder_get32(bytes + 20),
                       .genre = builder_get32(bytes + 24),
                       .folder_index = builder_get32(bytes + 28),
                       .path_length = builder_get16(bytes + 32),
                       .duration_ms = builder_get32(bytes + 34),
                       .disc = bytes[38],
                       .number = bytes[39]};
}

static bool open_in(BuilderState *state, In *in, Region region, uint32_t length)
{
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (buffer == NULL) {
        return false;
    }
    in_open(in, &state->store, region.offset, region.offset + length, buffer, STREAM_BUFFER);
    return true;
}

static bool get_u32(In *in, uint32_t *value)
{
    uint8_t bytes[4];
    if (!in_get(in, bytes, sizeof bytes)) {
        return false;
    }
    *value = builder_get32(bytes);
    return true;
}

static bool cached_u32(Cache *cache, Region column, uint32_t index, uint32_t *value)
{
    uint8_t bytes[4];
    if (!cache_read(cache, column.offset + index * 4, bytes, sizeof bytes)) {
        return false;
    }
    *value = builder_get32(bytes);
    return true;
}

typedef struct PlaceInputs {
    In rows;
    In artists;
    In genres;
    In places;
    Cache folder_index;
    Cache folder_path;
    Out pairs;
} PlaceInputs;

static bool open_place_inputs(Encode *encode, PlaceInputs *inputs, Region pairs)
{
    BuilderState *state = encode->state;
    Prepared *prepared = encode->prepared;
    uint32_t count = state->track_count;
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (buffer == NULL || !open_in(state, &inputs->rows, state->rows, count * BUILDER_ROW_SIZE) ||
        !open_in(state, &inputs->artists, prepared->columns[BUILDER_ARTIST], count * 4) ||
        !open_in(state, &inputs->genres, prepared->columns[BUILDER_GENRE], count * 4) ||
        !open_in(state, &inputs->places, prepared->track_place, count * PLACE_SIZE) ||
        !cache_init(&inputs->folder_index, &state->store, &state->arena, COLUMN_BLOCK,
                    COLUMN_BLOCKS) ||
        !cache_init(&inputs->folder_path, &state->store, &state->arena, COLUMN_BLOCK,
                    COLUMN_BLOCKS)) {
        return false;
    }
    out_open(&inputs->pairs, &state->store, pairs, buffer, STREAM_BUFFER);
    return true;
}

static bool place_track(Encode *encode, PlaceInputs *inputs)
{
    Prepared *prepared = encode->prepared;
    uint8_t bytes[BUILDER_ROW_SIZE];
    uint8_t place[PLACE_SIZE];
    TrackRow row;
    Placed placed;
    uint32_t folder_path = 0;
    if (!in_get(&inputs->rows, bytes, sizeof bytes) || !get_u32(&inputs->artists, &placed.artist) ||
        !get_u32(&inputs->genres, &placed.genre) || !in_get(&inputs->places, place, sizeof place)) {
        return false;
    }
    builder_row_decode(bytes, &row);
    if (!cached_u32(&inputs->folder_index, prepared->folder_new_index, row.folder,
                    &placed.folder_index) ||
        !cached_u32(&inputs->folder_path, prepared->folder_path_length, row.folder, &folder_path)) {
        return false;
    }
    placed.title = row.text[BUILDER_TITLE];
    placed.title_length = row.length[BUILDER_TITLE];
    placed.file = row.text[BUILDER_FILE];
    placed.file_length = row.length[BUILDER_FILE];
    placed.folder = row.folder;
    placed.album = builder_get32(place + 4);
    placed.path_length = folder_path + row.length[BUILDER_FILE];
    placed.duration_ms = row.duration_ms;
    placed.disc = row.disc;
    placed.number = row.number;
    uint8_t value[PLACED_SIZE];
    placed_encode(&placed, value);
    return builder_put_entry(&inputs->pairs, builder_get32(place), value, sizeof value);
}

static bool place_rows(Encode *encode)
{
    BuilderState *state = encode->state;
    uint32_t count = state->track_count;
    uint32_t mark = state->arena.used;
    encode->placed = store_take(&state->store, count * PLACED_SIZE + 4);
    Region pairs = store_take(&state->store, count * (BUILDER_ENTRY_HEADER + PLACED_SIZE) + 4);
    PlaceInputs inputs;
    if (state->store.failed || !open_place_inputs(encode, &inputs, pairs)) {
        return false;
    }
    for (uint32_t track = 0; track < count; track++) {
        if (!place_track(encode, &inputs)) {
            return false;
        }
    }
    pairs.size = count * (BUILDER_ENTRY_HEADER + PLACED_SIZE);
    bool closed = out_close(&inputs.pairs);
    state->arena.used = mark;
    bool placed =
        closed && builder_scatter_values(state, pairs, encode->placed, count, PLACED_SIZE);
    state->store.end = (encode->placed.offset + encode->placed.size + 3) & ~3u;
    return placed;
}

static bool next_placed(In *in, Placed *placed)
{
    uint8_t bytes[PLACED_SIZE];
    if (!in_get(in, bytes, sizeof bytes)) {
        return false;
    }
    placed_decode(bytes, placed);
    return true;
}

static bool write_tracks(Encode *encode)
{
    BuilderState *state = encode->state;
    uint32_t mark = state->arena.used;
    In in;
    if (!open_in(state, &in, encode->placed, state->track_count * PLACED_SIZE)) {
        return false;
    }
    for (uint32_t position = 0; position < state->track_count; position++) {
        Placed placed;
        if (!next_placed(&in, &placed)) {
            return false;
        }
        uint32_t title = take_string(encode, placed.title_length);
        uint32_t path = encode->strings;
        encode->strings += placed.path_length + 1;
        Writer *writer = encode->writer;
        put_u32(writer, title);
        put_u32(writer, path);
        put_index(encode, placed.album);
        put_index(encode, placed.artist);
        put_index(encode, placed.genre);
        put_index(encode, placed.folder_index);
        put_u32(writer, placed.duration_ms);
        put_byte(writer, placed.disc);
        put_byte(writer, placed.number);
        put_u16(writer, 0);
    }
    state->arena.used = mark;
    return true;
}

static bool write_albums(Encode *encode)
{
    Prepared *prepared = encode->prepared;
    for (uint32_t album = 0; album < prepared->album_count; album++) {
        uint8_t record[ALBUM_FINAL];
        if (!read_region(encode, prepared->albums, album * ALBUM_FINAL, record, sizeof record)) {
            return false;
        }
        Writer *writer = encode->writer;
        put_u32(writer, take_string(encode, builder_get16(record + 4)));
        put_index(encode, builder_get32(record + 6));
        put_index(encode, builder_get32(record + 10));
        if (encode->wide) {
            put_u32(writer, builder_get32(record + 16));
            put_u32(writer, builder_get32(record + 20));
            put_u16(writer, builder_get16(record + 14));
            put_u16(writer, 0);
            continue;
        }
        put_u16(writer, builder_get16(record + 14));
        put_u16(writer, builder_get32(record + 16));
        put_u16(writer, builder_get32(record + 20));
        put_u16(writer, 0xFFFF);
    }
    return true;
}

static bool write_groups(Encode *encode, const GroupTable *table, uint32_t first_link)
{
    for (uint32_t group = 0; group < table->count; group++) {
        uint8_t display[DISPLAY_SIZE];
        uint8_t entry[GROUP_ENTRY];
        if (!read_region(encode, table->display, group * DISPLAY_SIZE, display, sizeof display) ||
            !read_region(encode, table->groups, group * GROUP_ENTRY, entry, sizeof entry)) {
            return false;
        }
        Writer *writer = encode->writer;
        put_u32(writer, take_string(encode, builder_get16(display + 4)));
        put_u32(writer, first_link + builder_get32(entry));
        put_index(encode, builder_get32(entry + 4));
        if (!encode->wide) {
            put_u16(writer, 0);
        }
    }
    return true;
}

static bool write_folders(Encode *encode, uint32_t first_link)
{
    for (uint32_t folder = 0; folder < encode->state->folder_count; folder++) {
        uint8_t record[FOLDER_FINAL];
        if (!read_region(encode, encode->prepared->folders_final, folder * FOLDER_FINAL, record,
                         sizeof record)) {
            return false;
        }
        Writer *writer = encode->writer;
        put_u32(writer, take_string(encode, builder_get16(record + 4)));
        put_index(encode, builder_get32(record + 6));
        put_index(encode, builder_get32(record + 10));
        put_u32(writer, first_link + builder_get32(record + 18));
        put_index(encode, builder_get32(record + 14));
        if (!encode->wide) {
            put_u16(writer, 0);
        }
    }
    return true;
}

static bool copy_indices(Encode *encode, Region region, uint32_t first, uint32_t count)
{
    uint8_t *chunk = encode->stream;
    uint32_t done = 0;
    while (done < count) {
        uint32_t step = count - done < STREAM_BUFFER / 4 ? count - done : STREAM_BUFFER / 4;
        if (!read_region(encode, region, (first + done) * 4, chunk, step * 4)) {
            return false;
        }
        for (uint32_t index = 0; index < step; index++) {
            put_index(encode, builder_get32(chunk + 4 * index));
        }
        done += step;
    }
    return true;
}

static void write_playlists(Encode *encode, uint32_t first_link)
{
    const Prepared *prepared = encode->prepared;
    for (uint32_t sorted = 0; sorted < prepared->playlist_count; sorted++) {
        const PlaylistRow *playlist = &prepared->playlists[encode->playlist_order[sorted]];
        Writer *writer = encode->writer;
        put_u32(writer, take_string(encode, playlist->length));
        put_u32(writer, first_link);
        put_index(encode, playlist->count);
        if (!encode->wide) {
            put_u16(writer, 0);
        }
        first_link += playlist->count;
    }
}

static bool write_links(Encode *encode)
{
    const Prepared *prepared = encode->prepared;
    const GroupTable *tables[3] = {&prepared->artists, &prepared->album_artists, &prepared->genres};
    for (int table = 0; table < 3; table++) {
        if (!copy_indices(encode, tables[table]->links, 0, tables[table]->link_count)) {
            return false;
        }
    }
    if (!copy_indices(encode, prepared->folder_links, 0, prepared->folder_link_count)) {
        return false;
    }
    for (uint32_t sorted = 0; sorted < prepared->playlist_count; sorted++) {
        const PlaylistRow *playlist = &prepared->playlists[encode->playlist_order[sorted]];
        if (!copy_indices(encode, prepared->playlist_links, playlist->first_link,
                          playlist->count)) {
            return false;
        }
    }
    return true;
}

static bool write_records(Encode *encode, const Layout *layout)
{
    const Prepared *prepared = encode->prepared;
    Writer *writer = encode->writer;
    encode->strings = 1;
    write_header(writer, layout, encode->wide, encode->state->track_count, prepared->album_count);
    if (!write_tracks(encode)) {
        return false;
    }
    pad_to(writer, layout->offset[SECTION_ALBUMS]);
    if (!write_albums(encode)) {
        return false;
    }
    uint32_t links = 0;
    const GroupTable *tables[3] = {&prepared->artists, &prepared->album_artists, &prepared->genres};
    for (int table = 0; table < 3; table++) {
        pad_to(writer, layout->offset[SECTION_ARTISTS + table]);
        if (!write_groups(encode, tables[table], links)) {
            return false;
        }
        links += tables[table]->link_count;
    }
    pad_to(writer, layout->offset[SECTION_FOLDERS]);
    if (!write_folders(encode, links)) {
        return false;
    }
    links += prepared->folder_link_count;
    pad_to(writer, layout->offset[SECTION_TITLE_ORDER]);
    if (!copy_indices(encode, prepared->title_order, 0, encode->state->track_count)) {
        return false;
    }
    pad_to(writer, layout->offset[SECTION_PLAYLISTS]);
    write_playlists(encode, links);
    pad_to(writer, layout->offset[SECTION_LINKS]);
    if (!write_links(encode)) {
        return false;
    }
    pad_to(writer, layout->offset[SECTION_STRINGS]);
    return true;
}

static bool emit_text(Encode *encode, Cache *cache, uint32_t offset, uint32_t length)
{
    if (length == 0) {
        return true;
    }
    if (!builder_read_text(cache, offset, length, encode->text)) {
        return false;
    }
    put_bytes(encode->writer, encode->text, length);
    put_byte(encode->writer, 0);
    return true;
}

static bool folder_text_for(Encode *encode, uint32_t node)
{
    if (node == encode->folder_text_node) {
        return true;
    }
    BuilderState *state = encode->state;
    Prepared *prepared = encode->prepared;
    FolderRow chain[BUILDER_MAX_DEPTH];
    uint32_t depth = 0;
    for (uint32_t walk = node; walk != 0 && depth < BUILDER_MAX_DEPTH;) {
        if (!builder_read_folder(&prepared->folders, state, walk, &chain[depth])) {
            return false;
        }
        walk = chain[depth++].parent;
    }
    uint32_t used = 0;
    while (depth > 0) {
        const FolderRow *folder = &chain[--depth];
        if (used + folder->length + 1 >= BUILDER_TEXT_CAPACITY ||
            !cache_read(&prepared->heap, folder->name, encode->folder_text + used,
                        folder->length)) {
            return false;
        }
        used += folder->length;
        encode->folder_text[used++] = '/';
    }
    encode->folder_text_node = node;
    encode->folder_text_length = used;
    return true;
}

static bool emit_track_strings(Encode *encode)
{
    BuilderState *state = encode->state;
    Prepared *prepared = encode->prepared;
    uint32_t mark = state->arena.used;
    In in;
    Out ids;
    if (!open_in(state, &in, encode->placed, state->track_count * PLACED_SIZE)) {
        return false;
    }
    if (encode->wide) {
        uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
        encode->ids = store_take(&state->store, state->track_count * ID_ENTRY);
        if (state->arena.failed || state->store.failed) {
            return false;
        }
        encode->ids.size = state->track_count * ID_ENTRY;
        out_open(&ids, &state->store, encode->ids, buffer, STREAM_BUFFER);
    }
    for (uint32_t position = 0; position < state->track_count; position++) {
        Placed placed;
        if (!next_placed(&in, &placed) ||
            !emit_text(encode, &prepared->heap, placed.title, placed.title_length) ||
            !folder_text_for(encode, placed.folder) ||
            !builder_read_text(&prepared->heap, placed.file, placed.file_length, encode->text)) {
            return false;
        }
        put_bytes(encode->writer, encode->folder_text, encode->folder_text_length);
        put_bytes(encode->writer, encode->text, placed.file_length);
        put_byte(encode->writer, 0);
        if (encode->wide) {
            uint64_t id =
                library_path_id(HASH64_START, encode->folder_text, encode->folder_text_length);
            id = library_path_id(id, encode->text, placed.file_length);
            uint8_t entry[ID_ENTRY];
            builder_put16(entry, ID_ENTRY);
            builder_put32(entry + 2, (uint32_t)(id >> 32));
            builder_put32(entry + 6, (uint32_t)id);
            builder_put32(entry + 10, position);
            if (!out_put(&ids, entry, sizeof entry)) {
                return false;
            }
        }
    }
    bool closed = !encode->wide || out_close(&ids);
    state->arena.used = mark;
    return closed;
}

static int by_id_then_track(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    for (uint32_t offset = 2; offset <= 10; offset += 4) {
        uint32_t a = builder_get32(left + offset);
        uint32_t b = builder_get32(right + offset);
        if (a != b) {
            return a < b ? -1 : 1;
        }
    }
    return 0;
}

static bool hash_text(Encode *encode, uint32_t offset, uint32_t length, uint64_t *hash)
{
    if (length > 0 && !builder_read_text(&encode->prepared->heap, offset, length, encode->text)) {
        return false;
    }
    *hash = hash64_update(*hash, encode->text, length);
    return true;
}

static bool album_key(Encode *encode, uint32_t album, uint64_t *key)
{
    Prepared *prepared = encode->prepared;
    uint8_t record[ALBUM_FINAL];
    uint8_t display[DISPLAY_SIZE];
    uint8_t separator = KEY_SEPARATOR;
    *key = HASH64_START;
    if (!read_region(encode, prepared->albums, album * ALBUM_FINAL, record, sizeof record) ||
        !read_region(encode, prepared->album_artists.display,
                     builder_get32(record + 6) * DISPLAY_SIZE, display, sizeof display) ||
        !hash_text(encode, builder_get32(display), builder_get16(display + 4), key)) {
        return false;
    }
    *key = hash64_update(*key, &separator, sizeof separator);
    return hash_text(encode, builder_get32(record), builder_get16(record + 4), key);
}

static bool collect_album_keys(Encode *encode, Region *keys)
{
    BuilderState *state = encode->state;
    uint32_t count = encode->prepared->album_count;
    uint32_t mark = state->arena.used;
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    *keys = store_take(&state->store, count * ID_ENTRY);
    if (state->arena.failed || state->store.failed) {
        return false;
    }
    keys->size = count * ID_ENTRY;
    Out out;
    out_open(&out, &state->store, *keys, buffer, STREAM_BUFFER);
    for (uint32_t album = 0; album < count; album++) {
        uint64_t key = 0;
        uint8_t entry[ID_ENTRY];
        if (!album_key(encode, album, &key)) {
            return false;
        }
        builder_put16(entry, ID_ENTRY);
        builder_put32(entry + 2, (uint32_t)(key >> 32));
        builder_put32(entry + 6, (uint32_t)key);
        builder_put32(entry + 10, album);
        if (!out_put(&out, entry, sizeof entry)) {
            return false;
        }
    }
    bool closed = out_close(&out);
    state->arena.used = mark;
    return closed;
}

static bool write_sorted(Encode *encode, Region rows, uint32_t start, uint32_t count)
{
    BuilderState *state = encode->state;
    Region sorted;
    if (!builder_sort(state, rows, by_id_then_track, &sorted)) {
        return false;
    }
    uint32_t mark = state->arena.used;
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (state->arena.failed) {
        return false;
    }
    In in;
    in_open(&in, &state->store, sorted.offset, sorted.offset + sorted.size, buffer, STREAM_BUFFER);
    pad_to(encode->writer, start);
    for (uint32_t index = 0; index < count; index++) {
        uint8_t entry[ID_ENTRY];
        if (!in_get(&in, entry, sizeof entry)) {
            return false;
        }
        put_u32(encode->writer, builder_get32(entry + 6));
        put_u32(encode->writer, builder_get32(entry + 2));
        put_u32(encode->writer, builder_get32(entry + 10));
    }
    state->arena.used = mark;
    return true;
}

static bool write_album_keys(Encode *encode, const Layout *layout)
{
    BuilderState *state = encode->state;
    uint32_t count = encode->prepared->album_count;
    Region keys;
    Region sorted;
    if (!collect_album_keys(encode, &keys) ||
        !builder_sort(state, keys, by_id_then_track, &sorted)) {
        return false;
    }
    uint32_t mark = state->arena.used;
    uint8_t *in_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *out_buffer = arena_take(&state->arena, STREAM_BUFFER);
    Region pairs = store_take(&state->store, count * BUILDER_PAIR_SIZE);
    Region ranks = store_take(&state->store, count * WIDE_INDEX_RECORD);
    if (state->arena.failed || state->store.failed) {
        return false;
    }
    In in;
    in_open(&in, &state->store, sorted.offset, sorted.offset + sorted.size, in_buffer,
            STREAM_BUFFER);
    Out out;
    out_open(&out, &state->store, pairs, out_buffer, STREAM_BUFFER);
    pad_to(encode->writer, layout->album_keys);
    for (uint32_t position = 0; position < count; position++) {
        uint8_t entry[ID_ENTRY];
        if (!in_get(&in, entry, sizeof entry) ||
            !builder_put_pair(&out, builder_get32(entry + 10), position)) {
            return false;
        }
        put_u32(encode->writer, builder_get32(entry + 6));
        put_u32(encode->writer, builder_get32(entry + 2));
        put_u32(encode->writer, builder_get32(entry + 10));
    }
    bool closed = out_close(&out);
    state->arena.used = mark;
    if (!closed || !builder_scatter(state, pairs, ranks, count)) {
        return false;
    }
    pad_to(encode->writer, layout->album_ranks);
    return copy_indices(encode, ranks, 0, count);
}

static bool write_ids(Encode *encode, const Layout *layout)
{
    flush(encode->writer);
    return write_sorted(encode, encode->ids, layout->ids, encode->state->track_count) &&
           write_album_keys(encode, layout);
}

static bool emit_names(Encode *encode)
{
    Prepared *prepared = encode->prepared;
    for (uint32_t album = 0; album < prepared->album_count; album++) {
        uint8_t record[ALBUM_FINAL];
        if (!read_region(encode, prepared->albums, album * ALBUM_FINAL, record, sizeof record) ||
            !emit_text(encode, &prepared->heap, builder_get32(record), builder_get16(record + 4))) {
            return false;
        }
    }
    const GroupTable *tables[3] = {&prepared->artists, &prepared->album_artists, &prepared->genres};
    for (int table = 0; table < 3; table++) {
        for (uint32_t group = 0; group < tables[table]->count; group++) {
            uint8_t entry[DISPLAY_SIZE];
            if (!read_region(encode, tables[table]->display, group * DISPLAY_SIZE, entry,
                             sizeof entry) ||
                !emit_text(encode, &prepared->heap, builder_get32(entry),
                           builder_get16(entry + 4))) {
                return false;
            }
        }
    }
    for (uint32_t folder = 0; folder < encode->state->folder_count; folder++) {
        uint8_t record[FOLDER_FINAL];
        if (!read_region(encode, prepared->folders_final, folder * FOLDER_FINAL, record,
                         sizeof record) ||
            !emit_text(encode, &prepared->heap, builder_get32(record), builder_get16(record + 4))) {
            return false;
        }
    }
    for (uint32_t sorted = 0; sorted < prepared->playlist_count; sorted++) {
        const PlaylistRow *playlist = &prepared->playlists[encode->playlist_order[sorted]];
        if (playlist->length == 0) {
            continue;
        }
        if (!read_playlist_name(encode, playlist, encode->text)) {
            return false;
        }
        put_bytes(encode->writer, encode->text, playlist->length);
        put_byte(encode->writer, 0);
    }
    return true;
}

static bool load_columns(Encode *encode)
{
    BuilderState *state = encode->state;
    if (!place_rows(encode)) {
        return false;
    }
    encode->stream = arena_take(&state->arena, STREAM_BUFFER);
    encode->text = arena_take(&state->arena, BUILDER_TEXT_CAPACITY);
    encode->folder_text = arena_take(&state->arena, BUILDER_TEXT_CAPACITY);
    encode->folder_text_node = BUILDER_UNSET;
    encode->folder_text_length = 0;
    return !state->arena.failed;
}

static bool fits_version_3(const BuilderState *state, const Prepared *prepared)
{
    const uint32_t counts[] = {state->track_count,      prepared->album_count,
                               prepared->artists.count, prepared->album_artists.count,
                               prepared->genres.count,  state->folder_count,
                               prepared->playlist_count};
    for (uint32_t index = 0; index < sizeof counts / sizeof counts[0]; index++) {
        if (counts[index] > BUILDER_V3_LIMIT) {
            return false;
        }
    }
    return true;
}

BuilderResult builder_encode(BuilderState *state, Prepared *prepared, ByteSink sink,
                             uint32_t capacity, uint32_t *length, bool wide)
{
    state->arena.used = prepared->index_mark;
    Writer *writer = arena_take(&state->arena, (uint32_t)sizeof(Writer));
    Encode encode = {.state = state, .prepared = prepared, .writer = writer, .wide = wide};
    encode.text = arena_take(&state->arena, BUILDER_TEXT_CAPACITY);
    if (writer == NULL || encode.text == NULL || !order_playlists(&encode) ||
        !load_columns(&encode)) {
        return state->store.io_failed ? BUILDER_WRITE_FAILED
               : state->store.failed  ? BUILDER_NO_ROOM
                                      : BUILDER_OUT_OF_MEMORY;
    }
    if (!wide && !fits_version_3(state, prepared)) {
        return BUILDER_TOO_MANY;
    }
    uint32_t strings = string_total(&encode);
    if (strings == 0) {
        return BUILDER_WRITE_FAILED;
    }
    Layout layout;
    plan(&encode, &layout, strings);
    if (capacity != 0 && layout.end > capacity) {
        return BUILDER_NO_ROOM;
    }
    *writer = (Writer){.sink = sink, .offset = 0, .used = 0, .failed = false};
    if (!write_records(&encode, &layout)) {
        return BUILDER_WRITE_FAILED;
    }
    put_byte(writer, 0);
    if (!emit_track_strings(&encode) || !emit_names(&encode) ||
        (wide && !write_ids(&encode, &layout))) {
        return BUILDER_WRITE_FAILED;
    }
    flush(writer);
    static const uint8_t MAGIC[4] = {'E', 'M', 'D', 'B'};
    static const uint8_t WIDE_MAGIC[4] = {'R', 'L', 'I', 'B'};
    if (writer->failed || !sink.write(sink.context, 0, wide ? WIDE_MAGIC : MAGIC, sizeof MAGIC)) {
        return BUILDER_WRITE_FAILED;
    }
    *length = layout.end;
    return BUILDER_OK;
}
