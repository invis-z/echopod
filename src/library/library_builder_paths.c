#include <stddef.h>

#include "builder_internal.h"
#include "core/hash.h"
#include "library/sort_key.h"

enum {
    PATH_CAPACITY = 1024,
    MAX_PARTS = 64,
    STREAM_BUFFER = 4096,
    PLACE_SIZE = 16,
    INDEX_ENTRY = 10,
    LOOKUP_BLOCK = 1024,
    LOOKUP_BLOCKS = 4
};

typedef struct Span {
    uint32_t start;
    uint32_t length;
} Span;

typedef struct Parts {
    char text[PATH_CAPACITY];
    Span spans[MAX_PARTS];
    uint32_t count;
} Parts;

typedef struct ResolveScratch {
    char entry[PATH_CAPACITY];
    char joined[PATH_CAPACITY];
    char normal[PATH_CAPACITY];
    char folded[PATH_CAPACITY];
    char track[PATH_CAPACITY];
    char name[BUILDER_TEXT_CAPACITY];
    Parts raw;
    Parts wanted;
    Parts candidate;
    Span kept[MAX_PARTS];
} ResolveScratch;

static ResolveScratch *scratch_of(const Prepared *prepared)
{
    return prepared->resolve;
}

static uint32_t append_folded(char *out, uint32_t used, const char *text, uint32_t length)
{
    if (used >= PATH_CAPACITY - 1) {
        return used;
    }
    return used + (uint32_t)case_fold_make(text, length, out + used, PATH_CAPACITY - used);
}

static bool track_at(Prepared *prepared, uint32_t position, uint32_t *track)
{
    uint8_t bytes[4];
    if (!cache_read(&prepared->positions, prepared->final_tracks.offset + position * 4, bytes,
                    sizeof bytes)) {
        return false;
    }
    *track = builder_get32(bytes);
    return true;
}

static uint32_t folded_track_path(BuilderState *state, Prepared *prepared, uint32_t track,
                                  char *out)
{
    char *name = scratch_of(prepared)->name;
    TrackRow row;
    out[0] = '\0';
    if (!builder_read_row(&prepared->rows, state, track, &row)) {
        return 0;
    }
    FolderRow chain[BUILDER_MAX_DEPTH];
    uint32_t depth = 0;
    for (uint32_t node = row.folder; node != 0 && depth < BUILDER_MAX_DEPTH;) {
        if (!builder_read_folder(&prepared->folders, state, node, &chain[depth])) {
            return 0;
        }
        node = chain[depth++].parent;
    }
    uint32_t used = 0;
    while (depth > 0) {
        const FolderRow *folder = &chain[--depth];
        if (!builder_read_text(&prepared->heap, folder->name, folder->length, name)) {
            return 0;
        }
        used = append_folded(out, used, name, folder->length);
        used = used < PATH_CAPACITY - 1 ? used + 1 : used;
        out[used - 1] = '/';
        out[used] = '\0';
    }
    uint32_t length = row.length[BUILDER_FILE];
    if (!builder_read_text(&prepared->heap, row.text[BUILDER_FILE], length, name)) {
        return 0;
    }
    return append_folded(out, used, name, length);
}

uint32_t builder_folded_path(BuilderState *state, Prepared *prepared, uint32_t position, char *out)
{
    uint32_t track = 0;
    out[0] = '\0';
    return track_at(prepared, position, &track) ? folded_track_path(state, prepared, track, out)
                                                : 0;
}

static uint32_t folded_name_hash(BuilderState *state, Prepared *prepared, uint32_t track,
                                 char *folded)
{
    char *name = scratch_of(prepared)->name;
    TrackRow row;
    if (!builder_read_row(&prepared->rows, state, track, &row) ||
        !builder_read_text(&prepared->heap, row.text[BUILDER_FILE], row.length[BUILDER_FILE],
                           name)) {
        return 0;
    }
    uint32_t length =
        (uint32_t)case_fold_make(name, row.length[BUILDER_FILE], folded, PATH_CAPACITY);
    return hash_update(HASH_START, folded, length);
}

static bool same_folded_path(BuilderState *state, Prepared *prepared, uint32_t position,
                             const char *wanted, uint32_t length)
{
    char *folded = scratch_of(prepared)->track;
    uint32_t found = builder_folded_path(state, prepared, position, folded);
    if (found != length) {
        return false;
    }
    for (uint32_t index = 0; index < length; index++) {
        if (folded[index] != wanted[index]) {
            return false;
        }
    }
    return true;
}

static int by_hash_then_position(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    uint32_t a = builder_get32(left + 2);
    uint32_t b = builder_get32(right + 2);
    if (a != b) {
        return a < b ? -1 : 1;
    }
    uint32_t c = builder_get32(left + 6);
    uint32_t d = builder_get32(right + 6);
    return c < d ? -1 : c > d ? 1 : 0;
}

static bool put_index_entry(Out *out, uint32_t hash, uint32_t position)
{
    uint8_t entry[INDEX_ENTRY];
    builder_put16(entry, INDEX_ENTRY);
    builder_put32(entry + 2, hash);
    builder_put32(entry + 6, position);
    return out_put(out, entry, sizeof entry);
}

static bool emit_index_entries(BuilderState *state, Prepared *prepared, Out *exact, Out *names)
{
    ResolveScratch *scratch = scratch_of(prepared);
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (buffer == NULL) {
        return false;
    }
    In places;
    in_open(&places, &state->store, prepared->track_place.offset,
            prepared->track_place.offset + state->track_count * PLACE_SIZE, buffer, STREAM_BUFFER);
    for (uint32_t track = 0; track < state->track_count; track++) {
        uint8_t place[PLACE_SIZE];
        if (!in_get(&places, place, sizeof place)) {
            return false;
        }
        uint32_t position = builder_get32(place);
        uint32_t length = folded_track_path(state, prepared, track, scratch->folded);
        uint32_t path_hash = hash_update(HASH_START, scratch->folded, length);
        uint32_t name_hash = folded_name_hash(state, prepared, track, scratch->folded);
        if (state->store.io_failed || !put_index_entry(exact, path_hash, position) ||
            !put_index_entry(names, name_hash, position)) {
            return false;
        }
    }
    return true;
}

static bool sort_index(BuilderState *state, Region input, Region *sorted)
{
    SortJob job = {.store = &state->store,
                   .arena = &state->arena,
                   .input = input,
                   .compare = by_hash_then_position,
                   .context = NULL};
    return sort_records(&job, sorted);
}

bool builder_index_paths(BuilderState *state, Prepared *prepared)
{
    uint32_t count = state->track_count;
    prepared->resolve = arena_take(&state->arena, (uint32_t)sizeof(ResolveScratch));
    if (prepared->resolve == NULL ||
        !cache_init(&prepared->lookup, &state->store, &state->arena, LOOKUP_BLOCK, LOOKUP_BLOCKS) ||
        !cache_init(&prepared->positions, &state->store, &state->arena, LOOKUP_BLOCK,
                    LOOKUP_BLOCKS)) {
        return false;
    }
    prepared->index_mark = state->arena.used;
    Region exact_input = store_take(&state->store, count * INDEX_ENTRY);
    Region name_input = store_take(&state->store, count * INDEX_ENTRY);
    uint8_t *exact_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *name_buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (state->store.failed || exact_buffer == NULL || name_buffer == NULL) {
        return false;
    }
    Out exact;
    Out names;
    out_open(&exact, &state->store, exact_input, exact_buffer, STREAM_BUFFER);
    out_open(&names, &state->store, name_input, name_buffer, STREAM_BUFFER);
    bool emitted = emit_index_entries(state, prepared, &exact, &names) && out_close(&exact) &&
                   out_close(&names);
    exact_input.size = exact.written;
    name_input.size = names.written;
    state->arena.used = prepared->index_mark;
    bool sorted = emitted && sort_index(state, exact_input, &prepared->exact) &&
                  sort_index(state, name_input, &prepared->by_name);
    state->arena.used = prepared->index_mark;
    return sorted && !state->store.io_failed;
}

static bool index_entry(Prepared *prepared, Region table, uint32_t index, uint32_t *hash,
                        uint32_t *position)
{
    uint8_t entry[INDEX_ENTRY];
    if (!cache_read(&prepared->lookup, table.offset + index * INDEX_ENTRY, entry, sizeof entry)) {
        return false;
    }
    *hash = builder_get32(entry + 2);
    *position = builder_get32(entry + 6);
    return true;
}

static uint32_t first_with_hash(Prepared *prepared, Region table, uint32_t hash)
{
    uint32_t low = 0;
    uint32_t high = table.size / INDEX_ENTRY;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        uint32_t found = 0;
        uint32_t position = 0;
        if (!index_entry(prepared, table, middle, &found, &position)) {
            return high;
        }
        if (found < hash) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

static bool contains_url(const char *entry)
{
    for (const char *cursor = entry; cursor[0] != '\0'; cursor++) {
        if (cursor[0] == ':' && cursor[1] == '/' && cursor[2] == '/') {
            return true;
        }
    }
    return false;
}

static void split_parts(const char *path, uint32_t length, Parts *parts)
{
    parts->count = 0;
    uint32_t start = 0;
    for (uint32_t index = 0; index <= length && parts->count < MAX_PARTS; index++) {
        if (index < length && path[index] != '/') {
            continue;
        }
        parts->spans[parts->count] = (Span){.start = start, .length = index - start};
        parts->count++;
        start = index + 1;
    }
}

static bool is_dots(const char *text, Span span, uint32_t dots)
{
    if (span.length != dots) {
        return false;
    }
    for (uint32_t index = 0; index < dots; index++) {
        if (text[span.start + index] != '.') {
            return false;
        }
    }
    return true;
}

static uint32_t leading_slashes(const char *path, uint32_t length)
{
    if (length >= 2 && path[0] == '/' && path[1] == '/' && (length < 3 || path[2] != '/')) {
        return 2;
    }
    return length >= 1 && path[0] == '/' ? 1 : 0;
}

static uint32_t keep_components(const char *path, const Parts *raw, uint32_t initial, Span *kept)
{
    uint32_t count = 0;
    for (uint32_t index = 0; index < raw->count; index++) {
        Span span = raw->spans[index];
        if (span.length == 0 || is_dots(path, span, 1)) {
            continue;
        }
        bool parent = is_dots(path, span, 2);
        bool keep_parent =
            (initial == 0 && count == 0) || (count > 0 && is_dots(path, kept[count - 1], 2));
        if (!parent || keep_parent) {
            kept[count++] = span;
        } else if (count > 0) {
            count--;
        }
    }
    return count;
}

static uint32_t copy_span(char *out, uint32_t used, const char *path, Span span)
{
    for (uint32_t byte = 0; byte < span.length && used + 1 < PATH_CAPACITY; byte++) {
        out[used++] = path[span.start + byte];
    }
    return used;
}

static uint32_t normalize(ResolveScratch *scratch, const char *path, uint32_t length)
{
    char *out = scratch->normal;
    split_parts(path, length, &scratch->raw);
    uint32_t initial = leading_slashes(path, length);
    uint32_t count = keep_components(path, &scratch->raw, initial, scratch->kept);
    uint32_t used = 0;
    for (uint32_t index = 0; index < initial; index++) {
        out[used++] = '/';
    }
    for (uint32_t index = 0; index < count; index++) {
        used = copy_span(out, used, path, scratch->kept[index]);
        if (index + 1 < count && used + 1 < PATH_CAPACITY) {
            out[used++] = '/';
        }
    }
    if (used == 0) {
        out[used++] = '.';
    }
    out[used] = '\0';
    return used;
}

static uint32_t find_exact(BuilderState *state, Prepared *prepared, const char *candidate,
                           uint32_t length)
{
    ResolveScratch *scratch = scratch_of(prepared);
    char *folded = scratch->folded;
    uint32_t normal_length = normalize(scratch, candidate, length);
    uint32_t folded_length =
        (uint32_t)case_fold_make(scratch->normal, normal_length, folded, PATH_CAPACITY);
    uint32_t hash = hash_update(HASH_START, folded, folded_length);
    uint32_t entries = prepared->exact.size / INDEX_ENTRY;
    uint32_t result = BUILDER_UNSET;
    for (uint32_t index = first_with_hash(prepared, prepared->exact, hash); index < entries;
         index++) {
        uint32_t found = 0;
        uint32_t position = 0;
        if (!index_entry(prepared, prepared->exact, index, &found, &position) || found != hash) {
            break;
        }
        if (same_folded_path(state, prepared, position, folded, folded_length)) {
            result = position;
        }
    }
    return result;
}

static uint32_t join(const char *folder, const char *path, uint32_t length, char *out)
{
    uint32_t used = 0;
    if (path[0] != '/' && folder[0] != '\0') {
        for (; folder[used] != '\0' && used < PATH_CAPACITY / 2; used++) {
            out[used] = folder[used];
        }
        if (out[used - 1] != '/') {
            out[used++] = '/';
        }
    }
    for (uint32_t index = 0; index < length && used + 1 < PATH_CAPACITY; index++) {
        out[used++] = path[index];
    }
    out[used] = '\0';
    return used;
}

static uint32_t fold_parts(Parts *raw, const char *path, uint32_t length, Parts *parts)
{
    split_parts(path, length, raw);
    uint32_t used = 0;
    parts->count = 0;
    for (uint32_t index = 0; index < raw->count; index++) {
        Span span = raw->spans[index];
        if (span.length == 0 || is_dots(path, span, 1) || is_dots(path, span, 2)) {
            continue;
        }
        uint32_t folded = (uint32_t)case_fold_make(path + span.start, span.length,
                                                   parts->text + used, PATH_CAPACITY - used);
        parts->spans[parts->count++] = (Span){.start = used, .length = folded};
        used += folded < PATH_CAPACITY - used ? folded : 0;
    }
    return parts->count;
}

static bool same_span(const Parts *left, Span a, const Parts *right, Span b)
{
    if (a.length != b.length) {
        return false;
    }
    for (uint32_t index = 0; index < a.length; index++) {
        if (left->text[a.start + index] != right->text[b.start + index]) {
            return false;
        }
    }
    return true;
}

static uint32_t common_suffix(const Parts *left, const Parts *right)
{
    uint32_t length = 0;
    while (length < left->count && length < right->count &&
           same_span(left, left->spans[left->count - 1 - length], right,
                     right->spans[right->count - 1 - length])) {
        length++;
    }
    return length;
}

typedef struct Best {
    uint32_t score;
    uint32_t position;
    uint32_t at_score;
} Best;

static void consider(Best *best, uint32_t score, uint32_t position)
{
    if (best->at_score == 0 || score > best->score) {
        *best = (Best){.score = score, .position = position, .at_score = 1};
    } else if (score == best->score) {
        best->at_score++;
        best->position = position > best->position ? position : best->position;
    }
}

static uint32_t find_by_suffix(BuilderState *state, Prepared *prepared)
{
    ResolveScratch *scratch = scratch_of(prepared);
    const Parts *wanted = &scratch->wanted;
    Span last = wanted->spans[wanted->count - 1];
    uint32_t hash = hash_update(HASH_START, wanted->text + last.start, last.length);
    Best best = {.at_score = 0};
    uint32_t entries = prepared->by_name.size / INDEX_ENTRY;
    for (uint32_t index = first_with_hash(prepared, prepared->by_name, hash); index < entries;
         index++) {
        uint32_t found = 0;
        uint32_t position = 0;
        if (!index_entry(prepared, prepared->by_name, index, &found, &position) || found != hash) {
            break;
        }
        uint32_t length = builder_folded_path(state, prepared, position, scratch->track);
        fold_parts(&scratch->raw, scratch->track, length, &scratch->candidate);
        Parts *candidate = &scratch->candidate;
        if (candidate->count > 0 &&
            same_span(wanted, last, candidate, candidate->spans[candidate->count - 1])) {
            consider(&best, common_suffix(wanted, candidate), position);
        }
    }
    return best.at_score == 1 ? best.position : BUILDER_UNSET;
}

uint32_t builder_resolve_entry(BuilderState *state, Prepared *prepared, const char *folder,
                               const char *entry)
{
    ResolveScratch *scratch = scratch_of(prepared);
    if (contains_url(entry)) {
        return BUILDER_UNSET;
    }
    uint32_t length = 0;
    for (; entry[length] != '\0' && length + 1 < PATH_CAPACITY; length++) {
        scratch->entry[length] = entry[length] == '\\' ? '/' : entry[length];
    }
    scratch->entry[length] = '\0';
    uint32_t joined = join(folder, scratch->entry, length, scratch->joined);
    uint32_t found = find_exact(state, prepared, scratch->joined, joined);
    if (found == BUILDER_UNSET) {
        found = find_exact(state, prepared, scratch->entry, length);
    }
    if (found != BUILDER_UNSET) {
        return found;
    }
    uint32_t parts = fold_parts(&scratch->raw, scratch->entry, length, &scratch->wanted);
    return parts > 0 ? find_by_suffix(state, prepared) : BUILDER_UNSET;
}
