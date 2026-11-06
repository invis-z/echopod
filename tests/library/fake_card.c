#include "fake_card.h"

#include <stdlib.h>
#include <string.h>

enum {
    CLEARED_BYTES = 4096,
    GARBAGE = 0xA5
};

static uint32_t page_count(uint32_t size)
{
    return (size + FAKE_CARD_PAGE - 1) / FAKE_CARD_PAGE;
}

void fake_card_init(FakeCard *card, uint64_t free_bytes)
{
    memset(card, 0, sizeof *card);
    card->free_bytes = free_bytes;
    card->cut_at = FAKE_CARD_NEVER;
}

void fake_card_free(FakeCard *card)
{
    for (int index = 0; index < card->count; index++) {
        FakeCardFile *file = &card->files[index];
        for (uint32_t page = 0; page < page_count(file->size); page++) {
            free(file->pages[page]);
        }
        free(file->pages);
    }
    card->count = 0;
}

void fake_card_copy(FakeCard *to, const FakeCard *from)
{
    *to = *from;
    for (int index = 0; index < from->count; index++) {
        const FakeCardFile *source = &from->files[index];
        uint32_t pages = page_count(source->size);
        to->files[index].pages = calloc(pages, sizeof(uint8_t *));
        for (uint32_t page = 0; page < pages; page++) {
            if (source->pages[page] != NULL) {
                to->files[index].pages[page] = malloc(FAKE_CARD_PAGE);
                memcpy(to->files[index].pages[page], source->pages[page], FAKE_CARD_PAGE);
            }
        }
    }
}

void fake_card_cut(FakeCard *card, int operation, uint32_t torn_bytes)
{
    card->operations = 0;
    card->cut_at = operation;
    card->torn_bytes = torn_bytes;
}

void fake_card_restore_power(FakeCard *card)
{
    card->dead = false;
    card->cut_at = FAKE_CARD_NEVER;
    card->operations = 0;
}

FakeCardFile *fake_card_find(FakeCard *card, const char *path)
{
    for (int index = 0; index < card->count; index++) {
        if (strcmp(card->files[index].path, path) == 0) {
            return &card->files[index];
        }
    }
    return NULL;
}

static bool cut_now(FakeCard *card)
{
    if (!card->dead && card->operations++ == card->cut_at) {
        card->dead = true;
        return true;
    }
    return false;
}

static SegmentFileResult reserve(void *context, const char *path, uint32_t size)
{
    FakeCard *card = context;
    if (fake_card_find(card, path) != NULL) {
        return SEGMENT_FILE_OK;
    }
    card->reserves++;
    if (card->dead || cut_now(card)) {
        return SEGMENT_FILE_FAILED;
    }
    if (size > card->free_bytes || card->count == FAKE_CARD_FILES) {
        return SEGMENT_FILE_NO_SPACE;
    }
    FakeCardFile *file = &card->files[card->count++];
    memset(file->path, 0, sizeof file->path);
    strncpy(file->path, path, sizeof file->path - 1);
    file->pages = calloc(page_count(size), sizeof(uint8_t *));
    file->size = size;
    card->free_bytes -= size;
    return SEGMENT_FILE_OK;
}

static uint8_t *page_of(FakeCardFile *file, uint32_t page)
{
    if (file->pages[page] == NULL) {
        file->pages[page] = malloc(FAKE_CARD_PAGE);
        memset(file->pages[page], GARBAGE, FAKE_CARD_PAGE);
        if (page == 0) {
            memset(file->pages[page], 0, CLEARED_BYTES);
        }
    }
    return file->pages[page];
}

static bool in_file(const FakeCardFile *file, uint32_t offset, uint32_t length)
{
    return file != NULL && offset <= file->size && length <= file->size - offset;
}

bool fake_card_read(FakeCard *card, const char *path, uint32_t offset, void *buffer,
                    uint32_t length)
{
    FakeCardFile *file = fake_card_find(card, path);
    if (!in_file(file, offset, length)) {
        return false;
    }
    uint8_t *to = buffer;
    for (uint32_t index = 0; index < length; index++) {
        uint32_t at = offset + index;
        to[index] = page_of(file, at / FAKE_CARD_PAGE)[at % FAKE_CARD_PAGE];
    }
    return true;
}

static bool read_file(void *context, const char *path, uint32_t offset, void *buffer,
                      uint32_t length)
{
    return fake_card_read(context, path, offset, buffer, length);
}

static void put(FakeCardFile *file, uint32_t offset, const uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        uint32_t at = offset + index;
        page_of(file, at / FAKE_CARD_PAGE)[at % FAKE_CARD_PAGE] = bytes[index];
    }
}

static bool write_file(void *context, const char *path, uint32_t offset, const void *bytes,
                       uint32_t length)
{
    FakeCard *card = context;
    FakeCardFile *file = fake_card_find(card, path);
    if (!in_file(file, offset, length)) {
        return false;
    }
    card->writes++;
    if (card->dead) {
        return false;
    }
    if (cut_now(card)) {
        put(file, offset, bytes, card->torn_bytes < length ? card->torn_bytes : length);
        return false;
    }
    put(file, offset, bytes, length);
    return true;
}

SegmentFiles fake_card_files(FakeCard *card)
{
    return (SegmentFiles){
        .reserve = reserve, .read = read_file, .write = write_file, .context = card};
}
