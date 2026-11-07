#include "library/library_scan.h"

#include <stddef.h>
#include <stdint.h>

#include "library/playlist_reader.h"
#include "library/track_fields.h"
#include "library/track_tags.h"

enum {
    LISTING_SHARE = 8,
    MINIMUM_LISTING = 16 * 1024,
    MAXIMUM_LISTING = 96 * 1024,
    ALIGNMENT = 8,
    RESUME_BYTES = (2 + SCAN_NAME_CAPACITY + 3) & ~3,
    NO_RESUME = 0xFFFF,
    MINIMUM_BATCH = 2,
    ENTRY_OVERHEAD = 8,
    ENTRY_HEAD = 9,
    CACHE_BUFFER = 8192,
    CACHE_RECORD_MAX = 4096,
    CACHE_HEADER = 20,
    CACHE_STRINGS = 6,
    CACHE_TRACK = 1,
    CACHE_SKIPPED = 2,
    CACHE_GUESSED = 1
};

typedef struct Level {
    uint32_t start;
    uint32_t first;
    uint32_t count;
    uint32_t next;
    uint32_t path_length;
    bool partial;
} Level;

typedef struct ScanState {
    LibraryBuilder builder;
    TrackTags tags;
    TrackFields fields;
    ScanEntry entry;
    Level levels[SCAN_MAX_DEPTH];
    uint32_t depth;
    char path[SCAN_PATH_CAPACITY + 2];
    uint32_t path_length;
    uint8_t *listing;
    uint32_t listing_size;
    uint32_t listing_used;
    uint32_t playlist_floor;
    uint32_t playlist_count;
    PlaylistReader playlist;
    char entry_text[PLAYLIST_ENTRY_CAPACITY];
    char playlist_name[SCAN_NAME_CAPACITY];
    char cutoff[SCAN_NAME_CAPACITY];
    uint8_t *cache_in;
    uint32_t cache_in_position;
    uint32_t cache_in_filled;
    uint32_t cache_in_taken;
    bool cache_live;
    uint8_t *cache_out;
    uint32_t cache_out_used;
    uint32_t cache_out_written;
    bool cache_failed;
} ScanState;

typedef struct Listing {
    uint32_t *offsets;
    uint32_t *scratch;
} Listing;

static uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static const char *entry_name(const ScanState *state, uint32_t offset)
{
    return (const char *)state->listing + offset + ENTRY_HEAD;
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool entry_is_directory(const ScanState *state, uint32_t offset)
{
    return state->listing[offset] != 0;
}

static int compare_texts(const char *left, const char *right)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

static int compare_names(const ScanState *state, uint32_t left, uint32_t right)
{
    return compare_texts(entry_name(state, left), entry_name(state, right));
}

static void merge_names(const ScanState *state, const uint32_t *from, uint32_t *to, uint32_t start,
                        uint32_t middle, uint32_t end)
{
    uint32_t left = start;
    uint32_t right = middle;
    for (uint32_t out = start; out < end; out++) {
        bool take_left =
            left < middle && (right >= end || compare_names(state, from[left], from[right]) <= 0);
        to[out] = take_left ? from[left++] : from[right++];
    }
}

static void sort_names(const ScanState *state, uint32_t *items, uint32_t *scratch, uint32_t count)
{
    uint32_t *from = items;
    uint32_t *to = scratch;
    for (uint32_t width = 1; width < count; width *= 2) {
        for (uint32_t start = 0; start < count; start += 2 * width) {
            uint32_t middle = start + width < count ? start + width : count;
            uint32_t end = start + 2 * width < count ? start + 2 * width : count;
            merge_names(state, from, to, start, middle, end);
        }
        uint32_t *swap = from;
        from = to;
        to = swap;
    }
    for (uint32_t index = 0; from != items && index < count; index++) {
        items[index] = from[index];
    }
}

static bool store_entry(ScanState *state, const ScanEntry *entry)
{
    uint32_t length = text_length(entry->name);
    if (length + ENTRY_HEAD + 1 > state->playlist_floor - state->listing_used) {
        return false;
    }
    uint8_t *record = state->listing + state->listing_used;
    record[0] = entry->directory ? 1 : 0;
    put_u32(record + 1, entry->size);
    put_u32(record + 5, entry->modified);
    for (uint32_t index = 0; index <= length; index++) {
        record[ENTRY_HEAD + index] = (uint8_t)entry->name[index];
    }
    state->listing_used += length + ENTRY_HEAD + 1;
    return true;
}

static uint32_t align4(uint32_t value)
{
    return (value + 3) & ~3u;
}

static uint32_t *sorted_offsets(ScanState *state, uint32_t start, uint32_t count)
{
    uint32_t table = align4(state->listing_used);
    uint32_t *offsets = (uint32_t *)(void *)(state->listing + table);
    uint32_t cursor = start;
    for (uint32_t index = 0; index < count; index++) {
        offsets[index] = cursor;
        cursor += ENTRY_HEAD + 1 + text_length(entry_name(state, cursor));
    }
    sort_names(state, offsets, offsets + count, count);
    return offsets;
}

static ScanResult index_listing(ScanState *state, uint32_t start, uint32_t count, Level *level)
{
    uint32_t table = align4(state->listing_used);
    if (table > state->playlist_floor || (uint64_t)count * 8 > state->playlist_floor - table) {
        return SCAN_FOLDER_TOO_BIG;
    }
    sorted_offsets(state, start, count);
    state->listing_used = table + count * 4;
    level->first = table;
    level->count = count;
    level->next = 0;
    level->path_length = state->path_length;
    return SCAN_OK;
}

static ScanResult read_whole(ScanState *state, const ScanFileSystem *files, Level *level,
                             bool *fits)
{
    if (!files->open_dir(files->context, state->path)) {
        return SCAN_NO_MUSIC_ROOT;
    }
    uint32_t start = state->listing_used;
    uint32_t count = 0;
    *fits = true;
    while (*fits && files->next_entry(files->context, &state->entry)) {
        if (state->entry.name[0] == '.') {
            continue;
        }
        *fits = store_entry(state, &state->entry) &&
                align4(state->listing_used) + (count + 1) * ENTRY_OVERHEAD <= state->playlist_floor;
        count++;
    }
    files->close_dir(files->context);
    if (!*fits) {
        state->listing_used = start;
        return SCAN_OK;
    }
    level->start = start;
    level->partial = false;
    return index_listing(state, start, count, level);
}

typedef struct Batch {
    uint32_t start;
    uint32_t end;
    uint32_t count;
    bool cut;
    const char *after;
} Batch;

static bool batch_fits(const ScanState *state, const Batch *batch, uint32_t length)
{
    uint64_t need = (uint64_t)align4(state->listing_used + length + ENTRY_HEAD + 1) +
                    (uint64_t)(batch->count + 1) * ENTRY_OVERHEAD;
    return need <= batch->end;
}

static void keep_smaller_half(ScanState *state, Batch *batch)
{
    uint32_t *offsets = sorted_offsets(state, batch->start, batch->count);
    uint32_t keep = batch->count / 2;
    const char *first_dropped = entry_name(state, offsets[keep]);
    uint32_t length = text_length(first_dropped);
    for (uint32_t index = 0; index <= length; index++) {
        state->cutoff[index] = first_dropped[index];
    }
    uint32_t write = batch->start;
    for (uint32_t read = batch->start; read < state->listing_used;) {
        uint32_t size = ENTRY_HEAD + 1 + text_length(entry_name(state, read));
        if (compare_texts(entry_name(state, read), state->cutoff) < 0) {
            for (uint32_t index = 0; index < size; index++) {
                state->listing[write + index] = state->listing[read + index];
            }
            write += size;
        }
        read += size;
    }
    state->listing_used = write;
    batch->count = keep;
    batch->cut = true;
}

static bool wanted(const ScanState *state, const Batch *batch, const char *name)
{
    return name[0] != '.' && (batch->after == NULL || compare_texts(name, batch->after) > 0) &&
           (!batch->cut || compare_texts(name, state->cutoff) < 0);
}

static ScanResult add_to_batch(ScanState *state, Batch *batch)
{
    uint32_t length = text_length(state->entry.name);
    while (!batch_fits(state, batch, length)) {
        if (batch->count < MINIMUM_BATCH) {
            return SCAN_FOLDER_TOO_BIG;
        }
        keep_smaller_half(state, batch);
        if (compare_texts(state->entry.name, state->cutoff) >= 0) {
            return SCAN_OK;
        }
    }
    store_entry(state, &state->entry);
    batch->count++;
    return SCAN_OK;
}

static ScanResult read_batch(ScanState *state, const ScanFileSystem *files, Level *level,
                             uint32_t start, bool resume)
{
    uint8_t *header = state->listing + start;
    if (!resume) {
        header[0] = (uint8_t)NO_RESUME;
        header[1] = (uint8_t)(NO_RESUME >> 8);
    }
    bool has_after = (uint32_t)(header[0] | header[1] << 8) != NO_RESUME;
    Batch batch = {.start = start + RESUME_BYTES,
                   .end = start + (state->playlist_floor - start) / 2,
                   .count = 0,
                   .cut = false,
                   .after = has_after ? (const char *)header + 2 : NULL};
    if (batch.end < batch.start + 2 * (RESUME_BYTES + ENTRY_OVERHEAD)) {
        return SCAN_FOLDER_TOO_BIG;
    }
    if (!files->open_dir(files->context, state->path)) {
        return SCAN_NO_MUSIC_ROOT;
    }
    state->listing_used = batch.start;
    ScanResult result = SCAN_OK;
    while (result == SCAN_OK && files->next_entry(files->context, &state->entry)) {
        if (wanted(state, &batch, state->entry.name)) {
            result = add_to_batch(state, &batch);
        }
    }
    files->close_dir(files->context);
    if (result != SCAN_OK) {
        return result;
    }
    level->start = start;
    level->partial = batch.cut;
    return index_listing(state, batch.start, batch.count, level);
}

static ScanResult read_listing(ScanState *state, const ScanFileSystem *files, Level *level)
{
    bool fits = true;
    uint32_t start = state->listing_used;
    ScanResult result = read_whole(state, files, level, &fits);
    if (result != SCAN_OK || fits) {
        return result;
    }
    return read_batch(state, files, level, start, false);
}

static ScanResult read_next_batch(ScanState *state, const ScanFileSystem *files, Level *level)
{
    const uint32_t *offsets = (const uint32_t *)(const void *)(state->listing + level->first);
    const char *last = entry_name(state, offsets[level->count - 1]);
    uint32_t length = text_length(last);
    char *resume = (char *)state->listing + level->start + 2;
    for (uint32_t index = 0; index <= length; index++) {
        resume[index] = last[index];
    }
    state->listing[level->start] = (uint8_t)length;
    state->listing[level->start + 1] = (uint8_t)(length >> 8);
    return read_batch(state, files, level, level->start, true);
}

static bool append_path(ScanState *state, const char *name, bool directory)
{
    uint32_t length = text_length(name);
    if (state->path_length + length + 2 > SCAN_PATH_CAPACITY) {
        return false;
    }
    for (uint32_t index = 0; index < length; index++) {
        state->path[state->path_length + index] = name[index];
    }
    state->path_length += length;
    if (directory) {
        state->path[state->path_length++] = '/';
    }
    state->path[state->path_length] = '\0';
    return true;
}

static void truncate_path(ScanState *state, uint32_t length)
{
    state->path_length = length;
    state->path[length] = '\0';
}

static ScanResult builder_result(BuilderResult result)
{
    switch (result) {
    case BUILDER_OK:
        return SCAN_OK;
    case BUILDER_OUT_OF_MEMORY:
        return SCAN_OUT_OF_MEMORY;
    case BUILDER_TOO_MANY:
        return SCAN_TOO_MANY;
    case BUILDER_WRITE_FAILED:
        return SCAN_WRITE_FAILED;
    case BUILDER_NO_ROOM:
        return SCAN_NO_ROOM;
    }
    return SCAN_WRITE_FAILED;
}

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static int compare_part_paths(const char *left, const char *right)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    for (;; a++, b++) {
        bool left_end = *a == '\0' || *a == '/';
        bool right_end = *b == '\0' || *b == '/';
        if (left_end || right_end) {
            if (left_end != right_end) {
                return left_end ? -1 : 1;
            }
            if (*a == '\0' || *b == '\0') {
                return *a == *b ? 0 : *a == '\0' ? -1 : 1;
            }
        } else if (*a != *b) {
            return *a < *b ? -1 : 1;
        }
    }
}

static bool cache_fill(ScanState *state, const ScanCache *cache, uint32_t need)
{
    uint32_t left = state->cache_in_filled - state->cache_in_taken;
    if (left >= need) {
        return true;
    }
    for (uint32_t index = 0; index < left; index++) {
        state->cache_in[index] = state->cache_in[state->cache_in_taken + index];
    }
    state->cache_in_filled = left;
    state->cache_in_taken = 0;
    uint32_t room = CACHE_BUFFER - left;
    uint32_t available = cache->previous_length - state->cache_in_position;
    uint32_t step = room < available ? room : available;
    if (step > 0 && !cache->previous.read(cache->previous.context, state->cache_in_position,
                                          state->cache_in + left, step)) {
        return false;
    }
    state->cache_in_position += step;
    state->cache_in_filled += step;
    return state->cache_in_filled >= need;
}

static const uint8_t *cache_record(const ScanState *state)
{
    return state->cache_in + state->cache_in_taken;
}

static bool cache_strings_fit(const uint8_t *record, uint32_t length, uint32_t strings)
{
    uint32_t offset = CACHE_HEADER;
    for (uint32_t string = 0; string < strings; string++) {
        while (offset < length && record[offset] != 0) {
            offset++;
        }
        if (offset >= length) {
            return false;
        }
        offset++;
    }
    return offset == length;
}

static void cache_load(ScanState *state, const ScanCache *cache)
{
    state->cache_live = false;
    if (!cache_fill(state, cache, 2)) {
        return;
    }
    uint32_t length = get_u16(cache_record(state));
    if (length <= CACHE_HEADER || length > CACHE_RECORD_MAX || !cache_fill(state, cache, length)) {
        return;
    }
    const uint8_t *record = cache_record(state);
    uint32_t strings = record[2] == CACHE_TRACK ? CACHE_STRINGS : 1;
    state->cache_live = (record[2] == CACHE_TRACK || record[2] == CACHE_SKIPPED) &&
                        cache_strings_fit(record, length, strings);
}

static void cache_next(ScanState *state, const ScanCache *cache)
{
    state->cache_in_taken += get_u16(cache_record(state));
    cache_load(state, cache);
}

static bool cache_flush(ScanState *state, const ScanCache *cache)
{
    if (state->cache_out_used > 0 && !state->cache_failed) {
        state->cache_failed = !cache->next.write(cache->next.context, state->cache_out_written,
                                                 state->cache_out, state->cache_out_used);
        state->cache_out_written += state->cache_out_used;
    }
    state->cache_out_used = 0;
    return !state->cache_failed;
}

static uint8_t *cache_room(ScanState *state, const ScanCache *cache)
{
    if (CACHE_BUFFER - state->cache_out_used < CACHE_RECORD_MAX && !cache_flush(state, cache)) {
        return NULL;
    }
    return state->cache_out + state->cache_out_used;
}

static void cache_keep(ScanState *state, const ScanCache *cache, const uint8_t *record)
{
    uint8_t *to = cache_room(state, cache);
    uint32_t length = get_u16(record);
    for (uint32_t index = 0; to != NULL && index < length; index++) {
        to[index] = record[index];
    }
    state->cache_out_used += to != NULL ? length : 0;
}

static uint32_t put_text(uint8_t *record, uint32_t offset, const char *text)
{
    uint32_t length = text_length(text);
    if (offset + length + 1 > CACHE_RECORD_MAX) {
        return CACHE_RECORD_MAX + 1;
    }
    for (uint32_t index = 0; index <= length; index++) {
        record[offset + index] = (uint8_t)text[index];
    }
    return offset + length + 1;
}

static void cache_write(ScanState *state, const ScanCache *cache, const BuilderTrack *track,
                        const char *path, uint32_t size, uint32_t modified)
{
    uint8_t *record = cache_room(state, cache);
    if (record == NULL) {
        return;
    }
    record[2] = track != NULL ? CACHE_TRACK : CACHE_SKIPPED;
    record[3] = track != NULL && track->album_artist_guessed ? CACHE_GUESSED : 0;
    put_u32(record + 4, size);
    put_u32(record + 8, modified);
    put_u32(record + 12, track != NULL ? track->duration_ms : 0);
    record[16] = (uint8_t)(track != NULL ? track->year : 0);
    record[17] = (uint8_t)(track != NULL ? track->year >> 8 : 0);
    record[18] = track != NULL ? track->disc : 0;
    record[19] = track != NULL ? track->number : 0;
    uint32_t offset = put_text(record, CACHE_HEADER, path);
    if (track != NULL) {
        const char *texts[CACHE_STRINGS - 1] = {track->title, track->artist, track->album_artist,
                                                track->album, track->genre};
        for (int text = 0; text < CACHE_STRINGS - 1 && offset <= CACHE_RECORD_MAX; text++) {
            offset = put_text(record, offset, texts[text]);
        }
    }
    if (offset > CACHE_RECORD_MAX) {
        return;
    }
    record[0] = (uint8_t)offset;
    record[1] = (uint8_t)(offset >> 8);
    state->cache_out_used += offset;
}

static BuilderTrack cached_track(const uint8_t *record)
{
    const char *texts[CACHE_STRINGS];
    const char *cursor = (const char *)record + CACHE_HEADER;
    for (int text = 0; text < CACHE_STRINGS; text++) {
        texts[text] = cursor;
        cursor += text_length(cursor) + 1;
    }
    return (BuilderTrack){.path = texts[0],
                          .title = texts[1],
                          .artist = texts[2],
                          .album_artist = texts[3],
                          .album = texts[4],
                          .genre = texts[5],
                          .year = get_u16(record + 16),
                          .disc = record[18],
                          .number = record[19],
                          .duration_ms = get_u32(record + 12),
                          .album_artist_guessed = (record[3] & CACHE_GUESSED) != 0};
}

static bool reuse_cached(ScanState *state, const ScanJob *job, ScanStats *stats, uint32_t size,
                         uint32_t modified, ScanResult *result)
{
    const ScanCache *cache = job->cache;
    const char *path = state->path + 1;
    while (state->cache_live &&
           compare_part_paths((const char *)cache_record(state) + CACHE_HEADER, path) < 0) {
        cache_next(state, cache);
    }
    if (!state->cache_live ||
        compare_part_paths((const char *)cache_record(state) + CACHE_HEADER, path) != 0) {
        return false;
    }
    const uint8_t *record = cache_record(state);
    if (get_u32(record + 4) != size || get_u32(record + 8) != modified) {
        cache_next(state, cache);
        return false;
    }
    if (record[2] == CACHE_TRACK) {
        BuilderTrack track = cached_track(record);
        *result = builder_result(library_builder_add_track(&state->builder, &track));
        stats->tracks += *result == SCAN_OK;
    } else {
        stats->skipped++;
    }
    stats->reused++;
    cache_keep(state, cache, record);
    cache_next(state, cache);
    return true;
}

static ScanResult read_file(ScanState *state, const ScanJob *job, ScanStats *stats, uint32_t size,
                            uint32_t modified)
{
    ByteSource source;
    uint32_t length = 0;
    bool read = job->files.open_file(job->files.context, state->path, &source, &length);
    if (read) {
        TagFormat format = track_tags_format_for_name(state->path);
        read = track_tags_read(source, length, format, &state->tags);
        job->files.close_file(job->files.context);
    }
    ScanResult result = SCAN_OK;
    if (read) {
        track_fields_from_tags(state->path + 1, &state->tags, &state->fields);
        result = builder_result(library_builder_add_track(&state->builder, &state->fields.track));
        stats->tracks += result == SCAN_OK;
    } else {
        stats->skipped++;
    }
    if (job->cache != NULL && result == SCAN_OK) {
        cache_write(state, job->cache, read ? &state->fields.track : NULL, state->path + 1, size,
                    modified);
    }
    return result;
}

static ScanResult add_file(ScanState *state, const ScanJob *job, ScanStats *stats, uint32_t size,
                           uint32_t modified)
{
    stats->files++;
    ScanResult result = SCAN_OK;
    if (job->cache == NULL || !reuse_cached(state, job, stats, size, modified, &result)) {
        result = read_file(state, job, stats, size, modified);
    }
    if (job->progress.report != NULL) {
        job->progress.report(job->progress.context, stats->files, stats->tracks);
    }
    return result;
}

static bool is_playlist_name(const char *name)
{
    uint32_t length = text_length(name);
    const char *dot = NULL;
    for (uint32_t index = 1; index < length; index++) {
        dot = name[index] == '.' ? name + index : dot;
    }
    if (dot == NULL) {
        return false;
    }
    char lower[6] = {0};
    uint32_t count = 0;
    for (const char *cursor = dot + 1; *cursor != '\0' && count < 5; cursor++) {
        lower[count++] = (char)(*cursor >= 'A' && *cursor <= 'Z' ? *cursor - 'A' + 'a' : *cursor);
    }
    bool m3u = count == 3 && lower[0] == 'm' && lower[1] == '3' && lower[2] == 'u';
    bool m3u8 =
        count == 4 && lower[0] == 'm' && lower[1] == '3' && lower[2] == 'u' && lower[3] == '8';
    return (m3u || m3u8) && dot[count + 1] == '\0';
}

static ScanResult note_playlist(ScanState *state, const char *name)
{
    uint32_t parent = state->path_length;
    uint32_t length = parent + text_length(name);
    uint32_t need = align4(length + 1 + 4);
    if (need > state->playlist_floor - align4(state->listing_used)) {
        return SCAN_FOLDER_TOO_BIG;
    }
    state->playlist_floor -= need;
    uint8_t *record = state->listing + state->playlist_floor;
    record[0] = (uint8_t)(length & 0xFF);
    record[1] = (uint8_t)(length >> 8);
    record[2] = (uint8_t)(parent & 0xFF);
    record[3] = (uint8_t)(parent >> 8);
    for (uint32_t index = 0; index < parent; index++) {
        record[4 + index] = (uint8_t)state->path[index];
    }
    for (uint32_t index = parent; index < length; index++) {
        record[4 + index] = (uint8_t)name[index - parent];
    }
    record[4 + length] = 0;
    state->playlist_count++;
    return SCAN_OK;
}

static ScanResult visit_entry(ScanState *state, const ScanJob *job, ScanStats *stats,
                              uint32_t offset)
{
    const char *name = entry_name(state, offset);
    bool directory = entry_is_directory(state, offset);
    if (!directory && is_playlist_name(name)) {
        return note_playlist(state, name);
    }
    if (!directory && track_tags_format_for_name(name) == TAG_FORMAT_UNKNOWN) {
        return SCAN_OK;
    }
    if (!append_path(state, name, directory)) {
        return SCAN_PATH_TOO_LONG;
    }
    if (!directory) {
        const uint8_t *record = state->listing + offset;
        ScanResult result = add_file(state, job, stats, get_u32(record + 1), get_u32(record + 5));
        truncate_path(state, state->levels[state->depth - 1].path_length);
        return result;
    }
    if (state->depth >= SCAN_MAX_DEPTH) {
        return SCAN_PATH_TOO_LONG;
    }
    ScanResult result = read_listing(state, &job->files, &state->levels[state->depth]);
    state->depth += result == SCAN_OK;
    return result == SCAN_NO_MUSIC_ROOT ? SCAN_OK : result;
}

static ScanResult walk(ScanState *state, const ScanJob *job, ScanStats *stats)
{
    ScanResult result = read_listing(state, &job->files, &state->levels[0]);
    state->depth = result == SCAN_OK ? 1 : 0;
    while (result == SCAN_OK && state->depth > 0) {
        Level *level = &state->levels[state->depth - 1];
        if (level->next >= level->count && level->partial && level->count > 0) {
            truncate_path(state, level->path_length);
            result = read_next_batch(state, &job->files, level);
            continue;
        }
        if (level->next >= level->count) {
            state->listing_used = level->start;
            state->depth--;
            truncate_path(state,
                          state->depth > 0 ? state->levels[state->depth - 1].path_length : 1);
            continue;
        }
        const uint32_t *offsets = (const uint32_t *)(const void *)(state->listing + level->first);
        result = visit_entry(state, job, stats, offsets[level->next++]);
    }
    return result;
}

static bool split_memory(ScanState **state, const ScanJob *job)
{
    uintptr_t start = (uintptr_t)job->memory;
    uint32_t skip = (uint32_t)((ALIGNMENT - start % ALIGNMENT) % ALIGNMENT);
    uint32_t header = (uint32_t)((sizeof(ScanState) + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1));
    bool separate = job->listing != NULL && job->listing_size >= MINIMUM_LISTING;
    if (job->size < skip + header + MINIMUM_LISTING * (separate ? 1u : 2u)) {
        return false;
    }
    uint32_t size = job->size - skip;
    uint32_t listing = separate ? job->listing_size : size / LISTING_SHARE;
    listing = listing < MINIMUM_LISTING ? MINIMUM_LISTING : listing;
    listing = listing > MAXIMUM_LISTING ? MAXIMUM_LISTING : listing;
    uint8_t *memory = (uint8_t *)job->memory + skip;
    uint32_t taken = separate ? 0 : listing;
    uint32_t cache = job->cache != NULL ? 2 * CACHE_BUFFER : 0;
    if (size < header + taken + cache + MINIMUM_LISTING) {
        return false;
    }
    *state = (ScanState *)(void *)memory;
    (*state)->listing = separate ? (uint8_t *)job->listing : memory + header;
    (*state)->listing_size = listing;
    (*state)->listing_used = 0;
    (*state)->playlist_floor = listing;
    (*state)->playlist_count = 0;
    (*state)->cache_in = cache > 0 ? memory + header + taken : NULL;
    (*state)->cache_out = cache > 0 ? memory + header + taken + CACHE_BUFFER : NULL;
    (*state)->cache_in_position = 0;
    (*state)->cache_in_filled = 0;
    (*state)->cache_in_taken = 0;
    (*state)->cache_live = false;
    (*state)->cache_out_used = 0;
    (*state)->cache_out_written = 0;
    (*state)->cache_failed = false;
    taken += cache;
    library_builder_init(&(*state)->builder, memory + header + taken, size - header - taken,
                         job->scratch);
    return true;
}

static void name_and_folder(ScanState *state, const uint8_t *record, char *folder)
{
    uint32_t length = (uint32_t)record[0] | (uint32_t)record[1] << 8;
    uint32_t parent = (uint32_t)record[2] | (uint32_t)record[3] << 8;
    const char *path = (const char *)record + 4;
    uint32_t folder_length = parent > 1 ? parent - 2 : 0;
    for (uint32_t index = 0; index < folder_length; index++) {
        folder[index] = path[index + 1];
    }
    folder[folder_length] = '\0';
    uint32_t stem_end = length;
    for (uint32_t index = parent + 1; index < length; index++) {
        stem_end = path[index] == '.' ? index : stem_end;
    }
    uint32_t count = 0;
    for (uint32_t index = parent; index < stem_end; index++) {
        state->playlist_name[count++] = path[index];
    }
    state->playlist_name[count] = '\0';
}

static ScanResult read_playlist(ScanState *state, const ScanJob *job, const uint8_t *record)
{
    ByteSource source;
    uint32_t size = 0;
    const char *path = (const char *)record + 4;
    if (!job->files.open_file(job->files.context, path, &source, &size)) {
        return SCAN_OK;
    }
    name_and_folder(state, record, state->path);
    ScanResult result = builder_result(
        library_builder_begin_playlist(&state->builder, state->playlist_name, state->path));
    playlist_reader_open(&state->playlist, source, size);
    while (result == SCAN_OK &&
           playlist_reader_next(&state->playlist, state->entry_text, sizeof state->entry_text)) {
        result = builder_result(library_builder_playlist_entry(&state->builder, state->entry_text));
    }
    job->files.close_file(job->files.context);
    return result;
}

static uint32_t record_length(const ScanState *state, uint32_t offset)
{
    uint32_t length = (uint32_t)state->listing[offset] | (uint32_t)state->listing[offset + 1] << 8;
    return align4(length + 1 + 4);
}

static uint32_t noted_playlist(const ScanState *state, uint32_t index)
{
    uint32_t offset = state->playlist_floor;
    for (uint32_t skip = 0; skip + 1 + index < state->playlist_count; skip++) {
        offset += record_length(state, offset);
    }
    return offset;
}

static ScanResult read_playlists(ScanState *state, const ScanJob *job)
{
    ScanResult result = builder_result(library_builder_prepare(&state->builder));
    for (uint32_t index = 0; result == SCAN_OK && index < state->playlist_count; index++) {
        result = read_playlist(state, job, state->listing + noted_playlist(state, index));
    }
    return result;
}

ScanResult library_scan(const ScanJob *job, ScanStats *stats)
{
    *stats = (ScanStats){.files = 0, .tracks = 0, .skipped = 0, .reused = 0, .length = 0};
    ScanState *state = NULL;
    if (!split_memory(&state, job)) {
        return SCAN_OUT_OF_MEMORY;
    }
    state->path[0] = '/';
    truncate_path(state, 1);
    if (job->cache != NULL) {
        job->cache->next_length = 0;
        cache_load(state, job->cache);
    }
    ScanResult result = walk(state, job, stats);
    if (result == SCAN_OK && job->cache != NULL) {
        result = cache_flush(state, job->cache) ? SCAN_OK : SCAN_WRITE_FAILED;
        job->cache->next_length = state->cache_out_written;
    }
    if (result == SCAN_OK) {
        result = read_playlists(state, job);
    }
    if (result != SCAN_OK) {
        return result;
    }
    return builder_result(
        library_builder_finish(&state->builder, job->sink, job->capacity, &stats->length));
}
