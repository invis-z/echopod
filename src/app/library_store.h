#pragma once

#include <stdbool.h>

#include "library/block_cache.h"
#include "library/library.h"
#include "library/segment_store.h"
#include "library/segment_stream.h"

typedef struct LibraryStore {
    SegmentStore segments;
    SegmentStreamIo io;
    BlockCache cache;
    Library library;
    bool available;
} LibraryStore;

SegmentFiles library_store_files(void);
bool library_store_open(LibraryStore *store);
void library_store_close(LibraryStore *store);
