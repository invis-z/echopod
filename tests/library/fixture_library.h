#pragma once

#include <stdint.h>

#include "library/library.h"
#include "library/segment_store.h"
#include "memory_source.h"

enum {
    FIXTURE_TRACKS = 8,
    FIXTURE_ALBUMS = 5,
    FIXTURE_GROUPS = 4,
    FIXTURE_FOLDERS = 10,
    FIXTURE_CAPACITY = 8192,
    FIXTURE_RED_ALBUM = 0,
    FIXTURE_BLUE_ALBUM = 2,
    FIXTURE_RED = 0xF800,
    FIXTURE_BLUE = 0x001F
};

typedef struct FixtureLibrary {
    uint8_t bytes[FIXTURE_CAPACITY];
    uint32_t size;
    MemorySource memory;
    Library library;
} FixtureLibrary;

const uint8_t *fixture_bytes(uint32_t *size);
Library *fixture_library_open(FixtureLibrary *fixture);
bool fixture_covers_write(SegmentStore *store, const Library *library);
bool fixture_covers_none(SegmentStore *store, uint32_t albums);
