#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/library.h"
#include "library/library_scan.h"
#include "library/segment_store.h"

typedef struct CoverProgress {
    void (*report)(void *context, uint32_t done, uint32_t total);
    void *context;
} CoverProgress;

typedef struct CoverJob {
    ScanFileSystem files;
    const Library *library;
    SegmentStore *store;
    void *memory;
    uint32_t size;
    CoverProgress progress;
} CoverJob;

typedef struct CoverStats {
    uint32_t albums;
    uint32_t covers;
    uint32_t reused;
    uint32_t missing;
} CoverStats;

ScanResult cover_build(const CoverJob *job, CoverStats *stats);
