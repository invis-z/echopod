#include "card_files.h"

#include <stddef.h>
#include <stdint.h>

#include "app/update_record.h"
#include "bluetooth_store.h"
#include "equalizer_store.h"
#include "favorites_store.h"
#include "folders_store.h"
#include "hal/storage.h"
#include "library_rescan.h"
#include "library_store.h"
#include "music_check.h"
#include "player.h"
#include "settings_store.h"
#include "spoken_store.h"

enum {
    SMALL_FILE_BYTES = 4096
};

typedef struct CardFile {
    const char *path;
    uint32_t size;
} CardFile;

static const CardFile CARD_FILES[] = {
    {SETTINGS_STORE_PATH, SMALL_FILE_BYTES},  {PLAYER_RESUME_PATH, SMALL_FILE_BYTES},
    {FAVORITES_STORE_PATH, SMALL_FILE_BYTES}, {MUSIC_CHECK_PATH, SMALL_FILE_BYTES},
    {PLAYER_UP_NEXT_PATH, SMALL_FILE_BYTES},  {SPOKEN_STORE_PATH, SMALL_FILE_BYTES},
    {RESCAN_TIMES_PATH, SMALL_FILE_BYTES},    {BLUETOOTH_STORE_PATH, SMALL_FILE_BYTES},
    {EQUALIZER_STORE_PATH, SMALL_FILE_BYTES}, {UPDATE_RECORD_PATH, SMALL_FILE_BYTES},
    {FOLDERS_STORE_PATH, SMALL_FILE_BYTES},
};

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
