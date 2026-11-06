#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library/segment_store.h"

enum {
    FAKE_CARD_FILES = 800,
    FAKE_CARD_PAGE = 64 * 1024,
    FAKE_CARD_NEVER = -1
};

typedef struct FakeCardFile {
    char path[SEGMENT_PATH_CAPACITY];
    uint8_t **pages;
    uint32_t size;
} FakeCardFile;

typedef struct FakeCard {
    FakeCardFile files[FAKE_CARD_FILES];
    int count;
    uint64_t free_bytes;
    int operations;
    int cut_at;
    uint32_t torn_bytes;
    bool dead;
    int writes;
    int reserves;
} FakeCard;

void fake_card_init(FakeCard *card, uint64_t free_bytes);
void fake_card_free(FakeCard *card);
void fake_card_copy(FakeCard *to, const FakeCard *from);
void fake_card_cut(FakeCard *card, int operation, uint32_t torn_bytes);
void fake_card_restore_power(FakeCard *card);
FakeCardFile *fake_card_find(FakeCard *card, const char *path);
bool fake_card_read(FakeCard *card, const char *path, uint32_t offset, void *buffer,
                    uint32_t length);
SegmentFiles fake_card_files(FakeCard *card);
