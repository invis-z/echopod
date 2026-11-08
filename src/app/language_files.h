#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "settings/settings.h"

enum {
    LANGUAGE_BUILTIN_COUNT = 2,
    LANGUAGE_FILE_PATH_CAPACITY = 32
};

extern const char LANGUAGE_FOLDER_PATH[];

bool language_files_path(Language language, char *path, size_t capacity);
bool language_files_available(Language language);
bool language_files_attach(Language language);
