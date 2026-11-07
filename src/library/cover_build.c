#include "library/cover_build.h"

#include <stddef.h>

#include "core/hash.h"
#include "image/cover_image.h"
#include "library/covers.h"
#include "library/library.h"
#include "library/segment_stream.h"
#include "library/store_layout.h"
#include "library/track_tags.h"
#include "tags_internal.h"

enum {
    ALIGNMENT = 8,
    MAX_FOLDERS = 16,
    STEM_COUNT = 4,
    SUFFIX_COUNT = 3,
    MATCH_CAPACITY = 16,
    ROW_BYTES = COVER_ROW_BYTES,
    ROWS_CHUNK = 64,
    KEY_BYTES = LIBRARY_ID_SIZE,
    STAMP_NUMBERS = 3,
    ART_EMBEDDED = 1,
    ART_FILE = 2,
    ART_TRACKS = 4,
    GARBAGE_SHARE = 4
};

static const char *const STEMS[STEM_COUNT] = {"cover", "folder", "front", "albumart"};
static const char *const SUFFIXES[SUFFIX_COUNT] = {".jpg", ".jpeg", ".png"};

typedef struct ArtFile {
    ByteSource source;
    uint32_t offset;
    uint32_t length;
    uint32_t stamp;
} ArtFile;

typedef struct CoverMatch {
    char name[MATCH_CAPACITY];
    uint32_t modified;
} CoverMatch;

typedef struct Rows {
    ByteSource source;
    uint32_t start;
    uint32_t count;
    uint32_t size;
    uint32_t first;
    uint32_t filled;
    uint8_t bytes[ROWS_CHUNK * KEY_BYTES];
} Rows;

typedef struct OldCovers {
    Library library;
    SegmentStreamIo library_io;
    SegmentStreamIo rows_io;
    Rows keys;
    Rows rows;
    uint32_t position;
    uint8_t stream;
    uint32_t slots;
} OldCovers;

typedef struct CoverState {
    const Library *library;
    TrackTags tags;
    TagPictureReader picture;
    ScanEntry entry;
    LibraryAlbum album;
    char path[SCAN_PATH_CAPACITY + 2];
    CoverMatch matches[STEM_COUNT];
    uint16_t full[COVER_FULL_PIXELS];
    uint16_t half[COVER_HALF_PIXELS];
    uint32_t folders[MAX_FOLDERS];
    uint32_t folder_count;
    OldCovers old;
    SegmentStreamIo new_library_io;
    Rows new_keys;
    uint8_t out[ROWS_CHUNK * ROW_BYTES];
    uint32_t out_used;
    uint8_t stream;
    uint32_t slots;
    bool compact;
    bool full_card;
    uint8_t *decode_memory;
    uint32_t decode_size;
} CoverState;

static void put16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value)
{
    put16(bytes, value);
    put16(bytes + 2, value >> 16);
}

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static uint32_t art_stamp(const char *path, uint32_t first, uint32_t second, uint32_t third)
{
    uint8_t numbers[STAMP_NUMBERS * 4];
    put32(numbers, first);
    put32(numbers + 4, second);
    put32(numbers + 8, third);
    uint32_t hash = hash_update(HASH_START, path, text_length(path));
    return hash_update(hash, numbers, sizeof numbers);
}

static bool read_track_path(CoverState *state, uint32_t track_index, LibraryTrack *track)
{
    state->path[0] = '/';
    return library_read_track(state->library, track_index, track) &&
           library_read_string(state->library, track->path, state->path + 1,
                               sizeof state->path - 1);
}

static bool open_embedded(CoverState *state, const CoverJob *job, uint32_t track_index,
                          ArtFile *art)
{
    LibraryTrack track;
    ByteSource file;
    uint32_t size = 0;
    if (!read_track_path(state, track_index, &track) ||
        !job->files.open_file(job->files.context, state->path, &file, &size)) {
        return false;
    }
    TagFormat format = track_tags_format_for_name(state->path);
    TagPicture *picture = &state->tags.picture;
    if (!track_tags_read(file, size, format, &state->tags) ||
        !tags_open_picture(&state->picture, file, size, picture, &art->source)) {
        job->files.close_file(job->files.context);
        return false;
    }
    art->offset = picture->offset;
    art->length = picture->length;
    art->stamp = art_stamp(state->path, size, picture->length, ART_EMBEDDED);
    return true;
}

static bool open_any_embedded(CoverState *state, const CoverJob *job, ArtFile *art)
{
    const LibraryAlbum *album = &state->album;
    uint32_t tracks = album->track_count < ART_TRACKS ? album->track_count : ART_TRACKS;
    for (uint32_t track = 0; track < tracks; track++) {
        if (open_embedded(state, job, album->first_track + track, art)) {
            return true;
        }
    }
    return false;
}

static char lower(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static bool is_name(const char *name, const char *stem, const char *suffix)
{
    while (*stem != '\0') {
        if (lower(*name++) != *stem++) {
            return false;
        }
    }
    while (*suffix != '\0') {
        if (lower(*name++) != *suffix++) {
            return false;
        }
    }
    return *name == '\0';
}

static bool before(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return (uint8_t)*left < (uint8_t)*right;
}

static void copy_name(char *target, const char *name)
{
    uint32_t index = 0;
    while (name[index] != '\0' && index + 1 < MATCH_CAPACITY) {
        target[index] = name[index];
        index++;
    }
    target[index] = '\0';
}

static void consider(CoverState *state, const ScanEntry *entry)
{
    for (uint32_t stem = 0; stem < STEM_COUNT; stem++) {
        CoverMatch *match = &state->matches[stem];
        for (uint32_t suffix = 0; suffix < SUFFIX_COUNT; suffix++) {
            if (is_name(entry->name, STEMS[stem], SUFFIXES[suffix]) &&
                (match->name[0] == '\0' || before(entry->name, match->name))) {
                copy_name(match->name, entry->name);
                match->modified = entry->modified;
            }
        }
    }
}

static uint32_t folder_length(const char *path)
{
    uint32_t slash = 0;
    for (uint32_t index = 0; path[index] != '\0'; index++) {
        slash = path[index] == '/' ? index : slash;
    }
    return slash;
}

static const CoverMatch *best_match(CoverState *state, const CoverJob *job)
{
    uint32_t length = folder_length(state->path);
    state->path[length > 0 ? length : 1] = '\0';
    for (uint32_t stem = 0; stem < STEM_COUNT; stem++) {
        state->matches[stem].name[0] = '\0';
    }
    if (!job->files.open_dir(job->files.context, state->path)) {
        return NULL;
    }
    while (job->files.next_entry(job->files.context, &state->entry)) {
        if (!state->entry.directory) {
            consider(state, &state->entry);
        }
    }
    job->files.close_dir(job->files.context);
    for (uint32_t stem = 0; stem < STEM_COUNT; stem++) {
        if (state->matches[stem].name[0] != '\0') {
            return &state->matches[stem];
        }
    }
    return NULL;
}

static bool append_name(CoverState *state, const char *name)
{
    uint32_t length = text_length(state->path);
    if (length > 1) {
        state->path[length++] = '/';
    }
    for (uint32_t index = 0; name[index] != '\0'; index++) {
        if (length + 1 >= sizeof state->path) {
            return false;
        }
        state->path[length++] = name[index];
    }
    state->path[length] = '\0';
    return true;
}

static bool open_art_file(CoverState *state, const CoverJob *job, CoverMatch match, ArtFile *art)
{
    uint32_t size = 0;
    if (!append_name(state, match.name) ||
        !job->files.open_file(job->files.context, state->path, &art->source, &size)) {
        return false;
    }
    art->offset = 0;
    art->length = size;
    art->stamp = art_stamp(state->path, size, match.modified, ART_FILE);
    return true;
}

static bool folder_tried(CoverState *state, uint32_t folder)
{
    for (uint32_t index = 0; index < state->folder_count; index++) {
        if (state->folders[index] == folder) {
            return true;
        }
    }
    if (state->folder_count < MAX_FOLDERS) {
        state->folders[state->folder_count++] = folder;
    }
    return false;
}

static bool open_folder_art(CoverState *state, const CoverJob *job, ArtFile *art)
{
    const LibraryAlbum *album = &state->album;
    state->folder_count = 0;
    for (uint32_t track = 0; track < album->track_count; track++) {
        LibraryTrack info;
        if (!read_track_path(state, album->first_track + track, &info) ||
            folder_tried(state, info.folder)) {
            continue;
        }
        const CoverMatch *match = best_match(state, job);
        if (match != NULL) {
            return open_art_file(state, job, *match, art);
        }
    }
    return false;
}

static bool open_art(CoverState *state, const CoverJob *job, ArtFile *art)
{
    if (state->album.track_count == 0) {
        return false;
    }
    return open_any_embedded(state, job, art) || open_folder_art(state, job, art);
}

static bool decode_art(CoverState *state, const ArtFile *art)
{
    ImageSource image = {art->source.read, art->source.context, art->offset, art->length};
    return cover_image_decode(image, state->decode_memory, state->decode_size, state->full,
                              state->half) == COVER_IMAGE_OK;
}

static const uint8_t ZEROS[512] = {0};

static bool appended(CoverState *state, SegmentStore *store, const void *bytes, uint32_t length)
{
    SegmentResult result = segment_store_append(store, state->stream, bytes, length);
    state->full_card |= result == SEGMENT_NO_SPACE || result == SEGMENT_FULL;
    return result == SEGMENT_OK;
}

static bool write_record(CoverState *state, SegmentStore *store, uint32_t slot)
{
    uint32_t offset = covers_offset(slot);
    uint32_t length = segment_store_working_length(store, state->stream);
    while (length < offset) {
        uint32_t step = offset - length < sizeof ZEROS ? offset - length : sizeof ZEROS;
        if (!appended(state, store, ZEROS, step)) {
            return false;
        }
        length += step;
    }
    uint8_t *full = (uint8_t *)state->full;
    uint8_t *half = (uint8_t *)state->half;
    for (uint32_t index = 0; index < COVER_FULL_PIXELS; index++) {
        put16(full + index * 2, state->full[index]);
    }
    for (uint32_t index = 0; index < COVER_HALF_PIXELS; index++) {
        put16(half + index * 2, state->half[index]);
    }
    return length == offset && appended(state, store, full, COVER_FULL_BYTES) &&
           appended(state, store, half, COVER_HALF_BYTES);
}

static bool copy_record(CoverState *state, SegmentStore *store, uint32_t from, uint32_t slot)
{
    uint32_t offset = covers_offset(from);
    return segment_store_read(store, state->old.stream, offset, state->full, COVER_FULL_BYTES) &&
           segment_store_read(store, state->old.stream, offset + COVER_FULL_BYTES, state->half,
                              COVER_HALF_BYTES) &&
           write_record(state, store, slot);
}

static uint8_t *take(uint8_t **cursor, uint8_t *end, uint32_t bytes)
{
    uintptr_t address = (uintptr_t)*cursor;
    uint8_t *start = *cursor + (ALIGNMENT - address % ALIGNMENT) % ALIGNMENT;
    if (start > end || (uint32_t)(end - start) < bytes) {
        return NULL;
    }
    *cursor = start + bytes;
    return start;
}

static void rows_open(Rows *rows, ByteSource source, uint32_t start, uint32_t count, uint32_t size)
{
    *rows = (Rows){.source = source, .start = start, .count = count, .size = size};
}

static const uint8_t *row_at(Rows *rows, uint32_t index)
{
    if (index >= rows->count) {
        return NULL;
    }
    if (index < rows->first || index >= rows->first + rows->filled) {
        uint32_t left = rows->count - index;
        uint32_t take_rows = left < ROWS_CHUNK ? left : ROWS_CHUNK;
        if (!rows->source.read(rows->source.context, rows->start + index * rows->size, rows->bytes,
                               take_rows * rows->size)) {
            return NULL;
        }
        rows->first = index;
        rows->filled = take_rows;
    }
    return rows->bytes + (index - rows->first) * rows->size;
}

static uint64_t key_of(const uint8_t *row)
{
    return (uint64_t)get32(row + 4) << 32 | get32(row);
}

static void open_old(CoverState *state, SegmentStore *store)
{
    OldCovers *old = &state->old;
    SegmentRegion library = segment_store_region(store, LIBRARY_REGION_INDEX);
    SegmentRegion table = segment_store_region(store, LIBRARY_REGION_ALBUM_COVERS);
    uint32_t stream = segment_store_value(store, LIBRARY_VALUE_COVERS);
    old->stream = (uint8_t)stream;
    old->slots = segment_store_value(store, LIBRARY_VALUE_COVER_SLOTS);
    old->library_io = (SegmentStreamIo){.store = store, .stream = LIBRARY_STREAM_INDEX};
    old->rows_io = (SegmentStreamIo){.store = store, .stream = LIBRARY_STREAM_ALBUM_COVERS};
    rows_open(&old->keys, segment_stream_source(&old->library_io), 0, 0, KEY_BYTES);
    rows_open(&old->rows, segment_stream_source(&old->rows_io), 0, 0, ROW_BYTES);
    old->position = 0;
    bool usable = library.stream == LIBRARY_STREAM_INDEX && segment_store_found(store) &&
                  table.stream == LIBRARY_STREAM_ALBUM_COVERS &&
                  (stream == LIBRARY_STREAM_COVERS || stream == LIBRARY_STREAM_OTHER_COVERS) &&
                  library_open(&old->library, segment_stream_source(&old->library_io),
                               library.length) == LIBRARY_OK &&
                  table.length == old->library.album_keys.count * ROW_BYTES;
    if (!usable) {
        old->stream = LIBRARY_STREAM_COVERS;
        old->slots = 0;
        return;
    }
    rows_open(&old->keys, old->library.source, old->library.album_keys.offset,
              old->library.album_keys.count, KEY_BYTES);
    rows_open(&old->rows, segment_stream_source(&old->rows_io), 0, old->library.album_keys.count,
              ROW_BYTES);
}

static bool old_cover(CoverState *state, uint64_t key, uint32_t *slot, uint32_t *stamp)
{
    OldCovers *old = &state->old;
    const uint8_t *row = row_at(&old->keys, old->position);
    while (row != NULL && key_of(row) < key) {
        row = row_at(&old->keys, ++old->position);
    }
    if (row == NULL || key_of(row) != key) {
        return false;
    }
    const uint8_t *cover = row_at(&old->rows, old->position);
    if (cover == NULL) {
        return false;
    }
    *slot = get32(cover);
    *stamp = get32(cover + 4);
    return *slot < old->slots && *stamp != 0;
}

static ScanResult flush_rows(CoverState *state, SegmentStore *store)
{
    SegmentResult result =
        state->out_used == 0
            ? SEGMENT_OK
            : segment_store_append(store, LIBRARY_STREAM_ALBUM_COVERS, state->out, state->out_used);
    state->out_used = 0;
    if (result == SEGMENT_NO_SPACE || result == SEGMENT_FULL) {
        return SCAN_NO_ROOM;
    }
    return result == SEGMENT_OK ? SCAN_OK : SCAN_WRITE_FAILED;
}

static ScanResult put_row(CoverState *state, SegmentStore *store, uint32_t slot, uint32_t stamp)
{
    put32(state->out + state->out_used, slot);
    put32(state->out + state->out_used + 4, stamp);
    state->out_used += ROW_BYTES;
    return state->out_used == sizeof state->out ? flush_rows(state, store) : SCAN_OK;
}

static void report(const CoverJob *job, uint32_t done, uint32_t total)
{
    if (job->progress.report != NULL) {
        job->progress.report(job->progress.context, done, total);
    }
}

static bool new_slot(CoverState *state, uint32_t *slot)
{
    if (state->full_card) {
        return false;
    }
    *slot = state->slots++;
    return true;
}

static bool place(CoverState *state, const CoverJob *job, const ArtFile *art, uint64_t key,
                  uint32_t *slot, CoverStats *stats)
{
    uint32_t old_slot = 0;
    uint32_t old_stamp = 0;
    if (old_cover(state, key, &old_slot, &old_stamp) && old_stamp == art->stamp) {
        stats->reused++;
        if (!state->compact) {
            *slot = old_slot;
            return true;
        }
        return new_slot(state, slot) && copy_record(state, job->store, old_slot, *slot);
    }
    return decode_art(state, art) && new_slot(state, slot) &&
           write_record(state, job->store, *slot);
}

static void cover_album(CoverState *state, const CoverJob *job, uint32_t album, uint64_t key,
                        uint32_t row[2], CoverStats *stats)
{
    ArtFile art;
    row[0] = COVER_NONE;
    row[1] = 0;
    if (library_read_album(state->library, album, &state->album) && open_art(state, job, &art)) {
        if (place(state, job, &art, key, &row[0], stats)) {
            row[1] = art.stamp;
            stats->covers++;
        } else {
            row[0] = COVER_NONE;
        }
        job->files.close_file(job->files.context);
    }
    stats->missing += row[0] == COVER_NONE;
}

static ScanResult walk(CoverState *state, const CoverJob *job, CoverStats *stats)
{
    uint64_t last_key = 0;
    uint32_t row[2] = {COVER_NONE, 0};
    for (uint32_t position = 0; position < state->new_keys.count; position++) {
        report(job, position, stats->albums);
        const uint8_t *entry = row_at(&state->new_keys, position);
        if (entry == NULL) {
            return SCAN_WRITE_FAILED;
        }
        uint64_t key = key_of(entry);
        if (position == 0 || key != last_key) {
            cover_album(state, job, get32(entry + 8), key, row, stats);
        }
        ScanResult result = put_row(state, job->store, row[0], row[1]);
        if (result != SCAN_OK) {
            return result;
        }
        last_key = key;
    }
    report(job, stats->albums, stats->albums);
    return flush_rows(state, job->store);
}

static CoverState *prepare(const CoverJob *job)
{
    uint8_t *cursor = job->memory;
    uint8_t *end = cursor + job->size;
    CoverState *state = (CoverState *)take(&cursor, end, sizeof(CoverState));
    if (state == NULL) {
        return NULL;
    }
    state->library = job->library;
    state->decode_memory = cursor;
    state->decode_size = (uint32_t)(end - cursor);
    state->out_used = 0;
    state->full_card = false;
    return state;
}

static void choose_stream(CoverState *state, SegmentStore *store)
{
    uint32_t live = state->old.library.album_keys.count;
    uint32_t garbage = state->old.slots > live ? state->old.slots - live : 0;
    state->compact = state->old.slots > 0 && garbage * GARBAGE_SHARE > state->old.slots;
    state->stream = state->old.stream;
    state->slots = state->old.slots;
    if (state->compact || state->old.slots == 0) {
        state->stream = state->old.stream == LIBRARY_STREAM_COVERS ? LIBRARY_STREAM_OTHER_COVERS
                                                                   : LIBRARY_STREAM_COVERS;
        state->slots = 0;
        segment_store_start(store, state->stream, SEGMENT_POOL_COVERS);
    }
}

static void undo(SegmentStore *store)
{
    segment_store_keep(store, LIBRARY_STREAM_COVERS);
    segment_store_keep(store, LIBRARY_STREAM_OTHER_COVERS);
    segment_store_start(store, LIBRARY_STREAM_ALBUM_COVERS, SEGMENT_POOL_LIBRARY);
    segment_store_set_region(store, LIBRARY_REGION_ALBUM_COVERS,
                             (SegmentRegion){.stream = LIBRARY_STREAM_ALBUM_COVERS});
}

ScanResult cover_build(const CoverJob *job, CoverStats *stats)
{
    *stats = (CoverStats){.albums = 0, .covers = 0, .reused = 0, .missing = 0};
    CoverState *state = prepare(job);
    if (state == NULL) {
        return SCAN_OUT_OF_MEMORY;
    }
    SegmentStore *store = job->store;
    stats->albums = library_count(job->library, LIBRARY_ALBUMS);
    rows_open(&state->new_keys, job->library->source, job->library->album_keys.offset,
              job->library->album_keys.count, KEY_BYTES);
    open_old(state, store);
    choose_stream(state, store);
    segment_store_start(store, LIBRARY_STREAM_ALBUM_COVERS, SEGMENT_POOL_LIBRARY);
    ScanResult result = walk(state, job, stats);
    if (result != SCAN_OK) {
        undo(store);
        return result;
    }
    if (state->stream != state->old.stream) {
        segment_store_drop(store, state->old.stream);
    }
    segment_store_set_region(store, LIBRARY_REGION_ALBUM_COVERS,
                             (SegmentRegion){.stream = LIBRARY_STREAM_ALBUM_COVERS,
                                             .offset = 0,
                                             .length = stats->albums * ROW_BYTES});
    segment_store_set_value(store, LIBRARY_VALUE_COVERS, state->stream);
    segment_store_set_value(store, LIBRARY_VALUE_COVER_SLOTS, state->slots);
    return state->full_card ? SCAN_NO_ROOM : SCAN_OK;
}
