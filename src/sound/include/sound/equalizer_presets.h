#pragma once

typedef enum EqualizerPreset {
    EQUALIZER_PRESET_OFF,
    EQUALIZER_PRESET_FILE
} EqualizerPreset;

enum {
    EQUALIZER_PRESET_COUNT = EQUALIZER_PRESET_FILE + 1
};
