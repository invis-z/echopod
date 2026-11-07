#include "card_files.h"

#include <stddef.h>
#include <stdint.h>

#include "app/update_record.h"
#include "bluetooth_store.h"
#include "cover_cache.h"
#include "equalizer_store.h"
#include "favorites_store.h"
#include "folders_store.h"
#include "hal/storage.h"
#include "library/cover_build.h"
#include "library/covers.h"
#include "library_rescan.h"
#include "library_store.h"
#include "music_check.h"
#include "player.h"
#include "settings_store.h"
#include "spoken_store.h"

enum {
    SMALL_FILE_BYTES = 4096,
    COVERS_FILE_BYTES = 80 * 1024 * 1024,
    COVER_SLOT_BYTES = COVERS_ENTRY_SIZE + 4 + COVER_BUILD_RECORD_BYTES,
    COVER_SLOT_STEP = 256,
    COVER_SLOT_LIMIT = 8192
};

typedef struct CardFile {
    const char *path;
    uint32_t size;
} CardFile;

static const CardFile CARD_FILES[] = {
    {SETTINGS_STORE_PATH, SMALL_FILE_BYTES},  {PLAYER_RESUME_PATH, SMALL_FILE_BYTES},
    {FAVORITES_STORE_PATH, SMALL_FILE_BYTES}, {COVER_CACHE_PATH, COVERS_FILE_BYTES},
    {MUSIC_CHECK_PATH, SMALL_FILE_BYTES},     {PLAYER_UP_NEXT_PATH, SMALL_FILE_BYTES},
    {SPOKEN_STORE_PATH, SMALL_FILE_BYTES},    {RESCAN_TIMES_PATH, SMALL_FILE_BYTES},
    {BLUETOOTH_STORE_PATH, SMALL_FILE_BYTES}, {EQUALIZER_STORE_PATH, SMALL_FILE_BYTES},
    {UPDATE_RECORD_PATH, SMALL_FILE_BYTES},   {FOLDERS_STORE_PATH, SMALL_FILE_BYTES},
};

static uint32_t slots_for(uint32_t albums)
{
    return albums < COVER_SLOT_LIMIT ? albums : COVER_SLOT_LIMIT;
}

uint32_t card_files_covers_size(uint32_t albums)
{
    uint32_t slots = albums + albums / 4 + COVER_SLOT_STEP - 1;
    return COVERS_HEADER_SIZE + slots_for(slots - slots % COVER_SLOT_STEP) * COVER_SLOT_BYTES;
}

void card_files_fit_covers(uint32_t albums)
{
    HalFile file;
    if (!hal_file_open(&file, COVER_CACHE_PATH)) {
        return;
    }
    uint32_t size = file.size;
    hal_file_close(&file);
    uint32_t slots = size > COVERS_HEADER_SIZE ? (size - COVERS_HEADER_SIZE) / COVER_SLOT_BYTES : 0;
    if (slots >= slots_for(albums) ||
        hal_file_grow(COVER_CACHE_PATH, card_files_covers_size(albums)) != HAL_RESERVE_NO_SPACE) {
        return;
    }
    (void)hal_file_grow(COVER_CACHE_PATH,
                        COVERS_HEADER_SIZE + slots_for(albums) * COVER_SLOT_BYTES);
}

HalReserveResult card_files_reserve(const char *path)
{
    for (size_t index = 0; index < sizeof CARD_FILES / sizeof CARD_FILES[0]; index++) {
        if (CARD_FILES[index].path == path) {
            return hal_file_reserve(path, CARD_FILES[index].size);
        }
    }
    return HAL_RESERVE_FAILED;
}

CardFilesReport card_files_prepare(void (*creating)(void))
{
    CardFilesReport report = {.created = false, .card_full = false};
    for (size_t index = 0; index < sizeof CARD_FILES / sizeof CARD_FILES[0]; index++) {
        HalReserveResult result = hal_file_reserve(CARD_FILES[index].path, CARD_FILES[index].size);
        if (result == HAL_RESERVE_CREATED && creating != NULL) {
            creating();
            creating = NULL;
        }
        report.card_full |= result == HAL_RESERVE_NO_SPACE;
        report.created |= result == HAL_RESERVE_CREATED;
    }
    return report;
}
