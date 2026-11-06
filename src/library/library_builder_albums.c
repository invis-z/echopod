#include <stddef.h>

#include "builder_internal.h"
#include "library/sort_key.h"

enum {
    STREAM_BUFFER = 4096,
    GUESS_HEADER = 20,
    DECISION_SIZE = 6,
    ASSIGNMENT_SIZE = 12,
    ROW_FLAGS = 45,
    ROW_ALBUM_ARTIST_TEXT = 4 * BUILDER_ALBUM_ARTIST,
    ROW_ALBUM_ARTIST_LENGTH = 24 + 2 * BUILDER_ALBUM_ARTIST
};

static int compare_numbers(uint32_t left, uint32_t right)
{
    return left < right ? -1 : left > right ? 1 : 0;
}

static int compare_bytes(const uint8_t *left, uint32_t left_length, const uint8_t *right,
                         uint32_t right_length)
{
    uint32_t shorter = left_length < right_length ? left_length : right_length;
    for (uint32_t index = 0; index < shorter; index++) {
        if (left[index] != right[index]) {
            return (int)left[index] - (int)right[index];
        }
    }
    return compare_numbers(left_length, right_length);
}

static const uint8_t *album_key(const uint8_t *record, uint32_t *length)
{
    *length = builder_get16(record + 16);
    return record + GUESS_HEADER;
}

static const uint8_t *artist_key(const uint8_t *record, uint32_t *length)
{
    *length = builder_get16(record + 18);
    return record + GUESS_HEADER + builder_get16(record + 16);
}

static int same_album(const uint8_t *left, const uint8_t *right)
{
    int folder = compare_numbers(builder_get32(left + 2), builder_get32(right + 2));
    if (folder != 0) {
        return folder;
    }
    uint32_t left_length = 0;
    uint32_t right_length = 0;
    const uint8_t *left_key = album_key(left, &left_length);
    const uint8_t *right_key = album_key(right, &right_length);
    return compare_bytes(left_key, left_length, right_key, right_length);
}

static int same_artist(const uint8_t *left, const uint8_t *right)
{
    uint32_t left_length = 0;
    uint32_t right_length = 0;
    const uint8_t *left_key = artist_key(left, &left_length);
    const uint8_t *right_key = artist_key(right, &right_length);
    return compare_bytes(left_key, left_length, right_key, right_length);
}

static int by_album_artist_track(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    int album = same_album(left, right);
    if (album != 0) {
        return album;
    }
    int artist = same_artist(left, right);
    return artist != 0 ? artist
                       : compare_numbers(builder_get32(left + 6), builder_get32(right + 6));
}

static int by_track(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    return compare_numbers(builder_get32(left + 2), builder_get32(right + 2));
}

typedef struct KeyBuffers {
    char *text;
    char *album;
    char *artist;
} KeyBuffers;

static bool make_key(Prepared *prepared, const TrackRow *row, int field, char *text, char *key,
                     uint32_t *length)
{
    uint32_t text_length = row->length[field];
    if (text_length > 0 &&
        !builder_read_text(&prepared->heap, row->text[field], text_length, text)) {
        return false;
    }
    text[text_length] = '\0';
    *length = (uint32_t)sort_key_make(text, key, SORT_KEY_CAPACITY);
    return true;
}

static bool emit_guess(Prepared *prepared, Tail *tail, uint32_t track, const TrackRow *row,
                       const KeyBuffers *buffers)
{
    uint32_t album_length = 0;
    uint32_t artist_length = 0;
    if (!make_key(prepared, row, BUILDER_ALBUM, buffers->text, buffers->album, &album_length) ||
        !make_key(prepared, row, BUILDER_ARTIST, buffers->text, buffers->artist, &artist_length)) {
        return false;
    }
    uint8_t header[GUESS_HEADER];
    builder_put16(header, GUESS_HEADER + album_length + artist_length);
    builder_put32(header + 2, row->folder);
    builder_put32(header + 6, track);
    builder_put32(header + 10, row->text[BUILDER_ARTIST]);
    builder_put16(header + 14, row->length[BUILDER_ARTIST]);
    builder_put16(header + 16, album_length);
    builder_put16(header + 18, artist_length);
    return out_put(&tail->out, header, sizeof header) &&
           out_put(&tail->out, buffers->album, album_length) &&
           out_put(&tail->out, buffers->artist, artist_length);
}

static bool emit_guesses(BuilderState *state, Prepared *prepared, Region *guesses, uint32_t *count)
{
    uint32_t mark = state->arena.used;
    uint8_t *row_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *tail_buffer = arena_take(&state->arena, STREAM_BUFFER);
    KeyBuffers buffers = {arena_take(&state->arena, BUILDER_TEXT_CAPACITY + 1),
                          arena_take(&state->arena, SORT_KEY_CAPACITY),
                          arena_take(&state->arena, SORT_KEY_CAPACITY)};
    Tail tail;
    if (row_buffer == NULL || tail_buffer == NULL || buffers.text == NULL ||
        buffers.album == NULL || buffers.artist == NULL ||
        !builder_open_tail(state, &tail, tail_buffer)) {
        return false;
    }
    In rows;
    in_open(&rows, &state->store, state->rows.offset,
            state->rows.offset + state->track_count * BUILDER_ROW_SIZE, row_buffer, STREAM_BUFFER);
    *count = 0;
    for (uint32_t track = 0; track < state->track_count; track++) {
        uint8_t bytes[BUILDER_ROW_SIZE];
        if (!in_get(&rows, bytes, sizeof bytes)) {
            return false;
        }
        if (bytes[ROW_FLAGS] == 0) {
            continue;
        }
        TrackRow row;
        builder_row_decode(bytes, &row);
        if (!emit_guess(prepared, &tail, track, &row, &buffers)) {
            return false;
        }
        (*count)++;
    }
    bool closed = builder_close_tail(state, &tail, guesses);
    state->arena.used = mark;
    return closed;
}

static bool decide(BuilderState *state, Region sorted, Region *decisions)
{
    uint32_t mark = state->arena.used;
    Stream stream;
    uint8_t *tail_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *album = arena_take(&state->arena, BUILDER_MAX_RECORD);
    uint8_t *artist = arena_take(&state->arena, BUILDER_MAX_RECORD);
    Tail tail;
    if (tail_buffer == NULL || album == NULL || artist == NULL ||
        !builder_open_stream(state, &stream, sorted) ||
        !builder_open_tail(state, &tail, tail_buffer) || !builder_pull(&stream)) {
        return false;
    }
    while (stream.live) {
        for (uint32_t index = 0; index < builder_get16(stream.record); index++) {
            album[index] = stream.record[index];
        }
        uint32_t best_count = 0;
        uint8_t best[DECISION_SIZE] = {0};
        while (stream.live && same_album(album, stream.record) == 0) {
            for (uint32_t index = 0; index < builder_get16(stream.record); index++) {
                artist[index] = stream.record[index];
            }
            uint32_t count = 0;
            while (stream.live && same_album(album, stream.record) == 0 &&
                   same_artist(artist, stream.record) == 0) {
                count++;
                if (!builder_pull(&stream)) {
                    return false;
                }
            }
            if (count > best_count) {
                best_count = count;
                builder_put32(best, builder_get32(artist + 10));
                builder_put16(best + 4, builder_get16(artist + 14));
            }
        }
        if (!out_put(&tail.out, best, sizeof best)) {
            return false;
        }
    }
    bool closed = builder_close_tail(state, &tail, decisions);
    state->arena.used = mark;
    return closed && !state->store.io_failed;
}

static bool assign(BuilderState *state, Region sorted, Region decisions, Region *assignments)
{
    uint32_t mark = state->arena.used;
    Stream stream;
    uint8_t *tail_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *decision_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *album = arena_take(&state->arena, BUILDER_MAX_RECORD);
    Tail tail;
    if (tail_buffer == NULL || decision_buffer == NULL || album == NULL ||
        !builder_open_stream(state, &stream, sorted) ||
        !builder_open_tail(state, &tail, tail_buffer) || !builder_pull(&stream)) {
        return false;
    }
    In chosen;
    in_open(&chosen, &state->store, decisions.offset, decisions.offset + decisions.size,
            decision_buffer, STREAM_BUFFER);
    while (stream.live) {
        uint8_t decision[DECISION_SIZE];
        if (!in_get(&chosen, decision, sizeof decision)) {
            return false;
        }
        for (uint32_t index = 0; index < builder_get16(stream.record); index++) {
            album[index] = stream.record[index];
        }
        while (stream.live && same_album(album, stream.record) == 0) {
            uint8_t tuple[ASSIGNMENT_SIZE];
            builder_put16(tuple, ASSIGNMENT_SIZE);
            builder_put32(tuple + 2, builder_get32(stream.record + 6));
            for (uint32_t index = 0; index < DECISION_SIZE; index++) {
                tuple[6 + index] = decision[index];
            }
            if (!out_put(&tail.out, tuple, sizeof tuple) || !builder_pull(&stream)) {
                return false;
            }
        }
    }
    bool closed = builder_close_tail(state, &tail, assignments);
    state->arena.used = mark;
    return closed && !state->store.io_failed;
}

static bool rewrite_rows(BuilderState *state, Region sorted)
{
    uint32_t mark = state->arena.used;
    Stream stream;
    uint8_t *in_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *out_buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (in_buffer == NULL || out_buffer == NULL || !builder_open_stream(state, &stream, sorted) ||
        !builder_pull(&stream)) {
        return false;
    }
    Region region = {.offset = state->rows.offset, .size = state->track_count * BUILDER_ROW_SIZE};
    In rows;
    in_open(&rows, &state->store, region.offset, region.offset + region.size, in_buffer,
            STREAM_BUFFER);
    Out out;
    out_open(&out, &state->store, region, out_buffer, STREAM_BUFFER);
    for (uint32_t track = 0; track < state->track_count; track++) {
        uint8_t bytes[BUILDER_ROW_SIZE];
        if (!in_get(&rows, bytes, sizeof bytes)) {
            return false;
        }
        if (stream.live && builder_get32(stream.record + 2) == track) {
            builder_put32(bytes + ROW_ALBUM_ARTIST_TEXT, builder_get32(stream.record + 6));
            builder_put16(bytes + ROW_ALBUM_ARTIST_LENGTH, builder_get16(stream.record + 10));
            if (!builder_pull(&stream)) {
                return false;
            }
        }
        bytes[ROW_FLAGS] = 0;
        if (!out_put(&out, bytes, sizeof bytes)) {
            return false;
        }
    }
    bool closed = out_close(&out);
    state->arena.used = mark;
    return closed;
}

bool builder_guess_album_artists(BuilderState *state, Prepared *prepared)
{
    uint32_t store_mark = state->store.end;
    Region guesses;
    uint32_t count = 0;
    if (!emit_guesses(state, prepared, &guesses, &count)) {
        return false;
    }
    if (count == 0) {
        state->store.end = store_mark;
        return true;
    }
    Region sorted;
    Region decisions;
    Region assignments;
    Region ordered;
    bool done =
        builder_sort(state, guesses, by_album_artist_track, &sorted) &&
        decide(state, sorted, &decisions) && assign(state, sorted, decisions, &assignments) &&
        builder_sort(state, assignments, by_track, &ordered) && rewrite_rows(state, ordered);
    state->store.end = store_mark;
    return done;
}
