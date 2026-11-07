#pragma once

#include "library/library_scan.h"
#include "library/segment_store.h"

enum {
    LIBRARY_STREAM_INDEX = 1,
    LIBRARY_STREAM_RECORDS = 2,
    LIBRARY_STREAM_OTHER_RECORDS = 3,
    LIBRARY_STREAM_SCRATCH = 7,
    LIBRARY_REGION_INDEX = 0,
    LIBRARY_VALUE_RECORDS = 0
};

typedef struct RebuildJob {
    ScanJob scan;
    SegmentStore *store;
} RebuildJob;

ScanResult library_rebuild(const RebuildJob *job, ScanStats *stats);
