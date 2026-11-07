#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/segment_store.h"

enum {
    COVER_FULL_SIZE = 112,
    COVER_HALF_SIZE = 56,
    COVER_FULL_PIXELS = COVER_FULL_SIZE * COVER_FULL_SIZE,
    COVER_HALF_PIXELS = COVER_HALF_SIZE * COVER_HALF_SIZE,
    COVER_FULL_BYTES = COVER_FULL_PIXELS * 2,
    COVER_HALF_BYTES = COVER_HALF_PIXELS * 2,
    COVER_RECORD_BYTES = COVER_FULL_BYTES + COVER_HALF_BYTES,
    COVERS_PER_SEGMENT = SEGMENT_COVERS_SIZE / COVER_RECORD_BYTES,
    COVER_ROW_BYTES = 8
};

#define COVER_NONE UINT32_C(0xFFFFFFFF)

typedef enum CoverSize {
    COVER_FULL,
    COVER_HALF
} CoverSize;

typedef struct Covers {
    const SegmentStore *store;
    uint8_t stream;
    uint32_t albums;
    uint32_t slots;
} Covers;

uint32_t covers_offset(uint32_t slot);
bool covers_open(Covers *covers, const SegmentStore *store);
bool covers_slot(const Covers *covers, uint32_t rank, uint32_t *slot);
bool covers_read(const Covers *covers, uint32_t rank, CoverSize size, uint16_t *pixels);
