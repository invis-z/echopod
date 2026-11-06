#include <stddef.h>

#include "builder_internal.h"

enum {
    STREAM_BUFFER = 4096,
    COLUMN_BUFFER = 1024,
    ALBUM_TUPLE = 30,
    ALBUM_PRE = 26,
    ORDER_TUPLE = 18 + ALBUM_PRE,
    PLACE_SIZE = 16,
    ALBUM_FINAL = 28,
    GROUP_TUPLE = 16,
    GROUP_ENTRY = 8,
    LINK_TUPLE = 18,
    FOLDER_FINAL = 24,
    TITLE_TUPLE = 14,
    KIND_CHILD = 0,
    KIND_TRACK = 1,
    FOLDER_CACHE_BLOCKS = 4
};

static int compare_u16(const uint8_t *left, const uint8_t *right, uint32_t offset)
{
    uint32_t a = builder_get16(left + offset);
    uint32_t b = builder_get16(right + offset);
    return a < b ? -1 : a > b ? 1 : 0;
}

static int compare_u32(const uint8_t *left, const uint8_t *right, uint32_t offset)
{
    uint32_t a = builder_get32(left + offset);
    uint32_t b = builder_get32(right + offset);
    return a < b ? -1 : a > b ? 1 : 0;
}

static int compare_u8(const uint8_t *left, const uint8_t *right, uint32_t offset)
{
    return (int)left[offset] - (int)right[offset];
}

typedef struct Column {
    In in;
} Column;

static bool open_column(BuilderState *state, Column *column, Region region, uint32_t count)
{
    uint8_t *buffer = arena_take(&state->arena, COLUMN_BUFFER);
    if (buffer == NULL) {
        return false;
    }
    in_open(&column->in, &state->store, region.offset, region.offset + count * 4, buffer,
            COLUMN_BUFFER);
    return true;
}

static bool next_value(Column *column, uint32_t *value)
{
    uint8_t bytes[4];
    if (!in_get(&column->in, bytes, sizeof bytes)) {
        return false;
    }
    *value = builder_get32(bytes);
    return true;
}

static int by_album_key(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    int artist = compare_u32(left, right, 2);
    return artist != 0 ? artist : compare_u32(left, right, 6);
}

static int by_album_genre(const uint8_t *left, const uint8_t *right, const void *context)
{
    int album = by_album_key(left, right, context);
    return album != 0 ? album : compare_u32(left, right, 14);
}

static int by_album_track(const uint8_t *left, const uint8_t *right, const void *context)
{
    int album = by_album_key(left, right, context);
    if (album != 0) {
        return album;
    }
    int disc = compare_u8(left, right, 24);
    if (disc != 0) {
        return disc;
    }
    int number = compare_u8(left, right, 25);
    return number != 0 ? number : compare_u32(left, right, 20);
}

static bool emit_album_tuples(BuilderState *state, Prepared *prepared, Region *input)
{
    uint32_t mark = state->arena.used;
    Column album_artist;
    Column album;
    Column genre;
    Column genre_key;
    Column title;
    uint8_t *row_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *tail_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint32_t count = state->track_count;
    Tail tail;
    if (row_buffer == NULL || tail_buffer == NULL ||
        !open_column(state, &album_artist, prepared->columns[BUILDER_ALBUM_ARTIST], count) ||
        !open_column(state, &album, prepared->columns[BUILDER_ALBUM], count) ||
        !open_column(state, &genre, prepared->genre_spelling, count) ||
        !open_column(state, &genre_key, prepared->columns[BUILDER_GENRE], count) ||
        !open_column(state, &title, prepared->columns[BUILDER_TITLE], count) ||
        !builder_open_tail(state, &tail, tail_buffer)) {
        return false;
    }
    In rows;
    in_open(&rows, &state->store, state->rows.offset, state->rows.offset + count * BUILDER_ROW_SIZE,
            row_buffer, STREAM_BUFFER);
    for (uint32_t track = 0; track < count; track++) {
        uint8_t bytes[BUILDER_ROW_SIZE];
        TrackRow row;
        uint32_t values[5];
        if (!in_get(&rows, bytes, sizeof bytes) || !next_value(&album_artist, &values[0]) ||
            !next_value(&album, &values[1]) || !next_value(&genre, &values[2]) ||
            !next_value(&title, &values[3]) || !next_value(&genre_key, &values[4])) {
            return false;
        }
        builder_row_decode(bytes, &row);
        uint8_t tuple[ALBUM_TUPLE];
        builder_put16(tuple, ALBUM_TUPLE);
        builder_put32(tuple + 2, values[0]);
        builder_put32(tuple + 6, values[1]);
        builder_put32(tuple + 10, track);
        builder_put32(tuple + 14, values[2]);
        builder_put16(tuple + 18, row.year);
        builder_put32(tuple + 20, values[3]);
        tuple[24] = row.disc;
        tuple[25] = row.number;
        builder_put32(tuple + 26, values[4]);
        if (!out_put(&tail.out, tuple, sizeof tuple)) {
            return false;
        }
    }
    bool closed = builder_close_tail(state, &tail, input);
    state->arena.used = mark;
    return closed;
}

static bool album_genres(BuilderState *state, Region sorted, Region genres, uint32_t *albums)
{
    uint32_t mark = state->arena.used;
    Stream stream;
    uint8_t *previous = arena_take(&state->arena, ALBUM_TUPLE);
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (previous == NULL || buffer == NULL || !builder_open_stream(state, &stream, sorted) ||
        !builder_pull(&stream)) {
        return false;
    }
    Out out;
    out_open(&out, &state->store, genres, buffer, STREAM_BUFFER);
    uint32_t album = 0;
    while (stream.live) {
        for (uint32_t index = 0; index < ALBUM_TUPLE; index++) {
            previous[index] = stream.record[index];
        }
        uint32_t best_genre = builder_get32(previous + 26);
        uint32_t best_count = 0;
        uint32_t best_first = BUILDER_UNSET;
        while (stream.live && by_album_key(previous, stream.record, NULL) == 0) {
            uint32_t genre = builder_get32(stream.record + 14);
            uint32_t genre_key = builder_get32(stream.record + 26);
            uint32_t first = builder_get32(stream.record + 10);
            uint32_t occurrences = 0;
            while (stream.live && by_album_key(previous, stream.record, NULL) == 0 &&
                   builder_get32(stream.record + 14) == genre) {
                occurrences++;
                if (!builder_pull(&stream)) {
                    return false;
                }
            }
            if (occurrences > best_count || (occurrences == best_count && first < best_first)) {
                best_count = occurrences;
                best_first = first;
                best_genre = genre_key;
            }
        }
        uint8_t value[4];
        builder_put32(value, best_genre);
        if (!out_put(&out, value, sizeof value)) {
            return false;
        }
        album++;
    }
    *albums = album;
    bool closed = out_close(&out);
    state->arena.used = mark;
    return closed && !state->store.io_failed;
}

static bool write_album_rows(BuilderState *state, Region sorted, Region genres, uint32_t count,
                             Region albums)
{
    uint32_t mark = state->arena.used;
    Stream stream;
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *genre_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *previous = arena_take(&state->arena, ALBUM_TUPLE);
    if (buffer == NULL || genre_buffer == NULL || previous == NULL ||
        !builder_open_stream(state, &stream, sorted) || !builder_pull(&stream)) {
        return false;
    }
    Out out;
    out_open(&out, &state->store, albums, buffer, STREAM_BUFFER);
    In genre_of_album;
    in_open(&genre_of_album, &state->store, genres.offset, genres.offset + count * 4, genre_buffer,
            STREAM_BUFFER);
    uint32_t member_start = 0;
    while (stream.live) {
        for (uint32_t index = 0; index < ALBUM_TUPLE; index++) {
            previous[index] = stream.record[index];
        }
        uint32_t members = 0;
        uint32_t year = 0;
        while (stream.live && by_album_key(previous, stream.record, NULL) == 0) {
            uint32_t candidate = builder_get16(stream.record + 18);
            if (candidate != 0 && (year == 0 || candidate < year)) {
                year = candidate;
            }
            members++;
            if (!builder_pull(&stream)) {
                return false;
            }
        }
        uint8_t genre[4];
        if (!in_get(&genre_of_album, genre, sizeof genre)) {
            return false;
        }
        uint8_t row[ALBUM_PRE];
        builder_put32(row, builder_get32(previous + 10));
        builder_put32(row + 4, builder_get32(previous + 2));
        builder_put32(row + 8, builder_get32(previous + 6));
        builder_put16(row + 12, year);
        builder_put32(row + 14, members);
        builder_put32(row + 18, builder_get32(genre));
        builder_put32(row + 22, member_start);
        member_start += members;
        if (!out_put(&out, row, sizeof row)) {
            return false;
        }
    }
    bool closed = out_close(&out);
    state->arena.used = mark;
    return closed;
}

static bool write_members(BuilderState *state, Region sorted, Region members)
{
    uint32_t mark = state->arena.used;
    Stream stream;
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (buffer == NULL || !builder_open_stream(state, &stream, sorted) || !builder_pull(&stream)) {
        return false;
    }
    Out out;
    out_open(&out, &state->store, members, buffer, STREAM_BUFFER);
    while (stream.live) {
        if (!out_put(&out, stream.record + 10, 4) || !builder_pull(&stream)) {
            return false;
        }
    }
    bool closed = out_close(&out);
    state->arena.used = mark;
    return closed;
}

typedef struct AlbumPhase {
    Region pre;
    Region members;
    uint32_t count;
} AlbumPhase;

static bool group_albums(BuilderState *state, Prepared *prepared, AlbumPhase *albums)
{
    uint32_t count = state->track_count;
    albums->pre = store_take(&state->store, count * ALBUM_PRE + ALBUM_PRE);
    albums->members = store_take(&state->store, count * 4 + 4);
    if (state->store.failed) {
        return false;
    }
    uint32_t arena_mark = state->arena.used;
    uint32_t store_mark = state->store.end;
    Region genres = store_take(&state->store, count * 4 + 4);
    Region input;
    Region sorted;
    if (state->store.failed || !emit_album_tuples(state, prepared, &input) ||
        !builder_sort(state, input, by_album_genre, &sorted) ||
        !album_genres(state, sorted, genres, &albums->count)) {
        return false;
    }
    state->store.end = (input.offset + input.size + 3) & ~3u;
    if (!builder_sort(state, input, by_album_key, &sorted) ||
        !write_album_rows(state, sorted, genres, albums->count, albums->pre)) {
        return false;
    }
    state->store.end = (input.offset + input.size + 3) & ~3u;
    if (!builder_sort(state, input, by_album_track, &sorted) ||
        !write_members(state, sorted, albums->members)) {
        return false;
    }
    state->arena.used = arena_mark;
    state->store.end = store_mark;
    return true;
}

static int by_album_order(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    int title = compare_u32(left, right, 2);
    if (title != 0) {
        return title;
    }
    int artist = compare_u32(left, right, 6);
    return artist != 0 ? artist : compare_u32(left, right, 10);
}

static bool order_albums(BuilderState *state, const AlbumPhase *albums, Region *sorted)
{
    uint32_t arena_mark = state->arena.used;
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *pre_buffer = arena_take(&state->arena, STREAM_BUFFER);
    Tail tail;
    if (buffer == NULL || pre_buffer == NULL || !builder_open_tail(state, &tail, buffer)) {
        return false;
    }
    In pre;
    in_open(&pre, &state->store, albums->pre.offset, albums->pre.offset + albums->count * ALBUM_PRE,
            pre_buffer, STREAM_BUFFER);
    for (uint32_t album = 0; album < albums->count; album++) {
        uint8_t tuple[ORDER_TUPLE];
        if (!in_get(&pre, tuple + 18, ALBUM_PRE)) {
            return false;
        }
        builder_put16(tuple, ORDER_TUPLE);
        builder_put32(tuple + 2, builder_get32(tuple + 18 + 8));
        builder_put32(tuple + 6, builder_get32(tuple + 18 + 4));
        builder_put32(tuple + 10, builder_get32(tuple + 18));
        builder_put32(tuple + 14, album);
        if (!out_put(&tail.out, tuple, sizeof tuple)) {
            return false;
        }
    }
    Region input;
    if (!builder_close_tail(state, &tail, &input)) {
        return false;
    }
    state->arena.used = arena_mark;
    return builder_sort(state, input, by_album_order, sorted);
}

typedef struct Placing {
    Out albums;
    Out final_tracks;
    Out track_album;
    Out places;
    Region place_pairs;
} Placing;

static bool open_placing(BuilderState *state, Prepared *prepared, Placing *placing)
{
    uint32_t count = state->track_count;
    placing->place_pairs =
        store_take(&state->store, count * (BUILDER_ENTRY_HEADER + PLACE_SIZE) + 4);
    uint8_t *buffers[4];
    for (int index = 0; index < 4; index++) {
        buffers[index] = arena_take(&state->arena, STREAM_BUFFER);
    }
    if (state->store.failed || state->arena.failed) {
        return false;
    }
    out_open(&placing->albums, &state->store, prepared->albums, buffers[0], STREAM_BUFFER);
    out_open(&placing->final_tracks, &state->store, prepared->final_tracks, buffers[1],
             STREAM_BUFFER);
    out_open(&placing->track_album, &state->store, prepared->track_album, buffers[2],
             STREAM_BUFFER);
    out_open(&placing->places, &state->store, placing->place_pairs, buffers[3], STREAM_BUFFER);
    return true;
}

static bool place_member(Placing *placing, uint32_t track, uint32_t position, uint32_t album,
                         const uint8_t *pre)
{
    uint8_t value[4];
    uint8_t place[PLACE_SIZE];
    builder_put32(place, position);
    builder_put32(place + 4, album);
    builder_put32(place + 8, builder_get16(pre + 12));
    builder_put32(place + 12, builder_get32(pre + 8));
    builder_put32(value, track);
    if (!out_put(&placing->final_tracks, value, sizeof value)) {
        return false;
    }
    builder_put32(value, album);
    return out_put(&placing->track_album, value, sizeof value) &&
           builder_put_entry(&placing->places, track, place, sizeof place);
}

static bool place_album(BuilderState *state, Prepared *prepared, const AlbumPhase *albums,
                        Placing *placing, uint32_t final, const uint8_t *pre, uint32_t *position,
                        uint8_t *members)
{
    uint32_t first_seen = builder_get32(pre);
    uint32_t member_count = builder_get32(pre + 14);
    uint32_t member_start = builder_get32(pre + 22);
    uint32_t first_track = *position;
    for (uint32_t done = 0; done < member_count;) {
        uint32_t step =
            member_count - done < STREAM_BUFFER / 4 ? member_count - done : STREAM_BUFFER / 4;
        if (!store_read(&state->store, albums->members.offset + (member_start + done) * 4, members,
                        step * 4)) {
            return false;
        }
        for (uint32_t index = 0; index < step; index++) {
            if (!place_member(placing, builder_get32(members + 4 * index), (*position)++, final,
                              pre)) {
                return false;
            }
        }
        done += step;
    }
    if (builder_get32(pre + 8) < prepared->music_ranks[BUILDER_ALBUM]) {
        prepared->music_albums = final + 1;
        prepared->music_tracks = *position;
    }
    TrackRow row;
    if (!builder_read_row(&prepared->rows, state, first_seen, &row)) {
        return false;
    }
    uint8_t record[ALBUM_FINAL];
    builder_put32(record, row.text[BUILDER_ALBUM]);
    builder_put16(record + 4, row.length[BUILDER_ALBUM]);
    builder_put32(record + 6, builder_get32(pre + 4));
    builder_put32(record + 10, builder_get32(pre + 18));
    builder_put16(record + 14, builder_get16(pre + 12));
    builder_put32(record + 16, first_track);
    builder_put32(record + 20, member_count);
    builder_put32(record + 24, builder_get32(pre + 8));
    return out_put(&placing->albums, record, sizeof record);
}

static bool place_tracks(BuilderState *state, Prepared *prepared, const AlbumPhase *albums,
                         Region order)
{
    uint32_t mark = state->arena.used;
    Placing placing;
    Stream stream;
    uint8_t *members = arena_take(&state->arena, STREAM_BUFFER);
    if (members == NULL || !open_placing(state, prepared, &placing) ||
        !builder_open_stream(state, &stream, order) || !builder_pull(&stream)) {
        return false;
    }
    uint32_t position = 0;
    prepared->music_albums = 0;
    prepared->music_tracks = 0;
    for (uint32_t final = 0; stream.live; final++) {
        if (!place_album(state, prepared, albums, &placing, final, stream.record + 18, &position,
                         members) ||
            !builder_pull(&stream)) {
            return false;
        }
    }
    bool done = out_close(&placing.albums) && out_close(&placing.final_tracks) &&
                out_close(&placing.track_album) && out_close(&placing.places);
    placing.place_pairs.size = state->track_count * (BUILDER_ENTRY_HEADER + PLACE_SIZE);
    state->arena.used = mark;
    done = done && builder_scatter_values(state, placing.place_pairs, prepared->track_place,
                                          state->track_count, PLACE_SIZE);
    return done;
}

static bool build_albums(BuilderState *state, Prepared *prepared)
{
    uint32_t count = state->track_count;
    prepared->final_tracks = store_take(&state->store, count * 4 + 4);
    prepared->track_album = store_take(&state->store, count * 4 + 4);
    prepared->track_place = store_take(&state->store, count * PLACE_SIZE + PLACE_SIZE);
    prepared->albums = store_take(&state->store, count * ALBUM_FINAL + ALBUM_FINAL);
    if (state->store.failed) {
        return false;
    }
    uint32_t arena_mark = state->arena.used;
    uint32_t store_mark = state->store.end;
    AlbumPhase albums = {.count = 0};
    Region order;
    if (!group_albums(state, prepared, &albums) || !order_albums(state, &albums, &order) ||
        !place_tracks(state, prepared, &albums, order)) {
        return false;
    }
    prepared->album_count = albums.count;
    state->arena.used = arena_mark;
    state->store.end = store_mark;
    return true;
}

typedef struct TrackPlaces {
    In in;
    uint32_t position;
    uint32_t album;
    uint32_t year;
    uint32_t title;
} TrackPlaces;

static bool open_places(BuilderState *state, Prepared *prepared, TrackPlaces *places)
{
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (buffer == NULL) {
        return false;
    }
    in_open(&places->in, &state->store, prepared->track_place.offset,
            prepared->track_place.offset + state->track_count * PLACE_SIZE, buffer, STREAM_BUFFER);
    return true;
}

static bool next_place(TrackPlaces *places)
{
    uint8_t place[PLACE_SIZE];
    if (!in_get(&places->in, place, sizeof place)) {
        return false;
    }
    places->position = builder_get32(place);
    places->album = builder_get32(place + 4);
    places->year = builder_get32(place + 8);
    places->title = builder_get32(place + 12);
    return true;
}

static int by_group_chronology(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    int group = compare_u32(left, right, 2);
    if (group != 0) {
        return group;
    }
    int year = compare_u16(left, right, 6);
    if (year != 0) {
        return year;
    }
    int title = compare_u32(left, right, 8);
    return title != 0 ? title : compare_u32(left, right, 12);
}

static int by_group_title(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    int group = compare_u32(left, right, 2);
    if (group != 0) {
        return group;
    }
    int title = compare_u32(left, right, 8);
    return title != 0 ? title : compare_u32(left, right, 12);
}

static bool emit_group_tuples(BuilderState *state, Prepared *prepared, int field, Region *input)
{
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    Column groups;
    TrackPlaces places;
    Tail tail;
    if (buffer == NULL ||
        !open_column(state, &groups, prepared->columns[field], state->track_count) ||
        !open_places(state, prepared, &places) || !builder_open_tail(state, &tail, buffer)) {
        return false;
    }
    for (uint32_t track = 0; track < state->track_count; track++) {
        uint32_t group = 0;
        if (!next_value(&groups, &group) || !next_place(&places)) {
            return false;
        }
        uint8_t tuple[GROUP_TUPLE];
        builder_put16(tuple, GROUP_TUPLE);
        builder_put32(tuple + 2, group);
        builder_put16(tuple + 6, places.year);
        builder_put32(tuple + 8, places.title);
        builder_put32(tuple + 12, places.album);
        if (!out_put(&tail.out, tuple, sizeof tuple)) {
            return false;
        }
    }
    return builder_close_tail(state, &tail, input);
}

static bool write_group_links(BuilderState *state, Region sorted, GroupTable *table)
{
    Stream stream;
    uint8_t *link_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *group_buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (link_buffer == NULL || group_buffer == NULL ||
        !builder_open_stream(state, &stream, sorted) || !builder_pull(&stream)) {
        return false;
    }
    Out links;
    Out groups;
    out_open(&links, &state->store, table->links, link_buffer, STREAM_BUFFER);
    out_open(&groups, &state->store, table->groups, group_buffer, STREAM_BUFFER);
    uint32_t total = 0;
    for (uint32_t group = 0; group < table->count; group++) {
        uint32_t first = total;
        uint32_t last_album = BUILDER_UNSET;
        while (stream.live && builder_get32(stream.record + 2) == group) {
            uint32_t album = builder_get32(stream.record + 12);
            if (album != last_album) {
                uint8_t value[4];
                builder_put32(value, album);
                if (!out_put(&links, value, sizeof value)) {
                    return false;
                }
                total++;
                last_album = album;
            }
            if (!builder_pull(&stream)) {
                return false;
            }
        }
        uint8_t entry[GROUP_ENTRY];
        builder_put32(entry, first);
        builder_put32(entry + 4, total - first);
        if (!out_put(&groups, entry, sizeof entry)) {
            return false;
        }
    }
    table->link_count = total;
    return out_close(&links) && out_close(&groups);
}

static bool build_group(BuilderState *state, Prepared *prepared, int field, GroupTable *table,
                        SortCompare order)
{
    table->groups = store_take(&state->store, table->count * GROUP_ENTRY + GROUP_ENTRY);
    table->links = store_take(&state->store, state->track_count * 4 + 4);
    if (state->store.failed) {
        return false;
    }
    uint32_t arena_mark = state->arena.used;
    uint32_t store_mark = state->store.end;
    Region input;
    Region sorted;
    if (!emit_group_tuples(state, prepared, field, &input)) {
        return false;
    }
    state->arena.used = arena_mark;
    if (!builder_sort(state, input, order, &sorted) || !write_group_links(state, sorted, table)) {
        return false;
    }
    state->arena.used = arena_mark;
    state->store.end = store_mark;
    return true;
}

static int by_rank_path(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    uint32_t left_depth = (builder_get16(left) - 6u) / 4;
    uint32_t right_depth = (builder_get16(right) - 6u) / 4;
    uint32_t shorter = left_depth < right_depth ? left_depth : right_depth;
    for (uint32_t level = 0; level < shorter; level++) {
        int order = compare_u32(left, right, 6 + 4 * level);
        if (order != 0) {
            return order;
        }
    }
    return left_depth < right_depth ? -1 : left_depth > right_depth ? 1 : 0;
}

static bool column_at(Cache *cache, Region column, uint32_t index, uint32_t *value)
{
    uint8_t bytes[4];
    if (!cache_read(cache, column.offset + index * 4, bytes, sizeof bytes)) {
        return false;
    }
    *value = builder_get32(bytes);
    return true;
}

static bool emit_folder_paths(BuilderState *state, Prepared *prepared, Region *input)
{
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    Cache order;
    Tail tail;
    if (buffer == NULL ||
        !cache_init(&order, &state->store, &state->arena, COLUMN_BUFFER, FOLDER_CACHE_BLOCKS) ||
        !builder_open_tail(state, &tail, buffer)) {
        return false;
    }
    for (uint32_t node = 0; node < state->folder_count; node++) {
        uint32_t chain[BUILDER_MAX_DEPTH];
        uint32_t depth = 0;
        for (uint32_t walk = node; walk != 0 && depth < BUILDER_MAX_DEPTH;) {
            FolderRow row;
            if (!builder_read_folder(&prepared->folders, state, walk, &row) ||
                !column_at(&order, prepared->folder_order, walk, &chain[depth])) {
                return false;
            }
            depth++;
            walk = row.parent;
        }
        uint8_t record[6 + 4 * BUILDER_MAX_DEPTH];
        builder_put16(record, 6 + 4 * depth);
        builder_put32(record + 2, node);
        for (uint32_t level = 0; level < depth; level++) {
            builder_put32(record + 6 + 4 * level, chain[depth - 1 - level]);
        }
        if (!out_put(&tail.out, record, 6 + 4 * depth)) {
            return false;
        }
    }
    return builder_close_tail(state, &tail, input);
}

static bool number_folders(BuilderState *state, Prepared *prepared, Region node_at)
{
    uint32_t count = state->folder_count;
    uint32_t arena_mark = state->arena.used;
    uint32_t store_mark = state->store.end;
    Region input;
    Region sorted;
    if (!emit_folder_paths(state, prepared, &input)) {
        return false;
    }
    state->arena.used = arena_mark;
    Stream stream;
    Out nodes;
    Out pairs;
    Region new_index_pairs = store_take(&state->store, count * BUILDER_PAIR_SIZE);
    uint8_t *node_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *pair_buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (state->store.failed || node_buffer == NULL || pair_buffer == NULL ||
        !builder_sort(state, input, by_rank_path, &sorted) ||
        !builder_open_stream(state, &stream, sorted) || !builder_pull(&stream)) {
        return false;
    }
    out_open(&nodes, &state->store, node_at, node_buffer, STREAM_BUFFER);
    out_open(&pairs, &state->store, new_index_pairs, pair_buffer, STREAM_BUFFER);
    uint32_t position = 0;
    while (stream.live) {
        uint32_t node = builder_get32(stream.record + 2);
        uint8_t value[4];
        builder_put32(value, node);
        if (!out_put(&nodes, value, sizeof value) || !builder_put_pair(&pairs, node, position) ||
            !builder_pull(&stream)) {
            return false;
        }
        position++;
    }
    bool closed = position == count && out_close(&nodes) && out_close(&pairs);
    state->arena.used = arena_mark;
    bool numbered =
        closed && builder_scatter(state, new_index_pairs, prepared->folder_new_index, count);
    state->store.end = store_mark;
    return numbered;
}

static bool measure_folders(BuilderState *state, Prepared *prepared)
{
    uint32_t mark = state->arena.used;
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (buffer == NULL) {
        return false;
    }
    Out out;
    out_open(&out, &state->store, prepared->folder_path_length, buffer, STREAM_BUFFER);
    for (uint32_t node = 0; node < state->folder_count; node++) {
        uint32_t length = 0;
        for (uint32_t walk = node; walk != 0;) {
            FolderRow row;
            if (!builder_read_folder(&prepared->folders, state, walk, &row)) {
                return false;
            }
            length += row.length + 1u;
            walk = row.parent;
        }
        uint8_t value[4];
        builder_put32(value, length);
        if (!out_put(&out, value, sizeof value)) {
            return false;
        }
    }
    bool closed = out_close(&out);
    state->arena.used = mark;
    return closed;
}

static int by_folder_link(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    int folder = compare_u32(left, right, 2);
    if (folder != 0) {
        return folder;
    }
    for (uint32_t offset = 6; offset <= 8; offset++) {
        int order = compare_u8(left, right, offset);
        if (order != 0) {
            return order;
        }
    }
    int key = compare_u32(left, right, 10);
    return key != 0 ? key : compare_u32(left, right, 14);
}

static bool put_link_tuple(Tail *tail, uint32_t folder, uint32_t kind, uint32_t disc,
                           uint32_t number, uint32_t key, uint32_t value)
{
    uint8_t tuple[LINK_TUPLE];
    builder_put16(tuple, LINK_TUPLE);
    builder_put32(tuple + 2, folder);
    tuple[6] = (uint8_t)kind;
    tuple[7] = (uint8_t)disc;
    tuple[8] = (uint8_t)number;
    tuple[9] = 0;
    builder_put32(tuple + 10, key);
    builder_put32(tuple + 14, value);
    return out_put(&tail->out, tuple, sizeof tuple);
}

static bool emit_child_links(BuilderState *state, Prepared *prepared, Cache *new_index, Tail *tail)
{
    for (uint32_t node = 1; node < state->folder_count; node++) {
        FolderRow row;
        uint32_t child = 0;
        uint32_t parent = 0;
        if (!builder_read_folder(&prepared->folders, state, node, &row) ||
            !column_at(new_index, prepared->folder_new_index, node, &child) ||
            !column_at(new_index, prepared->folder_new_index, row.parent, &parent) ||
            !put_link_tuple(tail, parent, KIND_CHILD, 0, 0, child, child)) {
            return false;
        }
    }
    return true;
}

static bool emit_track_links(BuilderState *state, Prepared *prepared, Cache *new_index, Tail *tail)
{
    uint8_t *row_buffer = arena_take(&state->arena, STREAM_BUFFER);
    Column files;
    TrackPlaces places;
    if (row_buffer == NULL ||
        !open_column(state, &files, prepared->columns[BUILDER_FILE], state->track_count) ||
        !open_places(state, prepared, &places)) {
        return false;
    }
    In rows;
    in_open(&rows, &state->store, state->rows.offset,
            state->rows.offset + state->track_count * BUILDER_ROW_SIZE, row_buffer, STREAM_BUFFER);
    for (uint32_t track = 0; track < state->track_count; track++) {
        uint8_t bytes[BUILDER_ROW_SIZE];
        TrackRow row;
        uint32_t file = 0;
        uint32_t folder = 0;
        if (!in_get(&rows, bytes, sizeof bytes) || !next_value(&files, &file) ||
            !next_place(&places)) {
            return false;
        }
        builder_row_decode(bytes, &row);
        if (!column_at(new_index, prepared->folder_new_index, row.folder, &folder) ||
            !put_link_tuple(tail, folder, KIND_TRACK, row.disc, row.number, file,
                            places.position)) {
            return false;
        }
    }
    return true;
}

static bool emit_folder_links(BuilderState *state, Prepared *prepared, Region *input)
{
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    Cache new_index;
    Tail tail;
    if (buffer == NULL ||
        !cache_init(&new_index, &state->store, &state->arena, COLUMN_BUFFER, FOLDER_CACHE_BLOCKS) ||
        !builder_open_tail(state, &tail, buffer) ||
        !emit_child_links(state, prepared, &new_index, &tail) ||
        !emit_track_links(state, prepared, &new_index, &tail)) {
        return false;
    }
    return builder_close_tail(state, &tail, input);
}

static bool write_folder_rows(BuilderState *state, Prepared *prepared, Region node_at,
                              Region sorted)
{
    Stream stream;
    Cache new_index;
    uint8_t *link_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *row_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *node_buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (link_buffer == NULL || row_buffer == NULL || node_buffer == NULL ||
        !cache_init(&new_index, &state->store, &state->arena, COLUMN_BUFFER, FOLDER_CACHE_BLOCKS) ||
        !builder_open_stream(state, &stream, sorted) || !builder_pull(&stream)) {
        return false;
    }
    Out links;
    Out rows;
    In nodes;
    out_open(&links, &state->store, prepared->folder_links, link_buffer, STREAM_BUFFER);
    out_open(&rows, &state->store, prepared->folders_final, row_buffer, STREAM_BUFFER);
    in_open(&nodes, &state->store, node_at.offset, node_at.offset + state->folder_count * 4,
            node_buffer, STREAM_BUFFER);
    uint32_t total = 0;
    for (uint32_t position = 0; position < state->folder_count; position++) {
        uint32_t first = total;
        uint32_t tracks = 0;
        uint32_t children = 0;
        while (stream.live && builder_get32(stream.record + 2) == position) {
            uint8_t value[4];
            builder_put32(value, builder_get32(stream.record + 14));
            tracks += stream.record[6] == KIND_TRACK;
            children += stream.record[6] == KIND_CHILD;
            total++;
            if (!out_put(&links, value, sizeof value) || !builder_pull(&stream)) {
                return false;
            }
        }
        uint8_t node_bytes[4];
        FolderRow row;
        uint32_t parent = BUILDER_NO_INDEX;
        if (!in_get(&nodes, node_bytes, sizeof node_bytes)) {
            return false;
        }
        uint32_t node = builder_get32(node_bytes);
        if (!builder_read_folder(&prepared->folders, state, node, &row) ||
            (node != 0 &&
             !column_at(&new_index, prepared->folder_new_index, row.parent, &parent))) {
            return false;
        }
        uint8_t record[FOLDER_FINAL];
        builder_put32(record, row.name);
        builder_put16(record + 4, row.length);
        builder_put32(record + 6, parent);
        builder_put32(record + 10, children);
        builder_put32(record + 14, tracks);
        builder_put32(record + 18, first);
        builder_put16(record + 22, 0);
        if (!out_put(&rows, record, sizeof record)) {
            return false;
        }
    }
    prepared->folder_link_count = total;
    return out_close(&links) && out_close(&rows);
}

static bool build_folders(BuilderState *state, Prepared *prepared)
{
    uint32_t count = state->folder_count;
    prepared->folder_new_index = store_take(&state->store, count * 4 + 4);
    prepared->folder_path_length = store_take(&state->store, count * 4 + 4);
    prepared->folders_final = store_take(&state->store, count * FOLDER_FINAL + FOLDER_FINAL);
    prepared->folder_links = store_take(&state->store, (count + state->track_count) * 4 + 4);
    if (state->store.failed) {
        return false;
    }
    uint32_t arena_mark = state->arena.used;
    uint32_t store_mark = state->store.end;
    Region node_at = store_take(&state->store, count * 4 + 4);
    if (state->store.failed || !number_folders(state, prepared, node_at) ||
        !measure_folders(state, prepared)) {
        return false;
    }
    Region input;
    Region sorted;
    if (!emit_folder_links(state, prepared, &input)) {
        return false;
    }
    state->arena.used = arena_mark;
    if (!builder_sort(state, input, by_folder_link, &sorted) ||
        !write_folder_rows(state, prepared, node_at, sorted)) {
        return false;
    }
    state->arena.used = arena_mark;
    state->store.end = store_mark;
    return true;
}

static int by_title_then_artist(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    int title = compare_u32(left, right, 2);
    return title != 0 ? title : compare_u32(left, right, 6);
}

static bool emit_title_tuples(BuilderState *state, Prepared *prepared, Region *input)
{
    uint32_t count = state->track_count;
    Column titles;
    Column artists;
    TrackPlaces places;
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    Tail tail;
    if (buffer == NULL || !open_column(state, &titles, prepared->columns[BUILDER_TITLE], count) ||
        !open_column(state, &artists, prepared->columns[BUILDER_ARTIST], count) ||
        !open_places(state, prepared, &places) || !builder_open_tail(state, &tail, buffer)) {
        return false;
    }
    for (uint32_t track = 0; track < count; track++) {
        uint32_t title = 0;
        uint32_t artist = 0;
        if (!next_value(&titles, &title) || !next_value(&artists, &artist) ||
            !next_place(&places)) {
            return false;
        }
        uint8_t tuple[TITLE_TUPLE];
        builder_put16(tuple, TITLE_TUPLE);
        builder_put32(tuple + 2, title);
        builder_put32(tuple + 6, artist);
        builder_put32(tuple + 10, places.position);
        if (!out_put(&tail.out, tuple, sizeof tuple)) {
            return false;
        }
    }
    return builder_close_tail(state, &tail, input);
}

static int by_title_order(const uint8_t *left, const uint8_t *right, const void *context)
{
    int order = by_title_then_artist(left, right, context);
    return order != 0 ? order : compare_u32(left, right, 10);
}

static bool build_title_order(BuilderState *state, Prepared *prepared)
{
    uint32_t count = state->track_count;
    prepared->title_order = store_take(&state->store, count * 4 + 4);
    if (state->store.failed) {
        return false;
    }
    uint32_t arena_mark = state->arena.used;
    uint32_t store_mark = state->store.end;
    Region input;
    Region sorted;
    if (!emit_title_tuples(state, prepared, &input)) {
        return false;
    }
    state->arena.used = arena_mark;
    if (!builder_sort(state, input, by_title_order, &sorted)) {
        return false;
    }
    Stream stream;
    uint8_t *out_buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (out_buffer == NULL || !builder_open_stream(state, &stream, sorted) ||
        !builder_pull(&stream)) {
        return false;
    }
    Out out;
    out_open(&out, &state->store, prepared->title_order, out_buffer, STREAM_BUFFER);
    while (stream.live) {
        if (!out_put(&out, stream.record + 10, 4) || !builder_pull(&stream)) {
            return false;
        }
    }
    bool closed = out_close(&out);
    state->arena.used = arena_mark;
    state->store.end = store_mark;
    return closed;
}

bool builder_model(BuilderState *state, Prepared *prepared)
{
    return build_albums(state, prepared) &&
           build_group(state, prepared, BUILDER_ARTIST, &prepared->artists, by_group_chronology) &&
           build_group(state, prepared, BUILDER_ALBUM_ARTIST, &prepared->album_artists,
                       by_group_chronology) &&
           build_group(state, prepared, BUILDER_GENRE, &prepared->genres, by_group_title) &&
           build_folders(state, prepared) && build_title_order(state, prepared);
}
