#include "error_log.h"

#include <stdbool.h>

#include "core/hash.h"
#include "core/string_builder.h"
#include "hal/clock.h"
#include "hal/storage.h"

const char ERROR_LOG_PATH[] = "/.rippl/errors.log";

enum {
    MILLISECONDS_PER_SECOND = 1000,
    SECONDS_PER_MINUTE = 60,
    SECONDS_PER_HOUR = 3600
};

static const ClockStore *log_clock;
static bool known;
static bool made;
static bool missing;
static bool busy;
static uint32_t size;
static uint32_t lines;
static uint32_t last_entry;

void error_log_use_clock(const ClockStore *clock)
{
    log_clock = clock;
}

void error_log_forget(void)
{
    known = false;
    made = false;
    missing = false;
    size = 0;
    lines = 0;
    last_entry = 0;
}

static uint32_t entry_of(const char *what, const char *path)
{
    uint32_t hash = HASH_START;
    for (uint32_t index = 0; what[index] != '\0'; index++) {
        hash = hash_update(hash, &what[index], 1);
    }
    hash = hash_update(hash, ":", 1);
    for (uint32_t index = 0; path[index] != '\0'; index++) {
        hash = hash_update(hash, &path[index], 1);
    }
    return hash | 1u;
}

static bool find_size(void)
{
    if (missing) {
        return false;
    }
    if (!known) {
        HalFile file;
        made = hal_file_open(&file, ERROR_LOG_PATH);
        size = made ? file.size : 0;
        if (made) {
            hal_file_close(&file);
        }
        known = true;
    }
    return true;
}

static bool make_room(uint32_t length)
{
    HalReserveResult result = made ? hal_file_grow(ERROR_LOG_PATH, size + length)
                                   : hal_file_reserve(ERROR_LOG_PATH, length);
    return result == HAL_RESERVE_CREATED;
}

static void append_two(StringBuilder *builder, uint32_t value)
{
    string_builder_append_char(builder, (char)('0' + value / 10 % 10));
    string_builder_append_char(builder, (char)('0' + value % 10));
}

static void append_time(StringBuilder *builder)
{
    WallCivil civil;
    if (log_clock != NULL && clock_store_now(log_clock, hal_clock_milliseconds(), &civil)) {
        char text[WALL_CLOCK_DATE_TIME_CAPACITY];
        wall_clock_date_time_text(&civil, text, sizeof text);
        string_builder_append(builder, text);
        return;
    }
    uint32_t seconds = hal_clock_milliseconds() / MILLISECONDS_PER_SECOND;
    string_builder_append_char(builder, '+');
    string_builder_append_unsigned(builder, seconds / SECONDS_PER_HOUR);
    string_builder_append_char(builder, ':');
    append_two(builder, seconds % SECONDS_PER_HOUR / SECONDS_PER_MINUTE);
    string_builder_append_char(builder, ':');
    append_two(builder, seconds % SECONDS_PER_MINUTE);
}

static void write_line(const char *what, const char *path)
{
    char line[ERROR_LOG_LINE_CAPACITY];
    StringBuilder builder = string_builder_make(line, sizeof line - 1);
    append_time(&builder);
    string_builder_append(&builder, "  ");
    string_builder_append(&builder, what);
    if (path[0] != '\0') {
        string_builder_append(&builder, ": ");
    }
    for (uint32_t index = 0; path[index] != '\0'; index++) {
        char character = path[index];
        string_builder_append_char(&builder,
                                   character == '\n' || character == '\r' ? '?' : character);
    }
    uint32_t length = 0;
    while (line[length] != '\0') {
        length++;
    }
    line[length++] = '\n';
    if (!make_room(length) || !hal_file_write_at(ERROR_LOG_PATH, size, line, length)) {
        missing = true;
        return;
    }
    made = true;
    size += length;
    lines++;
}

void error_log_add(const char *what, const char *path)
{
    if (busy || lines > ERROR_LOG_SESSION_LINES) {
        return;
    }
    uint32_t entry = entry_of(what, path);
    if (entry == last_entry) {
        return;
    }
    busy = true;
    if (find_size()) {
        last_entry = entry;
        if (lines == ERROR_LOG_SESSION_LINES) {
            write_line("too many errors, the rest of this session isn't logged", "");
        } else {
            write_line(what, path);
        }
    }
    busy = false;
}
