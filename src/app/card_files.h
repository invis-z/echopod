#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/storage.h"

typedef struct CardFilesReport {
    bool created;
    bool card_full;
} CardFilesReport;

CardFilesReport card_files_prepare(void (*creating)(void));
