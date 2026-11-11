#include "sound/equalizer_file.h"

#include "core/hash.h"

enum {
    VERSION = 1,
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    PRESET_FIELD = 6,
    GAINS_FIELD = 7,
    NAME_LENGTH_FIELD = 14,
    NAME_FIELD = 16,
    CHECKSUM_FIELD = NAME_FIELD + EQUALIZER_PRESET_NAME_CAPACITY,
    GAIN_OFFSET = -EQUALIZER_GAIN_MIN_DB,
    GAIN_STEPS = EQUALIZER_GAIN_MAX_DB - EQUALIZER_GAIN_MIN_DB,
    BITS_PER_BYTE = 8
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'M', 'E', 'Q'};

EqualizerChoice equalizer_file_defaults(void)
{
    return (EqualizerChoice){.preset = EQUALIZER_PRESET_OFF};
}

static uint32_t checksum_of(const uint8_t *bytes, uint32_t field)
{
    return hash_update(HASH_START, bytes, field);
}

void equalizer_file_encode(const EqualizerChoice *choice, uint8_t *bytes)
{
    for (int index = 0; index < EQUALIZER_FILE_SIZE; index++) {
        bytes[index] = index < MAGIC_SIZE ? MAGIC[index] : 0;
    }
    bytes[VERSION_FIELD] = VERSION;
    bytes[PRESET_FIELD] = (uint8_t)choice->preset;
    for (int band = 0; band < EQUALIZER_CUSTOM_BANDS; band++) {
        bytes[GAINS_FIELD + band] = (uint8_t)(choice->custom_gains[band] + GAIN_OFFSET);
    }
    uint32_t length = 0;
    while (length + 1 < EQUALIZER_PRESET_NAME_CAPACITY && choice->preset_name[length] != '\0') {
        length++;
    }
    bytes[NAME_LENGTH_FIELD] = (uint8_t)length;
    for (uint32_t index = 0; index < length; index++) {
        bytes[NAME_FIELD + index] = (uint8_t)choice->preset_name[index];
    }
    uint32_t checksum = checksum_of(bytes, CHECKSUM_FIELD);
    for (int index = 0; index < 4; index++) {
        bytes[CHECKSUM_FIELD + index] = (uint8_t)(checksum >> (BITS_PER_BYTE * index));
    }
}

static bool magic_valid(const uint8_t *bytes)
{
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (bytes[index] != MAGIC[index]) {
            return false;
        }
    }
    return true;
}

static bool header_valid(const uint8_t *bytes)
{
    uint32_t checksum = 0;
    for (int index = 3; index >= 0; index--) {
        checksum = checksum << BITS_PER_BYTE | bytes[CHECKSUM_FIELD + (uint32_t)index];
    }
    return magic_valid(bytes) && bytes[VERSION_FIELD] == VERSION &&
           checksum == checksum_of(bytes, CHECKSUM_FIELD);
}

static bool values_valid(const uint8_t *bytes)
{
    if (bytes[PRESET_FIELD] >= EQUALIZER_PRESET_COUNT) {
        return false;
    }
    for (int band = 0; band < EQUALIZER_CUSTOM_BANDS; band++) {
        if (bytes[GAINS_FIELD + band] > GAIN_STEPS) {
            return false;
        }
    }
    return bytes[NAME_LENGTH_FIELD] < EQUALIZER_PRESET_NAME_CAPACITY;
}

bool equalizer_file_decode(const uint8_t *bytes, uint32_t length, EqualizerChoice *choice)
{
    if (length < EQUALIZER_FILE_SIZE || !header_valid(bytes) || !values_valid(bytes)) {
        return false;
    }
    *choice = equalizer_file_defaults();
    choice->preset = (EqualizerPreset)bytes[PRESET_FIELD];
    for (int band = 0; band < EQUALIZER_CUSTOM_BANDS; band++) {
        choice->custom_gains[band] = (int8_t)(bytes[GAINS_FIELD + band] - GAIN_OFFSET);
    }
    uint32_t name_length = bytes[NAME_LENGTH_FIELD];
    for (uint32_t index = 0; index < name_length; index++) {
        choice->preset_name[index] = (char)bytes[NAME_FIELD + index];
    }
    choice->preset_name[name_length] = '\0';
    return true;
}

EqualizerCurve equalizer_file_curve(const EqualizerChoice *choice)
{
    if (choice->preset == EQUALIZER_PRESET_FILE) {
        return (EqualizerCurve){0};
    }
    return equalizer_presets_curve(choice->preset, choice->custom_gains);
}
