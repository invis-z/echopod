#include "check.h"
#include "gfx/canvas.h"
#include "ui/rippl_logo.h"

enum {
    SIDE = 80
};

static Color pixels[96 * 96];

static void the_note_head_is_solid_and_the_corners_are_empty(void)
{
    CHECK_EQUAL(COLOR_ALPHA_OPAQUE, rippl_logo_alpha(&RIPPL_LOGO_SPLASH, 40, 66));
    CHECK_EQUAL(0, rippl_logo_alpha(&RIPPL_LOGO_SPLASH, 0, 0));
    CHECK_EQUAL(0, rippl_logo_alpha(&RIPPL_LOGO_SPLASH, SIDE - 1, SIDE - 1));
}

static void the_ring_is_solid_and_the_gap_inside_it_is_empty(void)
{
    CHECK_EQUAL(COLOR_ALPHA_OPAQUE, rippl_logo_alpha(&RIPPL_LOGO_SPLASH, 40, 4));
    CHECK_EQUAL(0, rippl_logo_alpha(&RIPPL_LOGO_SPLASH, 40, 13));
}

static void outside_the_mask_there_is_nothing(void)
{
    CHECK_EQUAL(0, rippl_logo_alpha(&RIPPL_LOGO_SPLASH, -1, 40));
    CHECK_EQUAL(0, rippl_logo_alpha(&RIPPL_LOGO_SPLASH, 40, SIDE));
    CHECK_EQUAL(SIDE, RIPPL_LOGO_SPLASH.side);
}

static void the_label_size_has_its_head_and_ring_where_the_splash_does(void)
{
    CHECK_EQUAL(48, RIPPL_LOGO_LABEL.side);
    CHECK_EQUAL(COLOR_ALPHA_OPAQUE, rippl_logo_alpha(&RIPPL_LOGO_LABEL, 24, 40));
    CHECK_EQUAL(COLOR_ALPHA_OPAQUE, rippl_logo_alpha(&RIPPL_LOGO_LABEL, 24, 2));
    CHECK_EQUAL(0, rippl_logo_alpha(&RIPPL_LOGO_LABEL, 24, 8));
    CHECK_EQUAL(0, rippl_logo_alpha(&RIPPL_LOGO_LABEL, 0, 0));
}

static int count(Color color)
{
    int found = 0;
    for (int index = 0; index < 96 * 96; index++) {
        found += pixels[index] == color ? 1 : 0;
    }
    return found;
}

static void drawing_at_full_alpha_paints_the_shape_in_the_colour(void)
{
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, 96, 96});
    canvas_clear(&canvas, 0);
    rippl_logo_draw(&canvas, &RIPPL_LOGO_SPLASH, (Point){8, 8}, RIPPL_ORANGE, COLOR_ALPHA_OPAQUE);
    CHECK_EQUAL(RIPPL_ORANGE, pixels[(8 + 66) * 96 + 8 + 40]);
    CHECK_EQUAL(0, pixels[(8 + 13) * 96 + 8 + 40]);
    CHECK(count(RIPPL_ORANGE) > 300);
}

static void drawing_at_zero_alpha_paints_nothing(void)
{
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, 96, 96});
    canvas_clear(&canvas, 0);
    rippl_logo_draw(&canvas, &RIPPL_LOGO_SPLASH, (Point){8, 8}, RIPPL_ORANGE, 0);
    CHECK_EQUAL(96 * 96, count(0));
}

static void drawing_is_clipped_to_the_canvas(void)
{
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, 96, 96});
    canvas_clear(&canvas, 0);
    Canvas corner = canvas_clipped(&canvas, (Rect){0, 0, 16, 16});
    rippl_logo_draw(&corner, &RIPPL_LOGO_SPLASH, (Point){0, 0}, RIPPL_ORANGE, COLOR_ALPHA_OPAQUE);
    for (int y = 0; y < 96; y++) {
        for (int x = 0; x < 96; x++) {
            CHECK(pixels[y * 96 + x] == 0 || (x < 16 && y < 16));
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_note_head_is_solid_and_the_corners_are_empty),
        TEST_CASE(the_ring_is_solid_and_the_gap_inside_it_is_empty),
        TEST_CASE(outside_the_mask_there_is_nothing),
        TEST_CASE(the_label_size_has_its_head_and_ring_where_the_splash_does),
        TEST_CASE(drawing_at_full_alpha_paints_the_shape_in_the_colour),
        TEST_CASE(drawing_at_zero_alpha_paints_nothing),
        TEST_CASE(drawing_is_clipped_to_the_canvas),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
