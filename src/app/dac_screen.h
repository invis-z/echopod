#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/app.h"
#include "gfx/canvas.h"
#include "ui/theme.h"

enum {
    DAC_SCREEN_STATS_LINES = 8,
    DAC_SCREEN_STATS_CAPACITY = 56,
    DAC_SCREEN_RATE_CAPACITY = 12
};

void dac_screen_rate_text(uint32_t rate, char *text, size_t capacity);
int dac_screen_stats_lines(const AppDacStats *stats, bool portrait,
                           char lines[DAC_SCREEN_STATS_LINES][DAC_SCREEN_STATS_CAPACITY]);
Rect dac_screen_stats_area(const Theme *theme, int lines);
void dac_screen_draw(Canvas *canvas, const Theme *theme, const AppDacView *view);
