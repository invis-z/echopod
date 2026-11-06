#include <stddef.h>

#include "builder_internal.h"
#include "library/sort_key.h"

enum {
    HEADER_SIZE = 128,
    SECTION_COUNT = 10,
    VERSION = 3,
    ALIGNMENT = 4,
    TRACK_RECORD = 24,
    ALBUM_RECORD = 16,
    GROUP_RECORD = 12,
    FOLDER_RECORD = 16,
    INDEX_RECORD = 2,
    BUFFER_SIZE = 512,
    STREAM_BUFFER = 4096,
    ALBUM_FINAL = 20,
    GROUP_ENTRY = 8,
    DISPLAY_SIZE = 8,
    FOLDER_FINAL = 16,
    NAME_HEADER = 6,
    PLACE_SIZE = 8,
    PLACED_SIZE = 30,
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
    uint16_t music[SECTION_COUNT];
    uint32_t end;
} Layout;

typedef struct Encode {
    BuilderState *state;
    Prepared *prepared;
    Writer *writer;
    Region placed;
    uint16_t *playlist_order;
    uint8_t *stream;
    char *text;
    char *folder_text;
    uint32_t folder_text_node;
    uint32_t folder_text_length;
    uint32_t strings;
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
    const uint16_t sizes[SECTION_COUNT] = {
        TRACK_RECORD,  ALBUM_RECORD, GROUP_RECORD, GROUP_RECORD, GROUP_RECORD,
        FOLDER_RECORD, INDEX_RECORD, GROUP_RECORD, INDEX_RECORD, 1};
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
    uint32_t offset = HEADER_SIZE;
    for (int section = 0; section < SECTION_COUNT; section++) {
        offset = (offset + ALIGNMENT - 1) & ~(uint32_t)(ALIGNMENT - 1);
        layout->offset[section] = offset;
        layout->count[section] = counts[section];
        layout->record_size[section] = sizes[section];
        layout->music[section] = (uint16_t)music[section];
        offset += counts[section] * sizes[section];
    }
    layout->end = offset;
}

static void write_header(Writer *writer, const Layout *layout)
{
    put_u32(writer, 0);
    put_u16(writer, VERSION);
    put_u16(writer, SECTION_COUNT);
    for (int section = 0; section < SECTION_COUNT; section++) {
        put_u32(writer, layout->offset[section]);
        put_u32(writer, layout->count[section]);
        put_u16(writer, layout->record_size[section]);
        put_u16(writer, layout->music[section]);
    }
    pad_to(writer, HEADER_SIZE);
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
    builder_put16(bytes + 12, placed->folder);
    builder_put16(bytes + 14, placed->album);
    builder_put16(bytes + 16, placed->artist);
    builder_put16(bytes + 18, placed->genre);
    builder_put16(bytes + 20, placed->folder_index);
    builder_put16(bytes + 22, placed->path_length);
    builder_put32(bytes + 24, placed->duration_ms);
    bytes[28] = placed->disc;
    bytes[29] = placed->number;
}

static void placed_decode(const uint8_t *bytes, Placed *placed)
{
    *placed = (Placed){.title = builder_get32(bytes),
                       .title_length = builder_get16(bytes + 4),
                       .file = builder_get32(bytes + 6),
                       .file_length = builder_get16(bytes + 10),
                       .folder = builder_get16(bytes + 12),
                       .album = builder_get16(bytes + 14),
                       .artist = builder_get16(bytes + 16),
                       .genre = builder_get16(bytes + 18),
                       .folder_index = builder_get16(bytes + 20),
                       .path_length = builder_get16(bytes + 22),
                       .duration_ms = builder_get32(bytes + 24),
                       .disc = bytes[28],
                       .number = bytes[29]};
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

static bool get_u16(In *in, uint32_t *value)
{
    uint8_t bytes[2];
    if (!in_get(in, bytes, sizeof bytes)) {
        return false;
    }
    *value = builder_get16(bytes);
    return true;
}

static bool cached_u16(Cache *cache, Region column, uint32_t index, uint32_t *value)
{
    uint8_t bytes[2];
    if (!cache_read(cache, column.offset + index * 2, bytes, sizeof bytes)) {
        return false;
    }
    *value = builder_get16(bytes);
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
        !open_in(state, &inputs->artists, prepared->columns[BUILDER_ARTIST], count * 2) ||
        !open_in(state, &inputs->genres, prepared->columns[BUILDER_GENRE], count * 2) ||
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
    if (!in_get(&inputs->rows, bytes, sizeof bytes) || !get_u16(&inputs->artists, &placed.artist) ||
        !get_u16(&inputs->genres, &placed.genre) || !in_get(&inputs->places, place, sizeof place)) {
        return false;
    }
    builder_row_decode(bytes, &row);
    if (!cached_u16(&inputs->folder_index, prepared->folder_new_index, row.folder,
                    &placed.folder_index) ||
        !cached_u16(&inputs->folder_path, prepared->folder_path_length, row.folder, &folder_path)) {
        return false;
    }
    placed.title = row.text[BUILDER_TITLE];
    placed.title_length = row.length[BUILDER_TITLE];
    placed.file = row.text[BUILDER_FILE];
    placed.file_length = row.length[BUILDER_FILE];
    placed.folder = row.folder;
    placed.album = builder_get16(place + 2);
    placed.path_length = folder_path + row.length[BUILDER_FILE];
    placed.duration_ms = row.duration_ms;
    placed.disc = row.disc;
    placed.number = row.number;
    uint8_t value[PLACED_SIZE];
    placed_encode(&placed, value);
    return builder_put_entry(&inputs->pairs, builder_get16(place), value, sizeof value);
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
        put_u16(writer, placed.album);
        put_u16(writer, placed.artist);
        put_u16(writer, placed.genre);
        put_u16(writer, placed.folder_index);
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
        put_u16(writer, builder_get16(record + 6));
        put_u16(writer, builder_get16(record + 8));
        put_u16(writer, builder_get16(record + 10));
        put_u16(writer, builder_get16(record + 12));
        put_u16(writer, builder_get16(record + 14));
        put_u16(writer, BUILDER_NONE);
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
        put_u16(writer, builder_get16(entry + 4));
        put_u16(writer, 0);
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
        put_u16(writer, builder_get16(record + 6));
        put_u16(writer, builder_get16(record + 8));
        put_u32(writer, first_link + builder_get32(record + 12));
        put_u16(writer, builder_get16(record + 10));
        put_u16(writer, 0);
    }
    return true;
}

static bool copy_indices(Encode *encode, Region region, uint32_t first, uint32_t count)
{
    uint8_t *chunk = encode->stream;
    uint32_t done = 0;
    while (done < count) {
        uint32_t step = count - done < STREAM_BUFFER / 2 ? count - done : STREAM_BUFFER / 2;
        if (!read_region(encode, region, (first + done) * 2, chunk, step * 2)) {
            return false;
        }
        for (uint32_t index = 0; index < step; index++) {
            put_u16(encode->writer, builder_get16(chunk + 2 * index));
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
        put_u16(writer, playlist->count);
        put_u16(writer, 0);
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
    write_header(writer, layout);
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
    if (!open_in(state, &in, encode->placed, state->track_count * PLACED_SIZE)) {
        return false;
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
    }
    state->arena.used = mark;
    return true;
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

BuilderResult builder_encode(BuilderState *state, Prepared *prepared, ByteSink sink,
                             uint32_t capacity, uint32_t *length)
{
    state->arena.used = prepared->index_mark;
    Writer *writer = arena_take(&state->arena, (uint32_t)sizeof(Writer));
    Encode encode = {.state = state, .prepared = prepared, .writer = writer};
    encode.text = arena_take(&state->arena, BUILDER_TEXT_CAPACITY);
    if (writer == NULL || encode.text == NULL || !order_playlists(&encode) ||
        !load_columns(&encode)) {
        return state->store.io_failed ? BUILDER_WRITE_FAILED
               : state->store.failed  ? BUILDER_NO_ROOM
                                      : BUILDER_OUT_OF_MEMORY;
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
    if (!emit_track_strings(&encode) || !emit_names(&encode)) {
        return BUILDER_WRITE_FAILED;
    }
    flush(writer);
    static const uint8_t MAGIC[4] = {'E', 'M', 'D', 'B'};
    if (writer->failed || !sink.write(sink.context, 0, MAGIC, sizeof MAGIC)) {
        return BUILDER_WRITE_FAILED;
    }
    *length = layout.end;
    return BUILDER_OK;
}
