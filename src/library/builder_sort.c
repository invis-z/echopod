#include <stddef.h>

#include "builder_internal.h"
#include "core/borrowed.h"

enum {
    STREAM_BUFFER = 4096,
    MAX_RUNS = 1024,
    MAX_FAN_IN = 32,
    MINIMUM_RUN_BUFFER = 512,
    MINIMUM_CHUNK = 8 * 1024
};

typedef struct Chunk {
    uint8_t *memory;
    uint32_t size;
    uint32_t used;
    uint32_t count;
} Chunk;

typedef struct Runs {
    Region *list;
    uint32_t count;
} Runs;

static BorrowedWord *chunk_offsets(const Chunk *chunk)
{
    return (BorrowedWord *)(chunk->memory + chunk->size - chunk->count * 4);
}

static bool fits(const Chunk *chunk, uint32_t length)
{
    uint32_t need = ((chunk->used + length + 3) & ~3u) + (chunk->count + 1) * 8;
    return need <= chunk->size;
}

static void add_record(Chunk *chunk, const uint8_t *record, uint32_t length)
{
    uint8_t *to = chunk->memory + chunk->used;
    for (uint32_t index = 0; index < length; index++) {
        to[index] = record[index];
    }
    chunk->count++;
    chunk_offsets(chunk)[0] = chunk->used;
    chunk->used += length;
}

static void merge(const Chunk *chunk, const BorrowedWord *from, BorrowedWord *to, uint32_t start,
                  uint32_t middle, uint32_t end, const SortJob *job)
{
    uint32_t left = start;
    uint32_t right = middle;
    for (uint32_t out = start; out < end; out++) {
        bool take_left = left < middle && (right >= end || job->compare(chunk->memory + from[left],
                                                                        chunk->memory + from[right],
                                                                        job->context) <= 0);
        to[out] = take_left ? from[left++] : from[right++];
    }
}

static BorrowedWord *sort_chunk(Chunk *chunk, const SortJob *job)
{
    uint32_t count = chunk->count;
    BorrowedWord *items = chunk_offsets(chunk);
    for (uint32_t low = 0, high = count; low + 1 < high; low++, high--) {
        uint32_t swap = items[low];
        items[low] = items[high - 1];
        items[high - 1] = swap;
    }
    BorrowedWord *scratch = (BorrowedWord *)(chunk->memory + ((chunk->used + 3) & ~3u));
    BorrowedWord *from = items;
    BorrowedWord *to = scratch;
    for (uint32_t width = 1; width < count; width *= 2) {
        for (uint32_t start = 0; start < count; start += 2 * width) {
            uint32_t middle = start + width < count ? start + width : count;
            uint32_t end = start + 2 * width < count ? start + 2 * width : count;
            merge(chunk, from, to, start, middle, end, job);
        }
        BorrowedWord *swap = from;
        from = to;
        to = swap;
    }
    return from;
}

static bool write_run(const Chunk *chunk, const BorrowedWord *order, const SortJob *job,
                      uint8_t *buffer, Runs *runs)
{
    if (runs->count >= MAX_RUNS) {
        job->store->failed = true;
        return false;
    }
    Region region = store_take(job->store, chunk->used);
    if (job->store->failed) {
        return false;
    }
    Out out;
    out_open(&out, job->store, region, buffer, STREAM_BUFFER);
    for (uint32_t index = 0; index < chunk->count; index++) {
        const uint8_t *record = chunk->memory + order[index];
        if (!out_put(&out, record, builder_get16(record))) {
            return false;
        }
    }
    runs->list[runs->count++] = (Region){.offset = region.offset, .size = chunk->used};
    return out_close(&out);
}

static bool make_runs(const SortJob *job, Runs *runs)
{
    Arena *arena = job->arena;
    uint8_t *in_buffer = arena_take(arena, STREAM_BUFFER);
    uint8_t *out_buffer = arena_take(arena, STREAM_BUFFER);
    uint8_t *pending = arena_take(arena, BUILDER_MAX_RECORD);
    uint32_t left = arena_left(arena);
    Chunk chunk = {.memory = arena_take(arena, left), .size = left, .used = 0, .count = 0};
    if (arena->failed || left < MINIMUM_CHUNK) {
        arena->failed = true;
        return false;
    }
    In in;
    in_open(&in, job->store, job->input.offset, job->input.offset + job->input.size, in_buffer,
            STREAM_BUFFER);
    bool have_pending = false;
    for (;;) {
        if (!have_pending) {
            if (in_done(&in)) {
                break;
            }
            if (!in_record(&in, pending)) {
                return false;
            }
        }
        uint32_t length = builder_get16(pending);
        have_pending = false;
        if (!fits(&chunk, length)) {
            if (chunk.count == 0) {
                arena->failed = true;
                return false;
            }
            if (!write_run(&chunk, sort_chunk(&chunk, job), job, out_buffer, runs)) {
                return false;
            }
            chunk.used = 0;
            chunk.count = 0;
            have_pending = true;
            continue;
        }
        add_record(&chunk, pending, length);
    }
    return chunk.count == 0 || write_run(&chunk, sort_chunk(&chunk, job), job, out_buffer, runs);
}

typedef struct Head {
    In in;
    uint8_t *record;
    bool live;
} Head;

static bool advance(Head *head)
{
    head->live = !in_done(&head->in) && in_record(&head->in, head->record);
    return head->live || in_done(&head->in);
}

static bool merge_group(const SortJob *job, const Region *group, uint32_t count, Region *merged)
{
    Arena *arena = job->arena;
    uint32_t mark = arena->used;
    uint32_t total = 0;
    for (uint32_t index = 0; index < count; index++) {
        total += group[index].size;
    }
    Head *heads = arena_take(arena, count * (uint32_t)sizeof(Head));
    uint8_t *out_buffer = arena_take(arena, STREAM_BUFFER);
    for (uint32_t index = 0; heads != NULL && index < count; index++) {
        heads[index].record = arena_take(arena, BUILDER_MAX_RECORD);
    }
    uint32_t share = arena_left(arena) / (count > 0 ? count : 1);
    share &= ~7u;
    if (heads == NULL || out_buffer == NULL || arena->failed || share < MINIMUM_RUN_BUFFER) {
        arena->failed = true;
        return false;
    }
    for (uint32_t index = 0; index < count; index++) {
        in_open(&heads[index].in, job->store, group[index].offset,
                group[index].offset + group[index].size, arena_take(arena, share), share);
        if (!advance(&heads[index])) {
            return false;
        }
    }
    *merged = store_take(job->store, total);
    if (job->store->failed) {
        return false;
    }
    merged->size = total;
    Out out;
    out_open(&out, job->store, *merged, out_buffer, STREAM_BUFFER);
    for (;;) {
        int best = -1;
        for (uint32_t index = 0; index < count; index++) {
            if (heads[index].live &&
                (best < 0 ||
                 job->compare(heads[index].record, heads[best].record, job->context) < 0)) {
                best = (int)index;
            }
        }
        if (best < 0) {
            break;
        }
        if (!out_put(&out, heads[best].record, builder_get16(heads[best].record)) ||
            !advance(&heads[best])) {
            return false;
        }
    }
    bool closed = out_close(&out);
    arena->used = mark;
    return closed;
}

bool sort_records(const SortJob *job, Region *output)
{
    Arena *arena = job->arena;
    uint32_t mark = arena->used;
    *output = (Region){.offset = job->input.offset, .size = 0};
    if (job->input.size == 0) {
        return true;
    }
    Runs runs = {.list = arena_take(arena, MAX_RUNS * (uint32_t)sizeof(Region)), .count = 0};
    uint32_t runs_mark = arena->used;
    if (runs.list == NULL || !make_runs(job, &runs)) {
        return false;
    }
    while (runs.count > 1) {
        arena->used = runs_mark;
        uint32_t merged = 0;
        for (uint32_t start = 0; start < runs.count; start += MAX_FAN_IN) {
            uint32_t count = runs.count - start < MAX_FAN_IN ? runs.count - start : MAX_FAN_IN;
            Region result;
            if (!merge_group(job, runs.list + start, count, &result)) {
                return false;
            }
            runs.list[merged++] = result;
        }
        runs.count = merged;
    }
    *output = runs.list[0];
    arena->used = mark;
    return true;
}

static int by_scatter_index(const uint8_t *left, const uint8_t *right, const void *context)
{
    (void)context;
    uint32_t a = builder_get32(left + 2);
    uint32_t b = builder_get32(right + 2);
    return a < b ? -1 : a > b ? 1 : 0;
}

bool builder_put_entry(Out *out, uint32_t index, const uint8_t *values, uint32_t size)
{
    uint8_t entry[BUILDER_ENTRY_HEADER + BUILDER_ENTRY_VALUES];
    builder_put16(entry, BUILDER_ENTRY_HEADER + size);
    builder_put32(entry + 2, index);
    for (uint32_t byte = 0; byte < size; byte++) {
        entry[BUILDER_ENTRY_HEADER + byte] = values[byte];
    }
    return size <= BUILDER_ENTRY_VALUES && out_put(out, entry, BUILDER_ENTRY_HEADER + size);
}

bool builder_put_pair(Out *out, uint32_t index, uint32_t value)
{
    uint8_t bytes[4];
    builder_put32(bytes, value);
    return builder_put_entry(out, index, bytes, sizeof bytes);
}

bool builder_scatter(BuilderState *state, Region pairs, Region column, uint32_t count)
{
    return builder_scatter_values(state, pairs, column, count, 4);
}

bool builder_scatter_values(BuilderState *state, Region pairs, Region column, uint32_t count,
                            uint32_t size)
{
    uint32_t arena_mark = state->arena.used;
    uint32_t store_mark = state->store.end;
    pairs.size = count * (BUILDER_ENTRY_HEADER + size);
    SortJob job = {.store = &state->store,
                   .arena = &state->arena,
                   .input = pairs,
                   .compare = by_scatter_index,
                   .context = NULL};
    Region sorted;
    if (!sort_records(&job, &sorted)) {
        return false;
    }
    uint8_t *in_buffer = arena_take(&state->arena, STREAM_BUFFER);
    uint8_t *out_buffer = arena_take(&state->arena, STREAM_BUFFER);
    if (in_buffer == NULL || out_buffer == NULL) {
        return false;
    }
    In in;
    in_open(&in, &state->store, sorted.offset, sorted.offset + sorted.size, in_buffer,
            STREAM_BUFFER);
    Out out;
    out_open(&out, &state->store, column, out_buffer, STREAM_BUFFER);
    for (uint32_t index = 0; index < count; index++) {
        uint8_t entry[BUILDER_ENTRY_HEADER + BUILDER_ENTRY_VALUES];
        if (in_done(&in) || !in_get(&in, entry, BUILDER_ENTRY_HEADER + size) ||
            builder_get32(entry + 2) != index ||
            !out_put(&out, entry + BUILDER_ENTRY_HEADER, size)) {
            state->arena.failed = !state->store.io_failed && !state->store.failed;
            return false;
        }
    }
    bool closed = in_done(&in) && out_close(&out);
    state->arena.used = arena_mark;
    state->store.end = store_mark;
    return closed;
}

bool builder_sort(BuilderState *state, Region input, SortCompare compare, Region *sorted)
{
    SortJob job = {.store = &state->store,
                   .arena = &state->arena,
                   .input = input,
                   .compare = compare,
                   .context = NULL};
    return sort_records(&job, sorted);
}

bool builder_open_tail(BuilderState *state, Tail *tail, uint8_t *buffer)
{
    Store *store = &state->store;
    if (store->end > store->io.size) {
        store->failed = true;
        return false;
    }
    tail->start = store->end;
    out_open(&tail->out, store, (Region){.offset = store->end, .size = store->io.size - store->end},
             buffer, STREAM_BUFFER);
    return true;
}

bool builder_close_tail(BuilderState *state, Tail *tail, Region *written)
{
    if (!out_close(&tail->out)) {
        return false;
    }
    *written = (Region){.offset = tail->start, .size = tail->out.written};
    state->store.end = (tail->start + tail->out.written + 3) & ~3u;
    return true;
}

bool builder_open_stream(BuilderState *state, Stream *stream, Region region)
{
    uint8_t *buffer = arena_take(&state->arena, STREAM_BUFFER);
    stream->record = arena_take(&state->arena, BUILDER_MAX_RECORD);
    if (buffer == NULL || stream->record == NULL) {
        return false;
    }
    in_open(&stream->in, &state->store, region.offset, region.offset + region.size, buffer,
            STREAM_BUFFER);
    stream->live = false;
    return true;
}

bool builder_pull(Stream *stream)
{
    if (in_done(&stream->in)) {
        stream->live = false;
        return true;
    }
    stream->live = in_record(&stream->in, stream->record);
    return stream->live;
}
