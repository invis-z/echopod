#include "library/segment_scratch.h"

static uint8_t *zero_fill(uint8_t *to, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        to[index] = 0;
    }
    return to + length;
}

void segment_scratch_start(SegmentScratch *scratch, SegmentStore *store, uint8_t stream)
{
    scratch->store = store;
    scratch->stream = stream;
    scratch->result = SEGMENT_OK;
    for (uint32_t page = 0; page < SEGMENT_SCRATCH_PAGES; page++) {
        scratch->pages[page] = SEGMENT_NONE;
    }
    segment_store_start(store, stream, SEGMENT_POOL_LIBRARY);
}

static bool in_range(uint32_t offset, uint32_t length)
{
    uint64_t end = (uint64_t)offset + length;
    return end <= (uint64_t)SEGMENT_SCRATCH_PAGES * SEGMENT_LIBRARY_SIZE;
}

static bool scratch_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    SegmentScratch *scratch = context;
    if (!in_range(offset, length)) {
        return false;
    }
    uint8_t *to = buffer;
    char path[SEGMENT_PATH_CAPACITY];
    SegmentFiles files = scratch->store->files;
    while (length > 0) {
        uint32_t page = offset / SEGMENT_LIBRARY_SIZE;
        uint32_t within = offset % SEGMENT_LIBRARY_SIZE;
        uint32_t part =
            SEGMENT_LIBRARY_SIZE - within < length ? SEGMENT_LIBRARY_SIZE - within : length;
        if (scratch->pages[page] == SEGMENT_NONE) {
            to = zero_fill(to, part);
        } else {
            segment_store_path(SEGMENT_POOL_LIBRARY, scratch->pages[page], path);
            if (!files.read(files.context, path, within, to, part)) {
                return false;
            }
            to += part;
        }
        offset += part;
        length -= part;
    }
    return true;
}

static bool map_page(SegmentScratch *scratch, uint32_t page)
{
    if (scratch->pages[page] != SEGMENT_NONE) {
        return true;
    }
    uint16_t number = SEGMENT_NONE;
    scratch->result = segment_store_grow(scratch->store, scratch->stream, &number);
    if (scratch->result != SEGMENT_OK) {
        return false;
    }
    scratch->pages[page] = number;
    return true;
}

static bool scratch_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    SegmentScratch *scratch = context;
    if (!in_range(offset, length) || scratch->result != SEGMENT_OK) {
        return false;
    }
    const uint8_t *from = bytes;
    char path[SEGMENT_PATH_CAPACITY];
    SegmentFiles files = scratch->store->files;
    while (length > 0) {
        uint32_t page = offset / SEGMENT_LIBRARY_SIZE;
        uint32_t within = offset % SEGMENT_LIBRARY_SIZE;
        uint32_t part =
            SEGMENT_LIBRARY_SIZE - within < length ? SEGMENT_LIBRARY_SIZE - within : length;
        if (!map_page(scratch, page)) {
            return false;
        }
        segment_store_path(SEGMENT_POOL_LIBRARY, scratch->pages[page], path);
        if (!files.write(files.context, path, within, from, part)) {
            scratch->result = SEGMENT_FAILED;
            return false;
        }
        from += part;
        offset += part;
        length -= part;
    }
    return true;
}

BuilderScratch segment_scratch_io(SegmentScratch *scratch)
{
    return (BuilderScratch){.read = scratch_read,
                            .write = scratch_write,
                            .context = scratch,
                            .size = (uint32_t)(SEGMENT_SCRATCH_PAGES - 1) * SEGMENT_LIBRARY_SIZE};
}

void segment_scratch_finish(SegmentScratch *scratch)
{
    segment_store_drop(scratch->store, scratch->stream);
}
