#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hal/storage.h"
#include "library/covers.h"
#include "library/library_rebuild.h"
#include "library/segment_stream.h"
#include "sim_storage.h"

enum {
    MEMORY_SIZE = 384 * 1024,
    PATH_CAPACITY = 1024,
    TEXT_CAPACITY = 1024
};

typedef struct HostFiles {
    HalDir dir;
    HalFile file;
    HalDirEntry entry;
} HostFiles;

static bool open_dir(void *context, const char *path)
{
    HostFiles *files = context;
    return hal_dir_open(&files->dir, path);
}

static bool next_entry(void *context, ScanEntry *entry)
{
    HostFiles *files = context;
    if (!hal_dir_next(&files->dir, &files->entry)) {
        return false;
    }
    entry->directory = files->entry.directory;
    entry->size = files->entry.size;
    entry->modified = files->entry.modified;
    memcpy(entry->name, files->entry.name, sizeof entry->name);
    return true;
}

static void close_dir(void *context)
{
    HostFiles *files = context;
    hal_dir_close(&files->dir);
}

static bool read_host(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    HostFiles *files = context;
    return hal_file_read(files->file, offset, buffer, length);
}

static bool open_file(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    HostFiles *files = context;
    if (!hal_file_open(&files->file, path)) {
        return false;
    }
    *source = (ByteSource){.read = read_host, .context = files};
    *size = files->file.size;
    return true;
}

static void close_file(void *context)
{
    HostFiles *files = context;
    hal_file_close(&files->file);
}

static const char *output_folder;

static void card_path(const char *path, char *full)
{
    snprintf(full, PATH_CAPACITY, "%s%s", output_folder, path);
}

static SegmentFileResult reserve(void *context, const char *path, uint32_t size)
{
    (void)context;
    char full[PATH_CAPACITY];
    card_path(path, full);
    struct stat existing;
    if (stat(full, &existing) == 0) {
        return SEGMENT_FILE_OK;
    }
    for (char *slash = strchr(full + strlen(output_folder) + 1, '/'); slash != NULL;
         slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        (void)mkdir(full, 0777);
        *slash = '/';
    }
    FILE *file = fopen(full, "wb");
    if (file == NULL) {
        return SEGMENT_FILE_FAILED;
    }
    bool sized = ftruncate(fileno(file), (off_t)size) == 0;
    return fclose(file) == 0 && sized ? SEGMENT_FILE_OK : SEGMENT_FILE_FAILED;
}

static bool read_card(void *context, const char *path, uint32_t offset, void *buffer,
                      uint32_t length)
{
    (void)context;
    char full[PATH_CAPACITY];
    card_path(path, full);
    FILE *file = fopen(full, "rb");
    bool read = file != NULL && fseek(file, (long)offset, SEEK_SET) == 0 &&
                fread(buffer, 1, length, file) == length;
    if (file != NULL) {
        fclose(file);
    }
    return read;
}

static bool write_card(void *context, const char *path, uint32_t offset, const void *bytes,
                       uint32_t length)
{
    (void)context;
    char full[PATH_CAPACITY];
    card_path(path, full);
    FILE *file = fopen(full, "r+b");
    bool written = file != NULL && fseek(file, (long)offset, SEEK_SET) == 0 &&
                   fwrite(bytes, 1, length, file) == length;
    return file != NULL && fclose(file) == 0 && written;
}

static bool dump_covers(SegmentStore *store)
{
    static uint16_t pixels[COVER_FULL_PIXELS];
    static char title[TEXT_CAPACITY];
    char path[PATH_CAPACITY];
    snprintf(path, sizeof path, "%s/covers.dump", output_folder);
    FILE *dump = fopen(path, "wb");
    SegmentRegion region = segment_store_region(store, LIBRARY_REGION_INDEX);
    SegmentStreamIo io = {.store = store, .stream = region.stream};
    Library library;
    Covers covers;
    bool ok = dump != NULL &&
              library_open(&library, segment_stream_source(&io), region.length) == LIBRARY_OK;
    bool any = ok && covers_open(&covers, store);
    for (uint32_t album = 0; ok && album < library_count(&library, LIBRARY_ALBUMS); album++) {
        LibraryAlbum record;
        uint32_t rank = 0;
        ok = library_read_album(&library, album, &record) &&
             library_read_string(&library, record.title, title, sizeof title) &&
             library_album_rank(&library, album, &rank);
        uint8_t present = any && ok && covers_read(&covers, rank, COVER_FULL, pixels);
        uint8_t length[2] = {(uint8_t)strlen(title), (uint8_t)(strlen(title) >> 8)};
        ok = ok && fwrite(length, 1, 2, dump) == 2 &&
             fwrite(title, 1, strlen(title), dump) == strlen(title) &&
             fwrite(&present, 1, 1, dump) == 1;
        if (ok && present) {
            uint8_t *bytes = (uint8_t *)pixels;
            for (uint32_t index = 0; index < COVER_FULL_PIXELS; index++) {
                uint16_t value = pixels[index];
                bytes[index * 2] = (uint8_t)value;
                bytes[index * 2 + 1] = (uint8_t)(value >> 8);
            }
            ok = fwrite(pixels, 1, COVER_FULL_BYTES, dump) == COVER_FULL_BYTES &&
                 covers_read(&covers, rank, COVER_HALF, pixels) &&
                 fwrite(pixels, 1, COVER_HALF_BYTES, dump) == COVER_HALF_BYTES;
        }
    }
    return dump != NULL && fclose(dump) == 0 && ok;
}

int main(int argc, char **argv)
{
    static uint8_t memory[MEMORY_SIZE];
    static HostFiles files;
    static SegmentStore store;
    if (argc != 3) {
        fprintf(stderr, "usage: cover_build_tool MUSIC OUTPUT_FOLDER\n");
        return 1;
    }
    output_folder = argv[2];
    sim_storage_configure(argv[1], NULL);
    segment_store_open(&store, (SegmentFiles){reserve, read_card, write_card, NULL});
    RebuildJob job = {
        .scan = {.files = {open_dir, next_entry, close_dir, open_file, close_file, &files},
                 .memory = memory,
                 .size = sizeof memory},
        .store = &store,
    };
    RebuildStats stats;
    ScanResult result = library_rebuild(&job, &stats);
    if (result != SCAN_OK || stats.cover_result != SCAN_OK) {
        fprintf(stderr, "rebuild failed: %d, covers %d\n", result, stats.cover_result);
        return 2;
    }
    printf("%u tracks, %u albums, %u covers, %u reused, %u missing\n", stats.scan.tracks,
           stats.covers.albums, stats.covers.covers, stats.covers.reused, stats.covers.missing);
    return dump_covers(&store) ? 0 : 4;
}
