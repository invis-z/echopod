#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "fake_card.h"
#include "hal/storage.h"
#include "library/library.h"
#include "library/library_rebuild.h"
#include "library/library_scan.h"
#include "library/music_folders.h"
#include "library_compare.h"
#include "sim_storage.h"

enum {
    MEMORY_SIZE = 384 * 1024,
    OUTPUT_CAPACITY = 1024 * 1024,
    SCRATCH_SIZE = 16 * 1024 * 1024
};

typedef struct HostFiles {
    HalDir dir;
    HalFile file;
    HalDirEntry entry;
    const char *folder;
    uint32_t opens;
} HostFiles;

static const MusicFolders *host_skip;

typedef struct Output {
    uint8_t bytes[OUTPUT_CAPACITY];
    uint32_t length;
} Output;

static bool open_dir(void *context, const char *path)
{
    HostFiles *files = context;
    files->folder = path;
    return hal_dir_open(&files->dir, path);
}

static bool next_entry(void *context, ScanEntry *entry)
{
    HostFiles *files = context;
    do {
        if (!hal_dir_next(&files->dir, &files->entry)) {
            return false;
        }
    } while (files->entry.directory &&
             music_folders_skips(host_skip, files->folder, files->entry.name));
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
    files->opens++;
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

static bool write_output(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Output *output = context;
    if (offset + length > OUTPUT_CAPACITY) {
        return false;
    }
    memcpy(output->bytes + offset, bytes, length);
    output->length = offset + length > output->length ? offset + length : output->length;
    return true;
}

static uint8_t *scratch_bytes(void)
{
    static uint8_t *bytes;
    if (bytes == NULL) {
        bytes = calloc(SCRATCH_SIZE, 1);
    }
    return bytes;
}

static bool scratch_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    if (offset > SCRATCH_SIZE || length > SCRATCH_SIZE - offset) {
        return false;
    }
    memcpy(buffer, scratch_bytes() + offset, length);
    return true;
}

static bool scratch_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    if (offset > SCRATCH_SIZE || length > SCRATCH_SIZE - offset) {
        return false;
    }
    memcpy(scratch_bytes() + offset, bytes, length);
    return true;
}

static const BuilderScratch SCRATCH = {scratch_read, scratch_write, NULL, SCRATCH_SIZE};

typedef struct Progress {
    uint32_t calls;
    uint32_t last_files;
    uint32_t last_tracks;
} Progress;

static void report(void *context, uint32_t files, uint32_t tracks)
{
    Progress *progress = context;
    progress->calls++;
    progress->last_files = files;
    progress->last_tracks = tracks;
}

static uint8_t *read_fixture(const char *name, uint32_t *size)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", RIPPL_FIXTURE_DIR, name);
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    fseek(file, 0, SEEK_END);
    *size = (uint32_t)ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *bytes = malloc(*size);
    CHECK(fread(bytes, 1, *size, file) == *size);
    fclose(file);
    return bytes;
}

static ScanResult scan_folder(const char *folder, Output *output, Progress *progress,
                              ScanStats *stats)
{
    static uint8_t memory[MEMORY_SIZE];
    static HostFiles files;
    sim_storage_configure(folder, NULL);
    output->length = 0;
    ScanJob job = {
        .files = {open_dir, next_entry, close_dir, open_file, close_file, &files},
        .memory = memory,
        .size = sizeof memory,
        .scratch = SCRATCH,
        .sink = {write_output, output},
        .progress = {report, progress},
    };
    return library_scan(&job, stats);
}

static void scanning_the_tag_fixtures_matches_the_pc_indexer(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_folder(RIPPL_FIXTURE_DIR "/tags", &output, &progress, &stats));
    CHECK_EQUAL(13u, stats.tracks);
    CHECK_EQUAL(13u, progress.last_tracks);
    CHECK_EQUAL(0u, progress.last_files);
    CHECK(progress.calls >= 14);
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture("tags_library.db", &expected_size);
    CHECK_EQUAL(stats.length, output.length);
    CHECK(library_compare(expected, expected_size, output.bytes, output.length));
    free(expected);
}

static void scanning_folders_and_playlists_matches_the_pc_indexer(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_folder(RIPPL_FIXTURE_DIR "/scan_music", &output, &progress, &stats));
    CHECK_EQUAL(8u, stats.tracks);
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture("scan_music.db", &expected_size);
    CHECK(library_compare(expected, expected_size, output.bytes, stats.length));
    free(expected);
}

static void internal_storage_scans_as_one_more_root_folder_like_the_pc_indexer(void)
{
    static uint8_t memory[MEMORY_SIZE];
    static HostFiles files;
    static Output output;
    sim_storage_configure(RIPPL_FIXTURE_DIR "/scan_music", NULL);
    sim_storage_configure_internal(RIPPL_FIXTURE_DIR "/tags");
    output.length = 0;
    ScanJob job = {
        .files = {open_dir, next_entry, close_dir, open_file, close_file, &files},
        .memory = memory,
        .size = sizeof memory,
        .scratch = SCRATCH,
        .sink = {write_output, &output},
    };
    ScanStats stats;
    ScanResult result = library_scan(&job, &stats);
    CHECK_EQUAL(SCAN_OK, result);
    CHECK_EQUAL(21u, stats.tracks);
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture("two_drives.db", &expected_size);
    CHECK(library_compare(expected, expected_size, output.bytes, stats.length));
    free(expected);
}

static MusicFolders fixture_folders(void)
{
    uint32_t size = 0;
    uint8_t *bytes = read_fixture("scan_music_folders.bin", &size);
    MusicFolders folders = {.count = 0};
    CHECK(music_folders_decode(bytes, size, &folders));
    free(bytes);
    return folders;
}

static void left_out_folders_match_the_pc_indexer(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    MusicFolders folders = fixture_folders();
    CHECK_EQUAL(2u, folders.count);
    host_skip = &folders;
    ScanResult result = scan_folder(RIPPL_FIXTURE_DIR "/scan_music", &output, &progress, &stats);
    host_skip = NULL;
    CHECK_EQUAL(SCAN_OK, result);
    CHECK_EQUAL(3u, stats.tracks);
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture("scan_music_folders.db", &expected_size);
    CHECK(library_compare(expected, expected_size, output.bytes, stats.length));
    free(expected);
}

enum {
    CACHE_CAPACITY = 256 * 1024,
    CACHE_HEADER = 20
};

typedef struct CacheBytes {
    uint8_t bytes[CACHE_CAPACITY];
    uint32_t length;
} CacheBytes;

static bool read_cache(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const CacheBytes *cache = context;
    if (offset > cache->length || length > cache->length - offset) {
        return false;
    }
    memcpy(buffer, cache->bytes + offset, length);
    return true;
}

static bool write_cache(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    CacheBytes *cache = context;
    if (offset > CACHE_CAPACITY || length > CACHE_CAPACITY - offset) {
        return false;
    }
    memcpy(cache->bytes + offset, bytes, length);
    cache->length = offset + length > cache->length ? offset + length : cache->length;
    return true;
}

static uint32_t record_length(const CacheBytes *cache, uint32_t offset)
{
    return (uint32_t)cache->bytes[offset] | (uint32_t)cache->bytes[offset + 1] << 8;
}

static uint32_t find_record(const CacheBytes *cache, const char *path)
{
    for (uint32_t offset = 0; offset < cache->length; offset += record_length(cache, offset)) {
        if (strcmp((const char *)cache->bytes + offset + CACHE_HEADER, path) == 0) {
            return offset;
        }
    }
    return UINT32_MAX;
}

static uint32_t record_count(const CacheBytes *cache)
{
    uint32_t count = 0;
    for (uint32_t offset = 0; offset < cache->length; offset += record_length(cache, offset)) {
        count++;
    }
    return count;
}

static HostFiles cached_files;

static ScanResult scan_cached(const char *folder, CacheBytes *previous, CacheBytes *next,
                              Output *output, ScanStats *stats)
{
    static uint8_t memory[MEMORY_SIZE];
    sim_storage_configure(folder, NULL);
    output->length = 0;
    next->length = 0;
    cached_files.opens = 0;
    ScanCache cache = {.previous = {read_cache, previous},
                       .previous_length = previous->length,
                       .next = {write_cache, next}};
    ScanJob job = {
        .files = {open_dir, next_entry, close_dir, open_file, close_file, &cached_files},
        .memory = memory,
        .size = sizeof memory,
        .scratch = SCRATCH,
        .sink = {write_output, output},
        .cache = &cache,
    };
    ScanResult result = library_scan(&job, stats);
    CHECK_EQUAL(next->length, cache.next_length);
    return result;
}

static void check_output(const Output *output, const ScanStats *stats, const char *fixture)
{
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture(fixture, &expected_size);
    CHECK(library_compare(expected, expected_size, output->bytes, stats->length));
    free(expected);
}

static CacheBytes first_cache;
static CacheBytes edited_cache;
static CacheBytes second_cache;
static Output cached_output;

static void first_scan(const char *folder, const char *fixture, ScanStats *stats)
{
    static CacheBytes none = {.length = 0};
    CHECK_EQUAL(SCAN_OK, scan_cached(folder, &none, &first_cache, &cached_output, stats));
    CHECK_EQUAL(0, stats->reused);
    CHECK_EQUAL(stats->files, record_count(&first_cache));
    check_output(&cached_output, stats, fixture);
}

static void a_second_scan_reads_no_unchanged_file(void)
{
    ScanStats stats;
    first_scan(RIPPL_FIXTURE_DIR "/tags", "tags_library.db", &stats);
    uint32_t opened = cached_files.opens;
    CHECK(opened >= stats.files);
    CHECK_EQUAL(SCAN_OK, scan_cached(RIPPL_FIXTURE_DIR "/tags", &first_cache, &second_cache,
                                     &cached_output, &stats));
    CHECK_EQUAL(13u, stats.tracks);
    CHECK_EQUAL(stats.files, stats.reused);
    CHECK_EQUAL(0, cached_files.opens);
    CHECK_EQUAL(first_cache.length, second_cache.length);
    CHECK(memcmp(first_cache.bytes, second_cache.bytes, first_cache.length) == 0);
    check_output(&cached_output, &stats, "tags_library.db");
}

static void a_changed_file_is_read_again(void)
{
    ScanStats stats;
    first_scan(RIPPL_FIXTURE_DIR "/scan_music", "scan_music.db", &stats);
    edited_cache = first_cache;
    uint32_t record = find_record(&edited_cache, "Artist A/Album One/02 - Second.ogg");
    CHECK(record != UINT32_MAX);
    edited_cache.bytes[record + 4] ^= 1;
    CHECK_EQUAL(SCAN_OK, scan_cached(RIPPL_FIXTURE_DIR "/scan_music", &edited_cache, &second_cache,
                                     &cached_output, &stats));
    CHECK_EQUAL(stats.files - 1, stats.reused);
    CHECK_EQUAL(first_cache.length, second_cache.length);
    CHECK(memcmp(first_cache.bytes, second_cache.bytes, first_cache.length) == 0);
    check_output(&cached_output, &stats, "scan_music.db");
}

static void removed_and_added_files_are_handled(void)
{
    ScanStats stats;
    first_scan(RIPPL_FIXTURE_DIR "/scan_music", "scan_music.db", &stats);
    uint32_t files = stats.files;
    uint32_t second = record_length(&first_cache, 0);
    uint32_t dropped = record_length(&first_cache, second);
    uint8_t gone[64] = {0};
    const char *gone_path = "!gone.mp3";
    uint32_t gone_length = CACHE_HEADER + (uint32_t)strlen(gone_path) + 1;
    gone[0] = (uint8_t)gone_length;
    gone[2] = 2;
    memcpy(gone + CACHE_HEADER, gone_path, strlen(gone_path) + 1);
    memcpy(edited_cache.bytes, gone, gone_length);
    memcpy(edited_cache.bytes + gone_length, first_cache.bytes, second);
    memcpy(edited_cache.bytes + gone_length + second, first_cache.bytes + second + dropped,
           first_cache.length - second - dropped);
    edited_cache.length = gone_length + first_cache.length - dropped;
    CHECK_EQUAL(SCAN_OK, scan_cached(RIPPL_FIXTURE_DIR "/scan_music", &edited_cache, &second_cache,
                                     &cached_output, &stats));
    CHECK_EQUAL(files, stats.files);
    CHECK_EQUAL(files - 1, stats.reused);
    CHECK_EQUAL(first_cache.length, second_cache.length);
    CHECK(memcmp(first_cache.bytes, second_cache.bytes, first_cache.length) == 0);
    check_output(&cached_output, &stats, "scan_music.db");
}

static void a_damaged_cache_is_ignored(void)
{
    ScanStats stats;
    first_scan(RIPPL_FIXTURE_DIR "/tags", "tags_library.db", &stats);
    edited_cache = first_cache;
    edited_cache.bytes[0] = 3;
    edited_cache.bytes[1] = 0;
    CHECK_EQUAL(SCAN_OK, scan_cached(RIPPL_FIXTURE_DIR "/tags", &edited_cache, &second_cache,
                                     &cached_output, &stats));
    CHECK_EQUAL(0, stats.reused);
    CHECK_EQUAL(first_cache.length, second_cache.length);
    check_output(&cached_output, &stats, "tags_library.db");
}

static void a_separate_listing_leaves_the_memory_to_the_builder(void)
{
    static uint8_t memory[128 * 1024];
    static _Alignas(8) uint8_t listing[16 * 1024];
    static HostFiles files;
    static Output output;
    sim_storage_configure(RIPPL_FIXTURE_DIR "/scan_music", NULL);
    output.length = 0;
    ScanJob job = {
        .files = {open_dir, next_entry, close_dir, open_file, close_file, &files},
        .memory = memory,
        .size = sizeof memory,
        .listing = listing,
        .listing_size = sizeof listing,
        .scratch = SCRATCH,
        .sink = {write_output, &output},
    };
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, library_scan(&job, &stats));
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture("scan_music.db", &expected_size);
    CHECK(library_compare(expected, expected_size, output.bytes, stats.length));
    free(expected);
}

static void a_missing_music_root_says_so(void)
{
    static Output output;
    Progress progress = {.calls = 0};
    ScanStats stats;
    CHECK_EQUAL(SCAN_NO_MUSIC_ROOT,
                scan_folder(RIPPL_FIXTURE_DIR "/no such folder", &output, &progress, &stats));
}

enum {
    TREE_CAPACITY = 4096,
    TREE_PATH = 96,
    FRAME_BYTES = 417,
    FRAMES = 3
};

typedef struct TreeEntry {
    char name[TREE_PATH];
    bool directory;
} TreeEntry;

typedef struct Tree {
    char paths[TREE_CAPACITY][TREE_PATH];
    uint32_t count;
    TreeEntry listing[TREE_CAPACITY];
    uint32_t listed;
    uint32_t cursor;
    uint32_t opens;
    const char *refused;
    uint8_t blob[FRAMES * FRAME_BYTES];
} Tree;

static Tree tree;

static void tree_add(const char *path)
{
    snprintf(tree.paths[tree.count++], TREE_PATH, "%s", path);
}

static bool tree_open_dir(void *context, const char *path)
{
    (void)context;
    size_t prefix = strlen(path);
    tree.listed = 0;
    tree.cursor = 0;
    tree.opens++;
    if (tree.refused != NULL && strcmp(path, tree.refused) == 0) {
        return false;
    }
    for (uint32_t index = 0; index < tree.count; index++) {
        const char *file = tree.paths[index];
        if (strncmp(file, path, prefix) != 0) {
            continue;
        }
        const char *rest = file + prefix;
        const char *slash = strchr(rest, '/');
        size_t length = slash == NULL ? strlen(rest) : (size_t)(slash - rest);
        bool seen = false;
        for (uint32_t other = 0; other < tree.listed && !seen; other++) {
            seen = strlen(tree.listing[other].name) == length &&
                   strncmp(tree.listing[other].name, rest, length) == 0;
        }
        if (!seen) {
            TreeEntry *entry = &tree.listing[tree.listed++];
            snprintf(entry->name, TREE_PATH, "%.*s", (int)length, rest);
            entry->directory = slash != NULL;
        }
    }
    return tree.listed > 0;
}

static bool tree_next_entry(void *context, ScanEntry *entry)
{
    (void)context;
    if (tree.cursor >= tree.listed) {
        return false;
    }
    const TreeEntry *next = &tree.listing[tree.listed - 1 - tree.cursor++];
    entry->directory = next->directory;
    snprintf(entry->name, sizeof entry->name, "%s", next->name);
    return true;
}

static void tree_close_dir(void *context)
{
    (void)context;
}

static bool tree_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    if (offset > sizeof tree.blob || length > sizeof tree.blob - offset) {
        return false;
    }
    memcpy(buffer, tree.blob + offset, length);
    return true;
}

static bool tree_open_file(void *context, const char *path, ByteSource *source, uint32_t *size)
{
    (void)context;
    (void)path;
    *source = (ByteSource){.read = tree_read, .context = NULL};
    *size = sizeof tree.blob;
    return true;
}

static void tree_close_file(void *context)
{
    (void)context;
}

static void start_tree(void)
{
    tree.count = 0;
    tree.opens = 0;
    tree.refused = NULL;
    memset(tree.blob, 0, sizeof tree.blob);
    for (uint32_t frame = 0; frame < FRAMES; frame++) {
        uint8_t *header = tree.blob + frame * FRAME_BYTES;
        header[0] = 0xFF;
        header[1] = 0xFB;
        header[2] = 0x90;
        header[3] = 0xC0;
    }
}

static ScanResult scan_tree_at(uint32_t listing_skip, uint32_t listing_size, Output *output,
                               ScanStats *stats)
{
    static uint8_t memory[MEMORY_SIZE];
    static _Alignas(8) uint8_t listing[96 * 1024 + 8];
    output->length = 0;
    ScanJob job = {
        .files = {tree_open_dir, tree_next_entry, tree_close_dir, tree_open_file, tree_close_file,
                  NULL},
        .memory = memory,
        .size = sizeof memory,
        .listing = listing + listing_skip,
        .listing_size = listing_size,
        .scratch = SCRATCH,
        .sink = {write_output, output},
    };
    return library_scan(&job, stats);
}

static ScanResult scan_tree(uint32_t listing_size, Output *output, ScanStats *stats)
{
    return scan_tree_at(0, listing_size, output, stats);
}

static bool output_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const Output *output = context;
    if (offset > output->length || length > output->length - offset) {
        return false;
    }
    memcpy(buffer, output->bytes + offset, length);
    return true;
}

static bool library_holds_every_path(Output *output)
{
    Library library;
    if (library_open(&library, (ByteSource){output_read, output}, output->length) != LIBRARY_OK ||
        library_count(&library, LIBRARY_TRACKS) != tree.count) {
        return false;
    }
    static bool found[TREE_CAPACITY];
    memset(found, 0, sizeof found);
    for (uint32_t index = 0; index < tree.count; index++) {
        LibraryTrack track;
        char path[TREE_PATH + 1];
        path[0] = '/';
        if (!library_read_track(&library, index, &track) ||
            !library_read_string(&library, track.path, path + 1, sizeof path - 1)) {
            return false;
        }
        for (uint32_t file = 0; file < tree.count; file++) {
            if (!found[file] && strcmp(tree.paths[file], path) == 0) {
                found[file] = true;
                break;
            }
        }
    }
    for (uint32_t file = 0; file < tree.count; file++) {
        if (!found[file]) {
            return false;
        }
    }
    return true;
}

static void a_folder_bigger_than_the_listing_is_read_in_batches(void)
{
    static Output output;
    start_tree();
    char path[TREE_PATH];
    for (uint32_t index = 0; index < 3000; index++) {
        snprintf(path, sizeof path, "/Big/%04u - %s %04u.mp3", index, index % 3 ? "song" : "Song",
                 index * 7919 % 3000);
        tree_add(path);
    }
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_tree(16 * 1024, &output, &stats));
    CHECK_EQUAL(3000u, stats.tracks);
    CHECK(library_holds_every_path(&output));
    CHECK(tree.opens > 2);
}

static void many_sibling_folders_fit_a_small_listing(void)
{
    static Output output;
    start_tree();
    char path[TREE_PATH];
    for (uint32_t index = 0; index < 700; index++) {
        snprintf(path, sizeof path, "/Music/Artist %03u/Album/01 - One.mp3", index);
        tree_add(path);
        snprintf(path, sizeof path, "/Music/Artist %03u/Album/02 - Two.mp3", index);
        tree_add(path);
    }
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_tree(16 * 1024, &output, &stats));
    CHECK_EQUAL(1400u, stats.tracks);
    CHECK(library_holds_every_path(&output));
}

static void a_big_folder_nested_in_big_folders_is_read_in_batches(void)
{
    static Output output;
    start_tree();
    char path[TREE_PATH];
    for (uint32_t outer = 0; outer < 700; outer++) {
        for (uint32_t inner = 0; inner < 5; inner++) {
            snprintf(path, sizeof path, "/Outer folder %03u/Inner %u.mp3", outer, inner);
            tree_add(path);
        }
    }
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_tree(16 * 1024, &output, &stats));
    CHECK_EQUAL(3500u, stats.tracks);
    CHECK(library_holds_every_path(&output));
}

static void a_folder_that_cannot_be_opened_leaves_the_next_ones_scanned(void)
{
    static Output output;
    start_tree();
    tree_add("/A Locked/01 - One.mp3");
    tree_add("/B Open/01 - Two.mp3");
    tree_add("/C Open/Inner/01 - Three.mp3");
    tree.refused = "/A Locked/";
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_tree(96 * 1024, &output, &stats));
    CHECK_EQUAL(2u, stats.tracks);
}

static void batches_build_the_same_library_as_one_whole_read(void)
{
    static Output whole;
    static Output batched;
    start_tree();
    char path[TREE_PATH];
    for (uint32_t outer = 0; outer < 700; outer++) {
        for (uint32_t inner = 0; inner < 5; inner++) {
            snprintf(path, sizeof path, "/Outer folder %03u/Inner %u.mp3", (outer * 37) % 700,
                     inner);
            tree_add(path);
        }
    }
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_tree(96 * 1024, &whole, &stats));
    CHECK_EQUAL(SCAN_OK, scan_tree(16 * 1024, &batched, &stats));
    CHECK(library_compare(whole.bytes, whole.length, batched.bytes, batched.length));
}

static void a_listing_at_an_odd_address_builds_the_same_library(void)
{
    static Output aligned;
    static Output odd;
    start_tree();
    char path[TREE_PATH];
    for (uint32_t outer = 0; outer < 700; outer++) {
        for (uint32_t inner = 0; inner < 5; inner++) {
            snprintf(path, sizeof path, "/Outer folder %03u/Inner %u.mp3", outer, inner);
            tree_add(path);
        }
    }
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, scan_tree(16 * 1024, &aligned, &stats));
    CHECK_EQUAL(SCAN_OK, scan_tree_at(1, 16 * 1024 + 7, &odd, &stats));
    CHECK(library_compare(aligned.bytes, aligned.length, odd.bytes, odd.length));
}

static FakeCard rebuild_card;
static SegmentStore rebuild_store;

static ScanResult rebuild(const char *folder, ScanStats *stats)
{
    static uint8_t memory[MEMORY_SIZE];
    sim_storage_configure(folder, NULL);
    cached_files.opens = 0;
    segment_store_open(&rebuild_store, fake_card_files(&rebuild_card));
    RebuildJob job = {
        .scan =
            {
                .files = {open_dir, next_entry, close_dir, open_file, close_file, &cached_files},
                .memory = memory,
                .size = sizeof memory,
            },
        .store = &rebuild_store,
    };
    return library_rebuild(&job, stats);
}

static void check_committed(const char *fixture)
{
    static SegmentStore reopened;
    segment_store_open(&reopened, fake_card_files(&rebuild_card));
    SegmentRegion region = segment_store_region(&reopened, LIBRARY_REGION_INDEX);
    CHECK_EQUAL(LIBRARY_STREAM_INDEX, region.stream);
    CHECK(region.length <= OUTPUT_CAPACITY);
    CHECK(segment_store_read(&reopened, region.stream, 0, cached_output.bytes, region.length));
    uint32_t expected_size = 0;
    uint8_t *expected = read_fixture(fixture, &expected_size);
    CHECK(library_compare(expected, expected_size, cached_output.bytes, region.length));
    free(expected);
}

static void a_rebuild_commits_the_library_and_reuses_it_next_time(void)
{
    fake_card_init(&rebuild_card, UINT64_MAX);
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, rebuild(RIPPL_FIXTURE_DIR "/tags", &stats));
    CHECK_EQUAL(13u, stats.tracks);
    CHECK_EQUAL(0, stats.reused);
    uint32_t first = segment_store_sequence(&rebuild_store);
    check_committed("tags_library.db");
    CHECK_EQUAL(SCAN_OK, rebuild(RIPPL_FIXTURE_DIR "/tags", &stats));
    CHECK_EQUAL(13u, stats.tracks);
    CHECK_EQUAL(stats.files, stats.reused);
    CHECK_EQUAL(0, cached_files.opens);
    CHECK_EQUAL(first + 1, segment_store_sequence(&rebuild_store));
    check_committed("tags_library.db");
    CHECK_EQUAL(SCAN_OK, rebuild(RIPPL_FIXTURE_DIR "/scan_music", &stats));
    CHECK_EQUAL(8u, stats.tracks);
    check_committed("scan_music.db");
    CHECK_EQUAL(0, segment_store_length(&rebuild_store, LIBRARY_STREAM_SCRATCH));
    fake_card_free(&rebuild_card);
}

static void a_rebuild_without_room_keeps_the_old_library(void)
{
    fake_card_init(&rebuild_card, UINT64_MAX);
    ScanStats stats;
    CHECK_EQUAL(SCAN_OK, rebuild(RIPPL_FIXTURE_DIR "/tags", &stats));
    uint32_t first = segment_store_sequence(&rebuild_store);
    rebuild_card.free_bytes = 0;
    CHECK_EQUAL(SCAN_NO_ROOM, rebuild(RIPPL_FIXTURE_DIR "/scan_music", &stats));
    static SegmentStore reopened;
    segment_store_open(&reopened, fake_card_files(&rebuild_card));
    CHECK_EQUAL(first, segment_store_sequence(&reopened));
    check_committed("tags_library.db");
    fake_card_free(&rebuild_card);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(scanning_the_tag_fixtures_matches_the_pc_indexer),
        TEST_CASE(scanning_folders_and_playlists_matches_the_pc_indexer),
        TEST_CASE(left_out_folders_match_the_pc_indexer),
        TEST_CASE(a_folder_that_cannot_be_opened_leaves_the_next_ones_scanned),
        TEST_CASE(a_missing_music_root_says_so),
        TEST_CASE(internal_storage_scans_as_one_more_root_folder_like_the_pc_indexer),
        TEST_CASE(a_separate_listing_leaves_the_memory_to_the_builder),
        TEST_CASE(a_second_scan_reads_no_unchanged_file),
        TEST_CASE(a_changed_file_is_read_again),
        TEST_CASE(removed_and_added_files_are_handled),
        TEST_CASE(a_rebuild_commits_the_library_and_reuses_it_next_time),
        TEST_CASE(a_rebuild_without_room_keeps_the_old_library),
        TEST_CASE(a_damaged_cache_is_ignored),
        TEST_CASE(a_folder_bigger_than_the_listing_is_read_in_batches),
        TEST_CASE(many_sibling_folders_fit_a_small_listing),
        TEST_CASE(a_big_folder_nested_in_big_folders_is_read_in_batches),
        TEST_CASE(batches_build_the_same_library_as_one_whole_read),
        TEST_CASE(a_listing_at_an_odd_address_builds_the_same_library),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
