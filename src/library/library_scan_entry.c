#include "library/library_rebuild.h"
#include "library/library_scan.h"

const LibraryScanEntry LIBRARY_SCAN_ENTRY = {
    .magic = LIBRARY_SCAN_MAGIC,
    .rebuild = library_rebuild,
};
