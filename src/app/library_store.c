#include "library_store.h"

#include <stddef.h>

#include "error_log.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "library/library_rebuild.h"

typedef struct CardFiles {
    HalFile file;
    bool open;
    char path[SEGMENT_PATH_CAPACITY];
} CardFiles;

static CardFiles card;

static bool same_path(const char *left, const char *right)
{
    size_t index = 0;
    while (left[index] != '\0' && left[index] == right[index]) {
        index++;
    }
    return left[index] == right[index];
}

static void forget(void)
{
    if (card.open) {
        hal_file_close(&card.file);
    }
    card.open = false;
}

static SegmentFileResult reserve(void *context, const char *path, uint32_t size)
{
    (void)context;
    hal_system_keep_alive();
    forget();
    HalReserveResult result = hal_file_reserve(path, size);
    if (result == HAL_RESERVE_EXISTS || result == HAL_RESERVE_CREATED) {
        return SEGMENT_FILE_OK;
    }
    return result == HAL_RESERVE_NO_SPACE ? SEGMENT_FILE_NO_SPACE : SEGMENT_FILE_FAILED;
}

static bool read(void *context, const char *path, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    hal_system_keep_alive();
    if (!card.open || !same_path(card.path, path)) {
        forget();
        size_t index = 0;
        for (; path[index] != '\0' && index + 1 < sizeof card.path; index++) {
            card.path[index] = path[index];
        }
        card.path[index] = '\0';
        card.open = hal_file_open(&card.file, path);
        if (!card.open && !same_path(path, SEGMENT_MANIFEST_PATH)) {
            error_log_add("can't open", path);
        }
    }
    return card.open && hal_file_read(card.file, offset, buffer, length);
}

static bool write(void *context, const char *path, uint32_t offset, const void *bytes,
                  uint32_t length)
{
    (void)context;
    hal_system_keep_alive();
    return hal_file_write_at(path, offset, bytes, length);
}

SegmentFiles library_store_files(void)
{
    return (SegmentFiles){.reserve = reserve, .read = read, .write = write, .context = NULL};
}

void library_store_close(LibraryStore *store)
{
    store->available = false;
    forget();
}

bool library_store_open(LibraryStore *store)
{
    library_store_close(store);
    store->library = (Library){.source = {.read = NULL, .context = NULL}};
    segment_store_open(&store->segments, library_store_files());
    SegmentRegion region = segment_store_region(&store->segments, LIBRARY_REGION_INDEX);
    if (!segment_store_found(&store->segments) || region.stream != LIBRARY_STREAM_INDEX ||
        region.offset != 0) {
        return false;
    }
    store->io = (SegmentStreamIo){.store = &store->segments, .stream = region.stream};
    block_cache_init(&store->cache, segment_stream_source(&store->io), region.length);
    LibraryStatus status =
        library_open(&store->library, block_cache_source(&store->cache), region.length);
    store->available = status == LIBRARY_OK;
    store->library.generation = segment_store_sequence(&store->segments);
    return store->available;
}
