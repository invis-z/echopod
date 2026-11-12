#include "ui/rippl_logo.h"

#include "rippl_logo_data.h"

_Static_assert((int)RIPPL_LOGO_LEVELS == (int)COLOR_ALPHA_OPAQUE, "a logo level is a blend alpha");

const RipplLogo RIPPL_LOGO_SPLASH = {RIPPL_LOGO_SPLASH_COVERAGE, RIPPL_LOGO_SPLASH_SIDE};

uint8_t rippl_logo_alpha(const RipplLogo *logo, int x, int y)
{
    if (x < 0 || y < 0 || x >= logo->side || y >= logo->side) {
        return 0;
    }
    int index = y * logo->side + x;
    uint8_t pair = logo->coverage[index / 2];
    return (uint8_t)((index % 2 == 0 ? pair : pair >> 4) & RIPPL_LOGO_LEVELS);
}

void rippl_logo_draw(Canvas *canvas, const RipplLogo *logo, Point origin, Color color,
                     uint8_t alpha)
{
    for (int y = 0; y < logo->side; y++) {
        for (int x = 0; x < logo->side; x++) {
            uint8_t coverage = rippl_logo_alpha(logo, x, y);
            if (coverage != 0) {
                canvas_blend_pixel(canvas, (Point){origin.x + x, origin.y + y}, color,
                                   (uint8_t)(coverage * alpha / COLOR_ALPHA_OPAQUE));
            }
        }
    }
}
