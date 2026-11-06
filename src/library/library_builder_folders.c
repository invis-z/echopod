#include <stddef.h>

#include "builder_internal.h"

enum {
    STREAM_BUFFER = 4096,
    CANON_SIZE = 10,
    REMAP_BLOCK = 1024,
    REMAP_BLOCKS = 4
};

static int by_hash_then_node(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    static const uint32_t OFFSETS[2] = {6, 2};
    for (int index = 0; index < 2; index++) {
        uint32_t a = builder_get32(left + OFFSETS[index]);
        uint32_t b = builder_get32(right + OFFSETS[index]);
        if (a != b) {
            return a < b ? -1 : 1;
        }
    }
    uint32_t a = builder_get32(left + 10);
    uint32_t b = builder_get32(right + 10);
    return a < b ? -1 : a > b ? 1 : 0;
}

static int by_canon_then_node(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    for (uint32_t offset = 2; offset <= 6; offset += 4) {
        uint32_t a = builder_get32(left + offset);
        uint32_t b = builder_get32(right + offset);
        if (a != b) {
            return a < b ? -1 : 1;
        }
    }
    return 0;
}

static bool sort_by(BuilderState *state, Region input, SortCompare compare, Region *sorted)
{
    SortJob job = {.store = &state->store,
                   .arena = &state->arena,
                   .input = input,
                   .compare = compare,
                   .context = NULL};
    return sort_records(&job, sorted);
}

static bool same_hash(const uint8_t *left, const uint8_t *right)
{
    return builder_get32(left + 2) == builder_get32(right + 2) &&
           builder_get32(left + 6) == builder_get32(right + 6);
}

static bool put_canon(Out *out, uint32_t canon, uint32_t node)
{
    uint8_t record[CANON_SIZE];
    builder_put16(record, CANON_SIZE);
    builder_put32(record + 2, canon);
    builder_put32(record + 6, node);
    return out_put(out, record, sizeof record);
}

static bool find_canons(BuilderState *state, Region hashes, Region canons)
{
    uint32_t mark = state->arena.used;
    uint8_t *in_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *out_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *record = arena_take(&state->arena, BUILDER_MAX_RECORD);
    uint8_t *first = arena_take(&state->arena, BUILDER_FOLDER_HASH_SIZE);
    if (state->arena.failed) {
        return false;
    }
    In in;
    in_open(&in, &state->store, hashes.offset, hashes.offset + hashes.size, in_buffer,
            STREAM_BUFFER);
    Out out;
    out_open(&out, &state->store, canons, out_buffer, STREAM_BUFFER);
    uint32_t canon = 0;
    bool started = false;
    while (!in_done(&in)) {
        if (!in_record(&in, record)) {
            return false;
        }
        if (!started || !same_hash(first, record)) {
            for (uint32_t index = 0; index < BUILDER_FOLDER_HASH_SIZE; index++) {
                first[index] = record[index];
            }
            canon = builder_get32(record + 10);
            started = true;
        }
        if (!put_canon(&out, canon, builder_get32(record + 10))) {
            return false;
        }
    }
    bool closed = out_close(&out);
    state->arena.used = mark;
    return closed;
}

static bool number_canons(BuilderState *state, Region sorted, Region remap, uint32_t *merged)
{
    uint32_t mark = state->arena.used;
    uint8_t *in_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *out_buffer = arena_take(&state->arena, STREAM_BUFFER);
    Region pairs = store_take(&state->store, state->folder_count * BUILDER_PAIR_SIZE);
    if (state->arena.failed || state->store.failed) {
        return false;
    }
    In in;
    in_open(&in, &state->store, sorted.offset, sorted.offset + sorted.size, in_buffer,
            STREAM_BUFFER);
    Out out;
    out_open(&out, &state->store, pairs, out_buffer, STREAM_BUFFER);
    uint32_t number = 0;
    uint32_t canon = BUILDER_UNSET;
    if (!builder_put_pair(&out, 0, 0)) {
        return false;
    }
    while (!in_done(&in)) {
        uint8_t record[CANON_SIZE];
        if (!in_get(&in, record, sizeof record)) {
            return false;
        }
        if (builder_get32(record + 2) != canon) {
            canon = builder_get32(record + 2);
            number++;
        }
        if (!builder_put_pair(&out, builder_get32(record + 6), number)) {
            return false;
        }
    }
    *merged = number + 1;
    bool closed = out_close(&out);
    state->arena.used = mark;
    bool numbered = closed && builder_scatter(state, pairs, remap, state->folder_count);
    return numbered;
}

static bool remapped(Cache *cache, Region remap, uint32_t node, uint32_t *value)
{
    uint8_t bytes[2];
    if (!cache_read(cache, remap.offset + node * 2, bytes, sizeof bytes)) {
        return false;
    }
    *value = builder_get16(bytes);
    return true;
}

static bool rewrite_folders(BuilderState *state, Region remap, Cache *cache)
{
    uint32_t mark = state->arena.used;
    uint8_t *in_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *out_buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (state->arena.failed) {
        return false;
    }
    In in;
    in_open(&in, &state->store, state->folders.offset,
            state->folders.offset + state->folder_count * BUILDER_FOLDER_SIZE, in_buffer,
            STREAM_BUFFER);
    Out out;
    out_open(&out, &state->store, state->folders, out_buffer, STREAM_BUFFER);
    uint32_t written = 0;
    for (uint32_t node = 0; node < state->folder_count; node++) {
        uint8_t bytes[BUILDER_FOLDER_SIZE];
        FolderRow row;
        uint32_t number = 0;
        if (!in_get(&in, bytes, sizeof bytes) || !remapped(cache, remap, node, &number)) {
            return false;
        }
        if (number != written) {
            continue;
        }
        builder_folder_decode(bytes, &row);
        uint32_t parent = BUILDER_NONE;
        if (node != 0 && !remapped(cache, remap, row.parent, &parent)) {
            return false;
        }
        row.parent = (uint16_t)parent;
        builder_folder_encode(&row, bytes);
        if (!out_put(&out, bytes, sizeof bytes)) {
            return false;
        }
        written++;
    }
    bool closed = out_close(&out);
    state->arena.used = mark;
    return closed;
}

static bool rewrite_rows(BuilderState *state, Region remap, Cache *cache)
{
    uint32_t mark = state->arena.used;
    uint8_t *in_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *out_buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (state->arena.failed) {
        return false;
    }
    In in;
    in_open(&in, &state->store, state->rows.offset,
            state->rows.offset + state->track_count * BUILDER_ROW_SIZE, in_buffer, STREAM_BUFFER);
    Out out;
    out_open(&out, &state->store, state->rows, out_buffer, STREAM_BUFFER);
    for (uint32_t track = 0; track < state->track_count; track++) {
        uint8_t bytes[BUILDER_ROW_SIZE];
        TrackRow row;
        uint32_t folder = 0;
        if (!in_get(&in, bytes, sizeof bytes)) {
            return false;
        }
        builder_row_decode(bytes, &row);
        if (!remapped(cache, remap, row.folder, &folder)) {
            return false;
        }
        row.folder = (uint16_t)folder;
        builder_row_encode(&row, bytes);
        if (!out_put(&out, bytes, sizeof bytes)) {
            return false;
        }
    }
    bool closed = out_close(&out);
    state->arena.used = mark;
    return closed;
}

bool builder_merge_folders(BuilderState *state)
{
    uint32_t arena_mark = state->arena.used;
    uint32_t store_mark = state->store.end;
    uint32_t others = state->folder_count - 1u;
    Region hashes = {.offset = state->folder_hashes.offset,
                     .size = others * BUILDER_FOLDER_HASH_SIZE};
    Region remap = store_take(&state->store, state->folder_count * 2 + 2);
    Region canons = store_take(&state->store, others * CANON_SIZE + CANON_SIZE);
    Region sorted;
    uint32_t merged = 0;
    if (state->store.failed || !sort_by(state, hashes, by_hash_then_node, &sorted) ||
        !find_canons(state, sorted, canons)) {
        return false;
    }
    canons.size = others * CANON_SIZE;
    Cache cache;
    if (!sort_by(state, canons, by_canon_then_node, &sorted) ||
        !number_canons(state, sorted, remap, &merged) ||
        !cache_init(&cache, &state->store, &state->arena, REMAP_BLOCK, REMAP_BLOCKS) ||
        !rewrite_folders(state, remap, &cache) || !rewrite_rows(state, remap, &cache)) {
        return false;
    }
    state->folder_count = merged;
    state->arena.used = arena_mark;
    state->store.end = store_mark;
    return true;
}
