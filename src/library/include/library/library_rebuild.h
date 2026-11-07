#pragma once

#include "library/cover_build.h"
#include "library/library_scan.h"
#include "library/segment_store.h"
#include "library/store_layout.h"

typedef struct RebuildJob {
    ScanJob scan;
    SegmentStore *store;
    CoverProgress covers;
} RebuildJob;

typedef struct RebuildStats {
    ScanStats scan;
    ScanResult cover_result;
    CoverStats covers;
} RebuildStats;

ScanResult library_rebuild(const RebuildJob *job, RebuildStats *stats);
