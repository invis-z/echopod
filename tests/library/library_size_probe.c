#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "library/library_builder.h"

enum {
    DEVICE_BUILDER_MEMORY = 384 * 1024 - 9336,
    DEVICE_LIBRARY_FILE = 4 * 1024 * 1024,
    DEVICE_SCRATCH_FILE = 24 * 1024 * 1024,
    KILOBYTE = 1024,
    MEGABYTE = 1024 * 1024,
    MAX_TRACKS = 65535,
    TEXT_CAPACITY = 256,
    MINUTE_MS = 60000
};

typedef struct Buffer {
    uint8_t *bytes;
    uint32_t size;
    uint32_t length;
} Buffer;

typedef struct Style {
    const char *name;
    int artist_length;
    int album_length;
    int title_length;
    bool cjk;
} Style;

typedef struct Limits {
    uint32_t memory;
    uint32_t library;
    uint32_t scratch;
    uint32_t tracks_per_album;
} Limits;

typedef struct Outcome {
    BuilderResult result;
    uint32_t library_bytes;
    uint32_t scratch_bytes;
} Outcome;

static const Style STYLES[] = {
    {"short", 10, 10, 10, false},
    {"typical", 16, 22, 24, false},
    {"long", 30, 70, 70, false},
    {"cjk", 6, 10, 12, true},
};

static bool buffer_read(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    Buffer *buffer = context;
    if (offset > buffer->size || length > buffer->size - offset) {
        return false;
    }
    memcpy(bytes, buffer->bytes + offset, length);
    return true;
}

static bool buffer_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    Buffer *buffer = context;
    if (offset > buffer->size || length > buffer->size - offset) {
        return false;
    }
    memcpy(buffer->bytes + offset, bytes, length);
    buffer->length = offset + length > buffer->length ? offset + length : buffer->length;
    return true;
}

static void name(char *out, const char *stem, uint32_t number, int characters, bool cjk)
{
    char digits[16];
    int digit_count = snprintf(digits, sizeof digits, " %u", number);
    size_t used = 0;
    for (int index = 0; index + digit_count < characters; index++) {
        if (cjk) {
            memcpy(out + used, "\xE6\xBC\xA2", 3);
            used += 3;
        } else {
            out[used++] = stem[(size_t)index % strlen(stem)];
        }
    }
    memcpy(out + used, digits, (size_t)digit_count + 1);
}

static const char *result_text(BuilderResult result)
{
    switch (result) {
    case BUILDER_OK:
        return "ok";
    case BUILDER_OUT_OF_MEMORY:
        return "out of RAM";
    case BUILDER_TOO_MANY:
        return "too many tracks";
    case BUILDER_WRITE_FAILED:
        return "scratch file full";
    case BUILDER_NO_ROOM:
        return "library file full";
    }
    return "?";
}

static Outcome build(const Style *style, uint32_t tracks, Limits limits)
{
    uint8_t *memory = malloc(limits.memory);
    Buffer scratch = {.bytes = malloc(limits.scratch), .size = limits.scratch, .length = 0};
    Buffer output = {.bytes = malloc(limits.library), .size = limits.library, .length = 0};
    if (memory == NULL || scratch.bytes == NULL || output.bytes == NULL) {
        fprintf(stderr, "out of host memory\n");
        exit(1);
    }
    LibraryBuilder builder;
    library_builder_init(&builder, memory, limits.memory,
                         (BuilderScratch){buffer_read, buffer_write, &scratch, limits.scratch});
    BuilderResult result = BUILDER_OK;
    for (uint32_t index = 0; index < tracks && result == BUILDER_OK; index++) {
        uint32_t album = index / limits.tracks_per_album;
        uint32_t number = index % limits.tracks_per_album + 1;
        char artist[TEXT_CAPACITY];
        char album_name[TEXT_CAPACITY];
        char title[TEXT_CAPACITY];
        char path[3 * TEXT_CAPACITY];
        name(artist, "Artist Name ", album / 4, style->artist_length, style->cjk);
        name(album_name, "Album Title ", album, style->album_length, style->cjk);
        name(title, "Song Title Words ", index, style->title_length, style->cjk);
        snprintf(path, sizeof path, "%s/%s/%02u - %s.flac", artist, album_name, number, title);
        BuilderTrack track = {.path = path,
                              .title = title,
                              .artist = artist,
                              .album_artist = artist,
                              .album = album_name,
                              .genre = "Rock",
                              .year = 2000,
                              .disc = 1,
                              .number = (uint8_t)number,
                              .duration_ms = MINUTE_MS};
        result = library_builder_add_track(&builder, &track);
    }
    uint32_t length = 0;
    if (result == BUILDER_OK) {
        result = library_builder_prepare(&builder);
    }
    if (result == BUILDER_OK) {
        result = library_builder_finish(&builder, (ByteSink){buffer_write, &output}, limits.library,
                                        &length);
    }
    free(memory);
    free(scratch.bytes);
    free(output.bytes);
    return (Outcome){result, length, scratch.length};
}

static uint32_t most_tracks(const Style *style, Limits limits, Outcome *first_failure)
{
    uint32_t good = 0;
    uint32_t bad = MAX_TRACKS + 1;
    *first_failure = (Outcome){BUILDER_TOO_MANY, 0, 0};
    while (bad - good > 1) {
        uint32_t middle = good + (bad - good) / 2;
        Outcome outcome = build(style, middle, limits);
        if (outcome.result == BUILDER_OK) {
            good = middle;
        } else {
            bad = middle;
            *first_failure = outcome;
        }
    }
    return good;
}

int main(int argc, char **argv)
{
    Limits limits = {
        .tracks_per_album = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 12,
        .memory = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) * KILOBYTE
                           : (uint32_t)DEVICE_BUILDER_MEMORY,
        .library = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) * MEGABYTE
                            : (uint32_t)DEVICE_LIBRARY_FILE,
        .scratch = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) * MEGABYTE
                            : (uint32_t)DEVICE_SCRATCH_FILE,
    };
    if (argc > 5) {
        Outcome outcome = build(&STYLES[1], (uint32_t)strtoul(argv[5], NULL, 10), limits);
        printf("%s\n", result_text(outcome.result));
        return 0;
    }
    printf("builder RAM %u B, library.db %u MB, build.tmp %u MB, %u tracks per album\n",
           limits.memory, limits.library / MEGABYTE, limits.scratch / MEGABYTE,
           limits.tracks_per_album);
    for (size_t index = 0; index < sizeof STYLES / sizeof STYLES[0]; index++) {
        const Style *style = &STYLES[index];
        Outcome failure;
        uint32_t tracks = most_tracks(style, limits, &failure);
        Outcome at_most = build(style, tracks, limits);
        printf("%-8s up to %5u tracks (library.db %7u B, %5.1f B/track; build.tmp %8u B); "
               "one more: %s\n",
               style->name, tracks, at_most.library_bytes,
               tracks ? (double)at_most.library_bytes / tracks : 0.0, at_most.scratch_bytes,
               result_text(failure.result));
    }
    return 0;
}
