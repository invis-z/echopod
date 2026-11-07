#include "library/library_rebuild.h"

#include <stddef.h>

#include "library/segment_scratch.h"
#include "library/segment_stream.h"

enum {
    ALIGNMENT = 8
};

static uint8_t records_stream(const SegmentStore *store)
{
    uint32_t value = segment_store_value(store, LIBRARY_VALUE_RECORDS);
    return value == LIBRARY_STREAM_OTHER_RECORDS ? LIBRARY_STREAM_OTHER_RECORDS
                                                 : LIBRARY_STREAM_RECORDS;
}

static bool out_of_room(SegmentResult result)
{
    return result == SEGMENT_NO_SPACE || result == SEGMENT_FULL;
}

static ScanResult store_result(ScanResult result, const SegmentResult *results, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        if (out_of_room(results[index])) {
            return SCAN_NO_ROOM;
        }
    }
    return result;
}

ScanResult library_rebuild(const RebuildJob *job, ScanStats *stats)
{
    SegmentStore *store = job->store;
    uintptr_t start = (uintptr_t)job->scan.memory;
    uint32_t skip = (uint32_t)((ALIGNMENT - start % ALIGNMENT) % ALIGNMENT);
    uint32_t taken =
        skip + (uint32_t)((sizeof(SegmentScratch) + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1));
    *stats = (ScanStats){.files = 0};
    if (job->scan.memory == NULL || job->scan.size < taken) {
        return SCAN_OUT_OF_MEMORY;
    }
    SegmentScratch *scratch = (SegmentScratch *)((uint8_t *)job->scan.memory + skip);
    uint8_t previous = records_stream(store);
    uint8_t next =
        previous == LIBRARY_STREAM_RECORDS ? LIBRARY_STREAM_OTHER_RECORDS : LIBRARY_STREAM_RECORDS;
    segment_store_begin(store);
    segment_store_start(store, next, SEGMENT_POOL_LIBRARY);
    segment_store_start(store, LIBRARY_STREAM_INDEX, SEGMENT_POOL_LIBRARY);
    segment_scratch_start(scratch, store, LIBRARY_STREAM_SCRATCH);
    SegmentStreamIo previous_io = {.store = store, .stream = previous};
    SegmentStreamIo next_io = {.store = store, .stream = next};
    SegmentStreamIo index_io = {.store = store, .stream = LIBRARY_STREAM_INDEX};
    ScanCache cache = {.previous = segment_stream_source(&previous_io),
                       .previous_length = segment_store_length(store, previous),
                       .next = segment_stream_sink(&next_io)};
    ScanJob scan = job->scan;
    scan.memory = (uint8_t *)job->scan.memory + taken;
    scan.size = job->scan.size - taken;
    scan.scratch = segment_scratch_io(scratch);
    scan.sink = segment_stream_sink(&index_io);
    scan.capacity = 0;
    scan.cache = &cache;
    scan.wide = true;
    ScanResult result = library_scan(&scan, stats);
    segment_scratch_finish(scratch);
    SegmentResult results[4] = {scratch->result, next_io.result, index_io.result, SEGMENT_OK};
    if (result == SCAN_OK) {
        segment_store_set_region(
            store, LIBRARY_REGION_INDEX,
            (SegmentRegion){.stream = LIBRARY_STREAM_INDEX, .offset = 0, .length = stats->length});
        segment_store_set_value(store, LIBRARY_VALUE_RECORDS, next);
        segment_store_drop(store, previous);
        results[3] = segment_store_commit(store);
        result = results[3] == SEGMENT_OK ? SCAN_OK : SCAN_WRITE_FAILED;
    }
    result = store_result(result, results, 4);
    if (result != SCAN_OK) {
        segment_store_cancel(store);
    }
    return result;
}
