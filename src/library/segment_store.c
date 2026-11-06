#include "library/segment_store.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    MANIFEST_MAGIC = 0x4D4C5052,
    MANIFEST_VERSION = 1,
    PATH_DIGITS = 4,
    STREAM_BYTES = 10,
    REGION_BYTES = 9,
    MANIFEST_BODY = 4 + 2 + 2 + 4 + 2 * SEGMENT_POOL_COUNT + STREAM_BYTES * SEGMENT_STREAM_COUNT +
                    REGION_BYTES * SEGMENT_REGION_COUNT + 4 * SEGMENT_VALUE_COUNT +
                    2 * SEGMENT_TOTAL_CAPACITY
};

static const uint32_t POOL_SIZE[SEGMENT_POOL_COUNT] = {SEGMENT_LIBRARY_SIZE, SEGMENT_COVERS_SIZE};
static const uint16_t POOL_CAPACITY[SEGMENT_POOL_COUNT] = {SEGMENT_LIBRARY_CAPACITY,
                                                           SEGMENT_COVERS_CAPACITY};
static const uint16_t POOL_BASE[SEGMENT_POOL_COUNT] = {0, SEGMENT_LIBRARY_CAPACITY};
static const char *const POOL_PREFIX[SEGMENT_POOL_COUNT] = {"/.rippl/library/L",
                                                            "/.rippl/covers/C"};

static void segment_path(uint8_t pool, uint16_t number, char *path)
{
    const char *prefix = POOL_PREFIX[pool];
    uint32_t length = 0;
    while (prefix[length] != '\0') {
        path[length] = prefix[length];
        length++;
    }
    for (int digit = PATH_DIGITS - 1; digit >= 0; digit--) {
        path[length + (uint32_t)digit] = (char)('0' + number % 10);
        number /= 10;
    }
    length += PATH_DIGITS;
    const char suffix[] = ".BIN";
    for (uint32_t index = 0; index < sizeof suffix; index++) {
        path[length + index] = suffix[index];
    }
}

static uint16_t *next_of(SegmentStore *store, uint8_t pool, uint16_t number)
{
    return &store->next[POOL_BASE[pool] + number];
}

static uint16_t follow(const SegmentStore *store, uint8_t pool, uint16_t number)
{
    return store->next[POOL_BASE[pool] + number];
}

static uint16_t segment_at(const SegmentStore *store, const SegmentStream *stream, uint32_t index)
{
    uint16_t number = stream->head;
    for (uint32_t step = 0; step < index; step++) {
        number = follow(store, stream->pool, number);
    }
    return number;
}

static bool read_stream(const SegmentStore *store, const SegmentStream *stream, uint32_t offset,
                        void *buffer, uint32_t length)
{
    if (offset > stream->length || length > stream->length - offset) {
        return false;
    }
    uint8_t *to = buffer;
    uint32_t size = POOL_SIZE[stream->pool];
    char path[SEGMENT_PATH_CAPACITY];
    while (length > 0) {
        uint32_t within = offset % size;
        uint32_t part = size - within < length ? size - within : length;
        segment_path(stream->pool, segment_at(store, stream, offset / size), path);
        if (!store->files.read(store->files.context, path, within, to, part)) {
            return false;
        }
        to += part;
        offset += part;
        length -= part;
    }
    return true;
}

static bool is_used(const SegmentStore *store, uint32_t index)
{
    return (store->used[index / 8] >> (index % 8)) & 1u;
}

static void set_used(SegmentStore *store, uint32_t index)
{
    store->used[index / 8] |= (uint8_t)(1u << (index % 8));
}

static void clear_used(SegmentStore *store)
{
    for (uint32_t index = 0; index < sizeof store->used; index++) {
        store->used[index] = 0;
    }
}

static bool mark_chain(SegmentStore *store, const SegmentStream *stream, const uint16_t *reserved,
                       bool exclusive)
{
    uint16_t number = stream->head;
    for (uint32_t index = 0; index < stream->count; index++) {
        if (number >= reserved[stream->pool]) {
            return false;
        }
        uint32_t slot = POOL_BASE[stream->pool] + number;
        if (exclusive && is_used(store, slot)) {
            return false;
        }
        set_used(store, slot);
        number = follow(store, stream->pool, number);
    }
    return true;
}

static void mark_state(SegmentStore *store, const SegmentState *state)
{
    for (int stream = 0; stream < SEGMENT_STREAM_COUNT; stream++) {
        mark_chain(store, &state->streams[stream], state->reserved, false);
    }
}

static SegmentResult reserve_segment(SegmentStore *store, uint8_t pool, uint16_t number)
{
    char path[SEGMENT_PATH_CAPACITY];
    segment_path(pool, number, path);
    SegmentFileResult result = store->files.reserve(store->files.context, path, POOL_SIZE[pool]);
    if (result == SEGMENT_FILE_NO_SPACE) {
        return SEGMENT_NO_SPACE;
    }
    return result == SEGMENT_FILE_OK ? SEGMENT_OK : SEGMENT_FAILED;
}

static SegmentResult allocate(SegmentStore *store, uint8_t pool, uint16_t *number)
{
    clear_used(store);
    mark_state(store, &store->committed);
    mark_state(store, &store->working);
    uint16_t reserved = store->working.reserved[pool];
    for (uint16_t candidate = 0; candidate < reserved; candidate++) {
        if (!is_used(store, POOL_BASE[pool] + candidate)) {
            *number = candidate;
            return SEGMENT_OK;
        }
    }
    if (reserved >= POOL_CAPACITY[pool]) {
        return SEGMENT_FULL;
    }
    SegmentResult result = reserve_segment(store, pool, reserved);
    if (result == SEGMENT_OK) {
        store->working.reserved[pool] = (uint16_t)(reserved + 1);
        *number = reserved;
    }
    return result;
}

static SegmentResult grow(SegmentStore *store, SegmentStream *stream)
{
    uint16_t number = 0;
    SegmentResult result = allocate(store, stream->pool, &number);
    if (result != SEGMENT_OK) {
        return result;
    }
    if (stream->count == 0) {
        stream->head = number;
    } else {
        *next_of(store, stream->pool, segment_at(store, stream, stream->count - 1u)) = number;
    }
    *next_of(store, stream->pool, number) = SEGMENT_NONE;
    stream->count++;
    return SEGMENT_OK;
}

static bool write_stream(SegmentStore *store, const SegmentStream *stream, uint32_t offset,
                         const uint8_t *bytes, uint32_t length)
{
    uint32_t size = POOL_SIZE[stream->pool];
    char path[SEGMENT_PATH_CAPACITY];
    while (length > 0) {
        uint32_t within = offset % size;
        uint32_t part = size - within < length ? size - within : length;
        segment_path(stream->pool, segment_at(store, stream, offset / size), path);
        if (!store->files.write(store->files.context, path, within, bytes, part)) {
            return false;
        }
        bytes += part;
        offset += part;
        length -= part;
    }
    return true;
}

typedef struct Chunk {
    SegmentStore *store;
    uint32_t base;
    uint32_t position;
    uint32_t used;
    uint32_t hash;
    bool ok;
    uint8_t *bytes;
} Chunk;

static void chunk_flush(Chunk *chunk)
{
    if (chunk->ok && chunk->used > 0) {
        SegmentFiles files = chunk->store->files;
        chunk->ok = files.write(files.context, SEGMENT_MANIFEST_PATH, chunk->base + chunk->position,
                                chunk->bytes, chunk->used);
    }
    chunk->position += chunk->used;
    chunk->used = 0;
}

static void put_bytes(Chunk *chunk, const uint8_t *bytes, uint32_t length, bool hashed)
{
    if (hashed) {
        chunk->hash = hash_update(chunk->hash, bytes, length);
    }
    for (uint32_t index = 0; index < length; index++) {
        if (chunk->used == SEGMENT_CHUNK_SIZE) {
            chunk_flush(chunk);
        }
        chunk->bytes[chunk->used++] = bytes[index];
    }
}

static void put_u8(Chunk *chunk, uint8_t value)
{
    put_bytes(chunk, &value, 1, true);
}

static void put_u16(Chunk *chunk, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    put_bytes(chunk, bytes, sizeof bytes, true);
}

static void put_u32(Chunk *chunk, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 24)};
    put_bytes(chunk, bytes, sizeof bytes, true);
}

static bool write_manifest(SegmentStore *store, const SegmentState *state, uint8_t slot)
{
    Chunk chunk = {.store = store,
                   .base = slot * (uint32_t)SEGMENT_MANIFEST_SLOT_SIZE,
                   .hash = HASH_START,
                   .ok = true,
                   .bytes = store->chunk};
    put_u32(&chunk, MANIFEST_MAGIC);
    put_u16(&chunk, MANIFEST_VERSION);
    put_u16(&chunk, 0);
    put_u32(&chunk, state->sequence);
    for (int pool = 0; pool < SEGMENT_POOL_COUNT; pool++) {
        put_u16(&chunk, state->reserved[pool]);
    }
    for (int index = 0; index < SEGMENT_STREAM_COUNT; index++) {
        const SegmentStream *stream = &state->streams[index];
        put_u8(&chunk, stream->pool);
        put_u8(&chunk, 0);
        put_u16(&chunk, stream->head);
        put_u16(&chunk, stream->count);
        put_u32(&chunk, stream->length);
    }
    for (int index = 0; index < SEGMENT_REGION_COUNT; index++) {
        put_u8(&chunk, state->regions[index].stream);
        put_u32(&chunk, state->regions[index].offset);
        put_u32(&chunk, state->regions[index].length);
    }
    for (int index = 0; index < SEGMENT_VALUE_COUNT; index++) {
        put_u32(&chunk, state->values[index]);
    }
    for (int index = 0; index < SEGMENT_TOTAL_CAPACITY; index++) {
        put_u16(&chunk, store->next[index]);
    }
    uint32_t hash = chunk.hash;
    uint8_t check[4] = {(uint8_t)hash, (uint8_t)(hash >> 8), (uint8_t)(hash >> 16),
                        (uint8_t)(hash >> 24)};
    put_bytes(&chunk, check, sizeof check, false);
    chunk_flush(&chunk);
    return chunk.ok;
}

typedef struct Reader {
    const SegmentFiles *files;
    uint32_t base;
    uint32_t position;
    uint32_t used;
    uint32_t filled;
    uint32_t hash;
    bool ok;
    uint8_t *bytes;
} Reader;

static void get_bytes(Reader *reader, uint8_t *bytes, uint32_t length, bool hashed)
{
    for (uint32_t index = 0; index < length; index++) {
        if (reader->used == reader->filled) {
            reader->position += reader->filled;
            reader->used = 0;
            reader->filled = SEGMENT_CHUNK_SIZE;
            reader->ok =
                reader->ok && reader->files->read(reader->files->context, SEGMENT_MANIFEST_PATH,
                                                  reader->base + reader->position, reader->bytes,
                                                  SEGMENT_CHUNK_SIZE);
        }
        bytes[index] = reader->ok ? reader->bytes[reader->used] : 0;
        reader->used++;
    }
    if (hashed) {
        reader->hash = hash_update(reader->hash, bytes, length);
    }
}

static uint8_t get_u8(Reader *reader)
{
    uint8_t value = 0;
    get_bytes(reader, &value, 1, true);
    return value;
}

static uint16_t get_u16(Reader *reader)
{
    uint8_t bytes[2];
    get_bytes(reader, bytes, sizeof bytes, true);
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static uint32_t decode_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static uint32_t get_u32(Reader *reader)
{
    uint8_t bytes[4];
    get_bytes(reader, bytes, sizeof bytes, true);
    return decode_u32(bytes);
}

static bool read_manifest(SegmentStore *store, uint8_t slot, SegmentState *state, bool keep_next)
{
    Reader reader = {.files = &store->files,
                     .base = slot * (uint32_t)SEGMENT_MANIFEST_SLOT_SIZE,
                     .hash = HASH_START,
                     .ok = true,
                     .bytes = store->chunk};
    bool valid = get_u32(&reader) == MANIFEST_MAGIC;
    valid = get_u16(&reader) == MANIFEST_VERSION && valid;
    get_u16(&reader);
    state->sequence = get_u32(&reader);
    for (int pool = 0; pool < SEGMENT_POOL_COUNT; pool++) {
        state->reserved[pool] = get_u16(&reader);
    }
    for (int index = 0; index < SEGMENT_STREAM_COUNT; index++) {
        SegmentStream *stream = &state->streams[index];
        stream->pool = get_u8(&reader);
        get_u8(&reader);
        stream->head = get_u16(&reader);
        stream->count = get_u16(&reader);
        stream->length = get_u32(&reader);
    }
    for (int index = 0; index < SEGMENT_REGION_COUNT; index++) {
        state->regions[index].stream = get_u8(&reader);
        state->regions[index].offset = get_u32(&reader);
        state->regions[index].length = get_u32(&reader);
    }
    for (int index = 0; index < SEGMENT_VALUE_COUNT; index++) {
        state->values[index] = get_u32(&reader);
    }
    for (int index = 0; index < SEGMENT_TOTAL_CAPACITY; index++) {
        uint16_t value = get_u16(&reader);
        if (keep_next) {
            store->next[index] = value;
        }
    }
    uint32_t hash = reader.hash;
    uint8_t check[4];
    get_bytes(&reader, check, sizeof check, false);
    return reader.ok && valid && decode_u32(check) == hash;
}

static bool stream_valid(const SegmentStream *stream)
{
    if (stream->pool >= SEGMENT_POOL_COUNT) {
        return false;
    }
    uint32_t size = POOL_SIZE[stream->pool];
    if (stream->count == 0) {
        return stream->length == 0;
    }
    uint32_t below = (stream->count - 1u) * size;
    return stream->count <= POOL_CAPACITY[stream->pool] && stream->length > below &&
           stream->length - below <= size;
}

static bool state_valid(SegmentStore *store, const SegmentState *state)
{
    for (int pool = 0; pool < SEGMENT_POOL_COUNT; pool++) {
        if (state->reserved[pool] > POOL_CAPACITY[pool]) {
            return false;
        }
    }
    clear_used(store);
    for (int index = 0; index < SEGMENT_STREAM_COUNT; index++) {
        const SegmentStream *stream = &state->streams[index];
        if (!stream_valid(stream) || !mark_chain(store, stream, state->reserved, true)) {
            return false;
        }
    }
    for (int index = 0; index < SEGMENT_REGION_COUNT; index++) {
        const SegmentRegion *region = &state->regions[index];
        if (region->stream >= SEGMENT_STREAM_COUNT) {
            return false;
        }
        uint32_t length = state->streams[region->stream].length;
        if (region->offset > length || region->length > length - region->offset) {
            return false;
        }
    }
    return true;
}

static void empty_state(SegmentState *state)
{
    *state = (SegmentState){0};
    for (int index = 0; index < SEGMENT_STREAM_COUNT; index++) {
        state->streams[index].head = SEGMENT_NONE;
    }
}

static bool newer(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) > 0;
}

static void share_all(SegmentStore *store)
{
    for (int index = 0; index < SEGMENT_STREAM_COUNT; index++) {
        store->shared[index] = true;
    }
}

static bool load_slot(SegmentStore *store, uint8_t slot)
{
    if (!read_manifest(store, slot, &store->committed, true) ||
        !state_valid(store, &store->committed)) {
        empty_state(&store->committed);
        return false;
    }
    store->slot = slot;
    store->found = true;
    return true;
}

void segment_store_open(SegmentStore *store, SegmentFiles files)
{
    store->files = files;
    store->found = false;
    store->slot = 1;
    bool first_ok = read_manifest(store, 0, &store->working, false);
    bool second_ok = read_manifest(store, 1, &store->committed, false);
    bool second_first =
        second_ok && (!first_ok || newer(store->committed.sequence, store->working.sequence));
    empty_state(&store->committed);
    uint8_t order[2] = {0, 1};
    if (second_first) {
        order[0] = 1;
        order[1] = 0;
    }
    for (int attempt = 0; attempt < 2 && !store->found; attempt++) {
        bool ok = order[attempt] == 0 ? first_ok : second_ok;
        if (ok) {
            load_slot(store, order[attempt]);
        }
    }
    store->working = store->committed;
    share_all(store);
}

bool segment_store_found(const SegmentStore *store)
{
    return store->found;
}

uint32_t segment_store_sequence(const SegmentStore *store)
{
    return store->committed.sequence;
}

uint32_t segment_store_length(const SegmentStore *store, uint8_t stream)
{
    return stream < SEGMENT_STREAM_COUNT ? store->committed.streams[stream].length : 0;
}

bool segment_store_read(const SegmentStore *store, uint8_t stream, uint32_t offset, void *buffer,
                        uint32_t length)
{
    return stream < SEGMENT_STREAM_COUNT &&
           read_stream(store, &store->committed.streams[stream], offset, buffer, length);
}

SegmentRegion segment_store_region(const SegmentStore *store, uint8_t region)
{
    return region < SEGMENT_REGION_COUNT ? store->committed.regions[region] : (SegmentRegion){0};
}

uint32_t segment_store_value(const SegmentStore *store, uint8_t index)
{
    return index < SEGMENT_VALUE_COUNT ? store->committed.values[index] : 0;
}

void segment_store_begin(SegmentStore *store)
{
    store->working = store->committed;
    share_all(store);
}

void segment_store_start(SegmentStore *store, uint8_t stream, uint8_t pool)
{
    if (stream >= SEGMENT_STREAM_COUNT || pool >= SEGMENT_POOL_COUNT) {
        return;
    }
    store->working.streams[stream] = (SegmentStream){.pool = pool, .head = SEGMENT_NONE};
    store->shared[stream] = false;
}

void segment_store_drop(SegmentStore *store, uint8_t stream)
{
    if (stream < SEGMENT_STREAM_COUNT) {
        segment_store_start(store, stream, store->working.streams[stream].pool);
    }
}

uint32_t segment_store_working_length(const SegmentStore *store, uint8_t stream)
{
    return stream < SEGMENT_STREAM_COUNT ? store->working.streams[stream].length : 0;
}

SegmentResult segment_store_append(SegmentStore *store, uint8_t stream, const void *bytes,
                                   uint32_t length)
{
    if (stream >= SEGMENT_STREAM_COUNT) {
        return SEGMENT_REFUSED;
    }
    SegmentStream *target = &store->working.streams[stream];
    uint32_t size = POOL_SIZE[target->pool];
    const uint8_t *from = bytes;
    while (length > 0) {
        if (target->length == target->count * size) {
            SegmentResult result = grow(store, target);
            if (result != SEGMENT_OK) {
                return result;
            }
        }
        uint32_t room = target->count * size - target->length;
        uint32_t part = room < length ? room : length;
        if (!write_stream(store, target, target->length, from, part)) {
            return SEGMENT_FAILED;
        }
        target->length += part;
        from += part;
        length -= part;
    }
    return SEGMENT_OK;
}

SegmentResult segment_store_write(SegmentStore *store, uint8_t stream, uint32_t offset,
                                  const void *bytes, uint32_t length)
{
    if (stream >= SEGMENT_STREAM_COUNT) {
        return SEGMENT_REFUSED;
    }
    const SegmentStream *target = &store->working.streams[stream];
    if (offset > target->length || length > target->length - offset) {
        return SEGMENT_REFUSED;
    }
    if (store->shared[stream] && offset < store->committed.streams[stream].length) {
        return SEGMENT_REFUSED;
    }
    return write_stream(store, target, offset, bytes, length) ? SEGMENT_OK : SEGMENT_FAILED;
}

bool segment_store_read_working(const SegmentStore *store, uint8_t stream, uint32_t offset,
                                void *buffer, uint32_t length)
{
    return stream < SEGMENT_STREAM_COUNT &&
           read_stream(store, &store->working.streams[stream], offset, buffer, length);
}

void segment_store_set_region(SegmentStore *store, uint8_t region, SegmentRegion value)
{
    if (region < SEGMENT_REGION_COUNT) {
        store->working.regions[region] = value;
    }
}

void segment_store_set_value(SegmentStore *store, uint8_t index, uint32_t value)
{
    if (index < SEGMENT_VALUE_COUNT) {
        store->working.values[index] = value;
    }
}

SegmentResult segment_store_commit(SegmentStore *store)
{
    store->working.sequence = store->committed.sequence + 1u;
    if (!state_valid(store, &store->working)) {
        store->working.sequence = store->committed.sequence;
        return SEGMENT_REFUSED;
    }
    if (!store->found) {
        SegmentFileResult reserved = store->files.reserve(
            store->files.context, SEGMENT_MANIFEST_PATH, SEGMENT_MANIFEST_SIZE);
        if (reserved != SEGMENT_FILE_OK) {
            store->working.sequence = store->committed.sequence;
            return reserved == SEGMENT_FILE_NO_SPACE ? SEGMENT_NO_SPACE : SEGMENT_FAILED;
        }
    }
    uint8_t slot = (uint8_t)(store->slot ^ 1u);
    if (!write_manifest(store, &store->working, slot)) {
        store->working.sequence = store->committed.sequence;
        return SEGMENT_FAILED;
    }
    store->committed = store->working;
    store->slot = slot;
    store->found = true;
    share_all(store);
    return SEGMENT_OK;
}

void segment_store_cancel(SegmentStore *store)
{
    segment_store_begin(store);
}
