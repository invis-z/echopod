#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    SEGMENT_POOL_LIBRARY,
    SEGMENT_POOL_COVERS,
    SEGMENT_POOL_COUNT
};

enum {
    SEGMENT_LIBRARY_SIZE = 1024 * 1024,
    SEGMENT_COVERS_SIZE = 4 * 1024 * 1024,
    SEGMENT_LIBRARY_CAPACITY = 512,
    SEGMENT_COVERS_CAPACITY = 256,
    SEGMENT_TOTAL_CAPACITY = SEGMENT_LIBRARY_CAPACITY + SEGMENT_COVERS_CAPACITY,
    SEGMENT_STREAM_COUNT = 8,
    SEGMENT_REGION_COUNT = 16,
    SEGMENT_VALUE_COUNT = 8,
    SEGMENT_PATH_CAPACITY = 32,
    SEGMENT_MANIFEST_SLOT_SIZE = 4096,
    SEGMENT_MANIFEST_SIZE = 2 * SEGMENT_MANIFEST_SLOT_SIZE,
    SEGMENT_CHUNK_SIZE = 512,
    SEGMENT_NONE = 0xFFFF
};

#define SEGMENT_MANIFEST_PATH "/.rippl/manifest.bin"

typedef enum SegmentFileResult {
    SEGMENT_FILE_OK,
    SEGMENT_FILE_NO_SPACE,
    SEGMENT_FILE_FAILED
} SegmentFileResult;

typedef struct SegmentFiles {
    SegmentFileResult (*reserve)(void *context, const char *path, uint32_t size);
    bool (*read)(void *context, const char *path, uint32_t offset, void *buffer, uint32_t length);
    bool (*write)(void *context, const char *path, uint32_t offset, const void *bytes,
                  uint32_t length);
    void *context;
} SegmentFiles;

typedef enum SegmentResult {
    SEGMENT_OK,
    SEGMENT_NO_SPACE,
    SEGMENT_FULL,
    SEGMENT_REFUSED,
    SEGMENT_FAILED
} SegmentResult;

typedef struct SegmentStream {
    uint8_t pool;
    uint16_t head;
    uint16_t count;
    uint32_t length;
} SegmentStream;

typedef struct SegmentRegion {
    uint8_t stream;
    uint32_t offset;
    uint32_t length;
} SegmentRegion;

typedef struct SegmentState {
    uint32_t sequence;
    uint16_t reserved[SEGMENT_POOL_COUNT];
    SegmentStream streams[SEGMENT_STREAM_COUNT];
    SegmentRegion regions[SEGMENT_REGION_COUNT];
    uint32_t values[SEGMENT_VALUE_COUNT];
} SegmentState;

typedef struct SegmentStore {
    SegmentFiles files;
    SegmentState committed;
    SegmentState working;
    bool shared[SEGMENT_STREAM_COUNT];
    uint8_t slot;
    bool found;
    uint16_t next[SEGMENT_TOTAL_CAPACITY];
    uint8_t used[SEGMENT_TOTAL_CAPACITY / 8];
    uint8_t chunk[SEGMENT_CHUNK_SIZE];
} SegmentStore;

void segment_store_open(SegmentStore *store, SegmentFiles files);
bool segment_store_found(const SegmentStore *store);
uint32_t segment_store_sequence(const SegmentStore *store);
uint32_t segment_store_length(const SegmentStore *store, uint8_t stream);
bool segment_store_read(const SegmentStore *store, uint8_t stream, uint32_t offset, void *buffer,
                        uint32_t length);
SegmentRegion segment_store_region(const SegmentStore *store, uint8_t region);
uint32_t segment_store_value(const SegmentStore *store, uint8_t index);

void segment_store_begin(SegmentStore *store);
void segment_store_start(SegmentStore *store, uint8_t stream, uint8_t pool);
void segment_store_drop(SegmentStore *store, uint8_t stream);
uint32_t segment_store_working_length(const SegmentStore *store, uint8_t stream);
SegmentResult segment_store_append(SegmentStore *store, uint8_t stream, const void *bytes,
                                   uint32_t length);
SegmentResult segment_store_write(SegmentStore *store, uint8_t stream, uint32_t offset,
                                  const void *bytes, uint32_t length);
bool segment_store_read_working(const SegmentStore *store, uint8_t stream, uint32_t offset,
                                void *buffer, uint32_t length);
void segment_store_set_region(SegmentStore *store, uint8_t region, SegmentRegion value);
void segment_store_set_value(SegmentStore *store, uint8_t index, uint32_t value);
SegmentResult segment_store_grow(SegmentStore *store, uint8_t stream, uint16_t *number);
void segment_store_path(uint8_t pool, uint16_t number, char *path);
SegmentResult segment_store_commit(SegmentStore *store);
void segment_store_cancel(SegmentStore *store);
