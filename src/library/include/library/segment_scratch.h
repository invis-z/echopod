#pragma once

#include <stdint.h>

#include "library/library_builder.h"
#include "library/segment_store.h"

enum {
    SEGMENT_SCRATCH_PAGES = 1024
};

typedef struct SegmentScratch {
    SegmentStore *store;
    uint8_t stream;
    SegmentResult result;
    uint16_t pages[SEGMENT_SCRATCH_PAGES];
} SegmentScratch;

void segment_scratch_start(SegmentScratch *scratch, SegmentStore *store, uint8_t stream);
BuilderScratch segment_scratch_io(SegmentScratch *scratch);
void segment_scratch_finish(SegmentScratch *scratch);
