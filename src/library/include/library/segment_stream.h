#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/byte_source.h"
#include "library/library_builder.h"
#include "library/segment_store.h"

typedef struct SegmentStreamIo {
    SegmentStore *store;
    uint8_t stream;
    SegmentResult result;
} SegmentStreamIo;

ByteSource segment_stream_source(SegmentStreamIo *io);
ByteSink segment_stream_sink(SegmentStreamIo *io);
