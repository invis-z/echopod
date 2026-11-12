#pragma once

#include <stdint.h>

#include "gfx/canvas.h"
#include "gfx/color.h"

#define RIPPL_ORANGE COLOR_RGB(255, 147, 30)

typedef struct RipplLogo {
    const uint8_t *coverage;
    int side;
} RipplLogo;

extern const RipplLogo RIPPL_LOGO_SPLASH;
extern const RipplLogo RIPPL_LOGO_LABEL;

uint8_t rippl_logo_alpha(const RipplLogo *logo, int x, int y);
void rippl_logo_draw(Canvas *canvas, const RipplLogo *logo, Point origin, Color color,
                     uint8_t alpha);
