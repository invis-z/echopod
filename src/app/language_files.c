#include "language_files.h"

#include <stdint.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "core/volume_path.h"
#include "hal/language.h"
#include "hal/storage.h"

const char LANGUAGE_FOLDER_PATH[] = "/" VOLUME_INTERNAL_NAME "/Languages";

static const char LANGUAGE_CODES[LANGUAGE_COUNT][3] = {"en", "zh", "ja", "ko", "de", "fr",
                                                       "es", "it", "pt", "nl", "sv", "da",
                                                       "no", "fi", "ru", "uk", "pl", "id"};

const char *language_files_code(Language language)
{
    return (unsigned)language < LANGUAGE_COUNT ? LANGUAGE_CODES[language] : "";
}

bool language_files_path(Language language, char *path, size_t capacity)
{
    if ((unsigned)language >= LANGUAGE_COUNT) {
        return false;
    }
    StringBuilder builder = string_builder_make(path, capacity);
    string_builder_append(&builder, LANGUAGE_FOLDER_PATH);
    string_builder_append(&builder, "/");
    string_builder_append(&builder, LANGUAGE_CODES[language]);
    string_builder_append(&builder, ".bin");
    return !builder.overflowed;
}

static bool open_pack(Language language, HalFile *file, uint32_t capacity)
{
    char path[LANGUAGE_FILE_PATH_CAPACITY];
    if ((int)language < LANGUAGE_BUILTIN_COUNT ||
        !language_files_path(language, path, sizeof path) || !hal_file_open(file, path)) {
        return false;
    }
    if (file->size < I18N_HEADER_BYTES || file->size > capacity) {
        hal_file_close(file);
        return false;
    }
    return true;
}

bool language_files_available(Language language)
{
    if ((unsigned)language >= LANGUAGE_COUNT) {
        return false;
    }
    if ((int)language < LANGUAGE_BUILTIN_COUNT) {
        return true;
    }
    uint32_t capacity = 0;
    (void)hal_language_buffer(&capacity);
    HalFile file;
    if (!open_pack(language, &file, capacity)) {
        return false;
    }
    uint8_t header[I18N_HEADER_BYTES];
    bool valid =
        hal_file_read(file, 0, header, sizeof header) && i18n_header_valid(header, file.size);
    hal_file_close(&file);
    return valid;
}

bool language_files_attach(Language language)
{
    i18n_detach();
    uint32_t capacity = 0;
    uint8_t *buffer = hal_language_buffer(&capacity);
    HalFile file;
    if (buffer == NULL || !open_pack(language, &file, capacity)) {
        return false;
    }
    uint32_t size = file.size;
    bool read = hal_file_read(file, 0, buffer, size);
    hal_file_close(&file);
    return read && i18n_attach(buffer, size);
}
