#pragma once

#include <stdint.h>

#include "clock_store.h"

extern const char ERROR_LOG_PATH[];

enum {
    ERROR_LOG_LINE_CAPACITY = 320,
    ERROR_LOG_SESSION_LINES = 64
};

void error_log_use_clock(const ClockStore *clock);
void error_log_add(const char *what, const char *path);
void error_log_forget(void);
