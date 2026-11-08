#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sound/equalizer_presets.h"

enum {
    EQUALIZER_FILE_SIZE = 75,
    EQUALIZER_PRESET_NAME_CAPACITY = 64
};

typedef struct EqualizerChoice {
    EqualizerPreset preset;
    char preset_name[EQUALIZER_PRESET_NAME_CAPACITY];
} EqualizerChoice;

EqualizerChoice equalizer_file_defaults(void);
void equalizer_file_encode(const EqualizerChoice *choice, uint8_t *bytes);
bool equalizer_file_decode(const uint8_t *bytes, uint32_t length, EqualizerChoice *choice);
