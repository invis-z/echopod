#include "check.h"
#include "np_faces_test.h"

enum {
    DEEP = COLOR_RGB(24, 20, 24),
    SHELL = COLOR_RGB(72, 72, 80),
    SHELL_HI = COLOR_RGB(96, 96, 104),
    TAPE = COLOR_RGB(120, 76, 48),
    LABEL = COLOR_RGB(240, 228, 200),
    INK = COLOR_RGB(40, 32, 32),
    INK_SOFT = COLOR_RGB(96, 80, 72),
    RED = COLOR_RGB(176, 48, 32),
    AMBER = COLOR_RGB(232, 152, 40),
    RULE = COLOR_RGB(184, 168, 136),
    COVER_RED = 0xF800,
    LANDSCAPE = 0,
    PORTRAIT = 1,
    BAND_TOP = 76,
    BAND_BOTTOM = 98,
    BAND_ROWS = 22,
    CARD_OFFSET = 148,
    COUNTER_OFFSET = 108,
    PORTRAIT_TOP = 31,
    COUNTER_GAP_MS = 1700,
    TEN_MINUTES_MS = 600000,
    TURN_TICKS = 32,
    STEPS = 5,
    STEP_MS = 100
};

typedef struct Probe {
    NpTestState state;
    int x;
    int y;
    Color expected;
} Probe;

static const char LETTER[] = "A";
static Color pixels[NP_TEST_PIXELS];
static Color other[NP_TEST_PIXELS];

static const Probe LANDSCAPE_PROBES[] = {
    {NP_TEST_PLAYING, 5, 100, DEEP},      {NP_TEST_PLAYING, 12, 100, SHELL},
    {NP_TEST_PLAYING, 18, 8, RULE},       {NP_TEST_PLAYING, 18, 9, DEEP},
    {NP_TEST_PLAYING, 30, 40, LABEL},     {NP_TEST_PLAYING, 30, 56, AMBER},
    {NP_TEST_PLAYING, 28, 90, RED},       {NP_TEST_PLAYING, 160, 59, DEEP},
    {NP_TEST_PLAYING, 152, 87, DEEP},     {NP_TEST_PLAYING, 99, 87, DEEP},
    {NP_TEST_PLAYING, 110, 87, LABEL},    {NP_TEST_PLAYING, 115, 87, TAPE},
    {NP_TEST_PLAYING, 235, 87, TAPE},     {NP_TEST_PLAYING, 160, 135, SHELL_HI},
    {NP_TEST_PLAYING, 103, 143, DEEP},    {NP_TEST_PLAYING, 90, 154, DEEP},
    {NP_TEST_PLAYING, 90, 158, AMBER},    {NP_TEST_PLAYING, 200, 158, TAPE},
    {NP_TEST_PAUSED, 152, 87, LABEL},     {NP_TEST_PAUSED, 160, 87, DEEP},
    {NP_TEST_SCRUBBING, 90, 158, LABEL},  {NP_TEST_SCRUBBING, 200, 158, LABEL},
    {NP_TEST_SCRUBBING, 230, 158, TAPE},  {NP_TEST_VOLUME, 140, 158, LABEL},
    {NP_TEST_VOLUME, 141, 158, TAPE},     {NP_TEST_BADGE, 167, 87, LABEL},
    {NP_TEST_NO_COVER, 90, 158, AMBER},   {NP_TEST_PLAYING, 65, 56, AMBER},
    {NP_TEST_PLAYING, 66, 55, AMBER},     {NP_TEST_PLAYING, 80, 129, SHELL},
    {NP_TEST_PLAYING, 90, 129, SHELL_HI},
};

static const Probe PORTRAIT_PROBES[] = {
    {NP_TEST_PLAYING, 50, 47, LABEL},    {NP_TEST_PLAYING, 66, 47, LABEL},
    {NP_TEST_PLAYING, 46, 47, TAPE},     {NP_TEST_PLAYING, 58, 47, DEEP},
    {NP_TEST_PLAYING, 58, 82, DEEP},     {NP_TEST_PLAYING, 110, 82, DEEP},
    {NP_TEST_PLAYING, 50, 90, DEEP},     {NP_TEST_PLAYING, 40, 72, SHELL},
    {NP_TEST_PLAYING, 50, 72, SHELL_HI}, {NP_TEST_PLAYING, 34, 31, AMBER},
    {NP_TEST_PLAYING, 35, 30, LABEL},    {NP_TEST_PLAYING, 50, 92, AMBER},
    {NP_TEST_PLAYING, 110, 92, TAPE},    {NP_TEST_SCRUBBING, 100, 92, LABEL},
    {NP_TEST_SCRUBBING, 115, 92, TAPE},  {NP_TEST_VOLUME, 73, 92, LABEL},
    {NP_TEST_VOLUME, 74, 92, TAPE},      {NP_TEST_PAUSED, 78, 47, LABEL},
};

typedef struct TextProbe {
    NpTestState state;
    Rect box;
    Color shown;
    Color hidden;
} TextProbe;

static const TextProbe LANDSCAPE_TEXT[] = {
    {NP_TEST_PLAYING, {33, 8, 253, 20}, INK, INK_SOFT},
    {NP_TEST_PLAYING, {33, 31, 253, 20}, INK_SOFT, INK},
    {NP_TEST_PLAYING, {254, 59, 40, 56}, LABEL, INK},
};

static const TextProbe PORTRAIT_TEXT[] = {
    {NP_TEST_PLAYING, {16, 151, 137, 20}, INK, INK_SOFT},
    {NP_TEST_PLAYING, {16, 174, 137, 20}, INK_SOFT, INK},
    {NP_TEST_PLAYING, {16, 197, 137, 20}, INK_SOFT, INK},
    {NP_TEST_PLAYING, {16, 221, 137, 16}, INK_SOFT, INK},
    {NP_TEST_PLAYING, {100, 10, 44, 16}, INK_SOFT, INK},
};

static void draw(NpTestScene scene, Color *target)
{
    np_test_draw(&scene, target);
    CHECK(np_test_area_painted(target, scene.layout));
}

static int count_in(const Color *target, NpTestLayout layout, Rect box, Color color)
{
    int count = 0;
    for (int y = box.y; y < box.y + box.height; y++) {
        for (int x = box.x; x < box.x + box.width; x++) {
            count += np_test_pixel(target, layout, x, y) == color;
        }
    }
    return count;
}

static int top_of(NpTestLayout layout)
{
    return layout.area.y + PORTRAIT_TOP;
}

static void the_reels_turn_counter_clockwise(void)
{
    NpTestScene scene =
        np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[LANDSCAPE], NP_TEST_PLAYING);
    scene.frame.tick = 0;
    draw(scene, pixels);
    CHECK_EQUAL(DEEP, np_test_pixel(pixels, scene.layout, 91, 87));
    scene.frame.tick = 8;
    draw(scene, pixels);
    CHECK_EQUAL(LABEL, np_test_pixel(pixels, scene.layout, 91, 87));
    CHECK_EQUAL(DEEP, np_test_pixel(pixels, scene.layout, 107, 87));
}

static void the_backdrop_colour_is_at_the_corner_of_the_area(void)
{
    for (int layout = 0; layout < NP_TEST_LAYOUT_COUNT; layout++) {
        NpTestScene scene =
            np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[layout], NP_TEST_PLAYING);
        draw(scene, pixels);
        Rect area = scene.layout.area;
        CHECK_EQUAL(DEEP, np_test_pixel(pixels, scene.layout, area.x, area.y));
    }
}

static void check_probes(const Probe *probes, size_t count, int layout)
{
    NpTestLayout shape = NP_TEST_LAYOUTS[layout];
    for (size_t index = 0; index < count; index++) {
        int top = layout == LANDSCAPE ? 0 : top_of(shape);
        draw(np_test_scene(NP_FACE_CASSETTE, shape, probes[index].state), pixels);
        CHECK_EQUAL(probes[index].expected,
                    np_test_pixel(pixels, shape, probes[index].x, top + probes[index].y));
    }
}

static void landscape_pixels_show_every_state(void)
{
    check_probes(LANDSCAPE_PROBES, sizeof LANDSCAPE_PROBES / sizeof LANDSCAPE_PROBES[0], LANDSCAPE);
}

static void portrait_pixels_show_every_state_at_both_status_bar_heights(void)
{
    for (int layout = PORTRAIT; layout < NP_TEST_LAYOUT_COUNT; layout++) {
        check_probes(PORTRAIT_PROBES, sizeof PORTRAIT_PROBES / sizeof PORTRAIT_PROBES[0], layout);
    }
}

static NpTestScene worded(int layout, NpTestState state, const char *word)
{
    NpTestScene scene = np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[layout], state);
    scene.frame.song = word;
    scene.frame.artist = word;
    scene.frame.album = word;
    scene.frame.codec = word;
    scene.frame.stream = word;
    return scene;
}

static int top_for(int layout)
{
    return layout == LANDSCAPE ? 0 : top_of(NP_TEST_LAYOUTS[layout]);
}

static void check_text(const TextProbe *probes, size_t count, int layout)
{
    NpTestLayout shape = NP_TEST_LAYOUTS[layout];
    for (size_t index = 0; index < count; index++) {
        Rect box = probes[index].box;
        box.y += top_for(layout);
        draw(worded(layout, probes[index].state, LETTER), pixels);
        CHECK(count_in(pixels, shape, box, probes[index].shown) > 0);
        CHECK_EQUAL(0, count_in(pixels, shape, box, probes[index].hidden));
    }
}

static int lum(Color color)
{
    return ((color >> 11) & 31) * 8 + ((color >> 5) & 63) * 4 + (color & 31) * 8;
}

static int darkest_in(const Color *target, NpTestLayout layout, Rect box)
{
    int darkest = lum(0xFFFF);
    for (int y = box.y; y < box.y + box.height; y++) {
        for (int x = box.x; x < box.x + box.width; x++) {
            int now = lum(np_test_pixel(target, layout, x, y));
            darkest = now < darkest ? now : darkest;
        }
    }
    return darkest;
}

static int brighter_than(const Color *target, NpTestLayout layout, Rect box, Color background)
{
    int count = 0;
    for (int y = box.y; y < box.y + box.height; y++) {
        for (int x = box.x; x < box.x + box.width; x++) {
            count += lum(np_test_pixel(target, layout, x, y)) > lum(background);
        }
    }
    return count;
}

static void feedback_is_set_in_the_strong_ink(void)
{
    static const NpTestState FEEDBACK[] = {NP_TEST_VOLUME, NP_TEST_SCRUBBING};
    static const Rect LINES[] = {{33, 31, 253, 20}, {16, 221, 137, 16}};
    for (int layout = LANDSCAPE; layout <= PORTRAIT; layout++) {
        NpTestLayout shape = NP_TEST_LAYOUTS[layout];
        Rect box = LINES[layout];
        box.y += top_for(layout);
        draw(worded(layout, NP_TEST_PLAYING, "?"), pixels);
        for (size_t index = 0; index < sizeof FEEDBACK / sizeof FEEDBACK[0]; index++) {
            draw(worded(layout, FEEDBACK[index], "?"), other);
            CHECK(darkest_in(other, shape, box) < darkest_in(pixels, shape, box));
        }
    }
}

static void portrait_times_are_set_in_the_strong_ink(void)
{
    NpTestLayout shape = NP_TEST_LAYOUTS[PORTRAIT];
    Rect artist = {16, top_for(PORTRAIT) + 174, 137, 20};
    Rect times = {16, top_for(PORTRAIT) + 238, 137, 16};
    draw(worded(PORTRAIT, NP_TEST_PLAYING, "?"), pixels);
    CHECK(darkest_in(pixels, shape, times) < darkest_in(pixels, shape, artist));
}

static void landscape_times_are_set_in_the_light_ink_on_the_shell(void)
{
    NpTestLayout shape = NP_TEST_LAYOUTS[LANDSCAPE];
    draw(worded(LANDSCAPE, NP_TEST_PLAYING, "?"), pixels);
    CHECK(brighter_than(pixels, shape, (Rect){17, 129, 50, 16}, SHELL) > 0);
    CHECK(brighter_than(pixels, shape, (Rect){253, 129, 50, 16}, SHELL) > 0);
    CHECK_EQUAL(lum(SHELL), darkest_in(pixels, shape, (Rect){17, 129, 50, 16}));
}

static void landscape_text_uses_the_ink_paired_with_its_background(void)
{
    check_text(LANDSCAPE_TEXT, sizeof LANDSCAPE_TEXT / sizeof LANDSCAPE_TEXT[0], LANDSCAPE);
}

static void portrait_text_uses_the_ink_paired_with_its_background(void)
{
    for (int layout = PORTRAIT; layout < NP_TEST_LAYOUT_COUNT; layout++) {
        check_text(PORTRAIT_TEXT, sizeof PORTRAIT_TEXT / sizeof PORTRAIT_TEXT[0], layout);
    }
}

static void the_portrait_card_hangs_below_the_cassette(void)
{
    for (int layout = PORTRAIT; layout < NP_TEST_LAYOUT_COUNT; layout++) {
        NpTestLayout shape = NP_TEST_LAYOUTS[layout];
        int top = top_of(shape);
        int card = top + CARD_OFFSET;
        draw(np_test_scene(NP_FACE_CASSETTE, shape, NP_TEST_PLAYING), pixels);
        CHECK_EQUAL(DEEP, np_test_pixel(pixels, shape, 80, top - 1));
        CHECK_EQUAL(SHELL, np_test_pixel(pixels, shape, 80, top));
        CHECK_EQUAL(SHELL_HI, np_test_pixel(pixels, shape, 80, top + 1));
        CHECK_EQUAL(RED, np_test_pixel(pixels, shape, 12, card));
        CHECK_EQUAL(LABEL, np_test_pixel(pixels, shape, 12, card + 10));
        CHECK_EQUAL(RULE, np_test_pixel(pixels, shape, 20, card + 24));
        CHECK_EQUAL(AMBER, np_test_pixel(pixels, shape, 30, top + 32));
        CHECK_EQUAL(TAPE, np_test_pixel(pixels, shape, 100, top + 93));
    }
}

static void paused_portrait_shows_the_pause_icon_between_the_reels(void)
{
    NpTestLayout shape = NP_TEST_LAYOUTS[PORTRAIT];
    int row = top_of(shape) + 47;
    draw(np_test_scene(NP_FACE_CASSETTE, shape, NP_TEST_PLAYING), pixels);
    draw(np_test_scene(NP_FACE_CASSETTE, shape, NP_TEST_PAUSED), other);
    CHECK_EQUAL(DEEP, np_test_pixel(pixels, shape, 78, row));
    CHECK_EQUAL(LABEL, np_test_pixel(other, shape, 78, row));
}

static void a_badge_replaces_the_pause_icon_while_paused(void)
{
    NpTestLayout shape = NP_TEST_LAYOUTS[LANDSCAPE];
    NpTestScene scene = np_test_scene(NP_FACE_CASSETTE, shape, NP_TEST_PAUSED);
    scene.frame.feedback = NOW_PLAYING_FEEDBACK_PLAY;
    draw(scene, pixels);
    CHECK_EQUAL(LABEL, np_test_pixel(pixels, shape, 160, 87));
}

static int differing_rows(NpTestLayout layout, int *first, int *last)
{
    int rows = 0;
    *first = layout.height;
    *last = -1;
    for (int y = 0; y < layout.height; y++) {
        for (int x = 0; x < layout.width; x++) {
            if (np_test_pixel(pixels, layout, x, y) != np_test_pixel(other, layout, x, y)) {
                rows++;
                *first = y < *first ? y : *first;
                *last = y;
                break;
            }
        }
    }
    return rows;
}

static void the_teeth_are_the_only_thing_that_moves_between_ticks(void)
{
    for (int layout = LANDSCAPE; layout < NP_TEST_LAYOUT_COUNT; layout++) {
        NpTestLayout shape = NP_TEST_LAYOUTS[layout];
        int centre = layout == LANDSCAPE ? 87 : top_of(shape) + 47;
        for (int step = 0; step < STEPS; step++) {
            NpTestScene scene = np_test_scene(NP_FACE_CASSETTE, shape, NP_TEST_PLAYING);
            scene.frame.tick += (uint32_t)step;
            draw(scene, pixels);
            scene.frame.tick++;
            draw(scene, other);
            int first;
            int last;
            int rows = differing_rows(shape, &first, &last);
            CHECK(rows > 0);
            CHECK(rows <= BAND_ROWS);
            CHECK(first >= centre - (BAND_BOTTOM - BAND_TOP) / 2);
            CHECK(last <= centre + (BAND_BOTTOM - BAND_TOP) / 2);
        }
    }
}

static void the_teeth_come_back_after_a_whole_turn(void)
{
    NpTestScene scene =
        np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[LANDSCAPE], NP_TEST_PLAYING);
    draw(scene, pixels);
    scene.frame.tick += TURN_TICKS;
    draw(scene, other);
    CHECK(np_test_same(pixels, other, scene.layout));
}

static void a_playing_frame_ignores_the_clock_when_the_title_fits(void)
{
    for (int layout = LANDSCAPE; layout < NP_TEST_LAYOUT_COUNT; layout++) {
        NpTestScene scene =
            np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[layout], NP_TEST_PLAYING);
        scene.frame.song = "AB";
        scene.frame.artist = "AB";
        draw(scene, pixels);
        scene.frame.now_ms += NP_TEST_PAUSED_SKEW_MS;
        draw(scene, other);
        CHECK(np_test_same(pixels, other, scene.layout));
    }
}

static int tape_in(Rect half)
{
    return count_in(pixels, NP_TEST_LAYOUTS[LANDSCAPE], half, TAPE);
}

static void the_packs_move_from_the_left_reel_to_the_right_one(void)
{
    static const Rect LEFT = {67, 57, 90, 60};
    static const Rect RIGHT = {163, 57, 90, 60};
    int left = 1 << 20;
    int right = 0;
    for (int step = 0; step <= STEPS; step++) {
        NpTestScene scene =
            np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[LANDSCAPE], NP_TEST_PLAYING);
        scene.frame.shown_ms = scene.status.duration_ms / STEPS * (uint32_t)step;
        draw(scene, pixels);
        CHECK(tape_in(LEFT) < left);
        CHECK(tape_in(RIGHT) > right);
        left = tape_in(LEFT);
        right = tape_in(RIGHT);
    }
}

static void the_packs_move_across_the_whole_queue_with_each_track_an_equal_share(void)
{
    static const Rect LEFT = {67, 57, 90, 60};
    static const Rect RIGHT = {163, 57, 90, 60};
    NpTestScene scene =
        np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[LANDSCAPE], NP_TEST_PLAYING);
    scene.frame.shown_ms = scene.status.duration_ms;
    scene.status.place = (PlayQueuePlace){1, 4};
    draw(scene, pixels);
    int first_left = tape_in(LEFT);
    int first_right = tape_in(RIGHT);
    scene.frame.shown_ms = 0;
    scene.status.place = (PlayQueuePlace){2, 4};
    draw(scene, pixels);
    CHECK_EQUAL(first_left, tape_in(LEFT));
    CHECK_EQUAL(first_right, tape_in(RIGHT));
    scene.status.place = (PlayQueuePlace){4, 4};
    draw(scene, pixels);
    CHECK(tape_in(LEFT) < first_left);
    CHECK(tape_in(RIGHT) > first_right);
}

static void shuffled_songs_keep_the_tape_on_the_left_reel(void)
{
    NpTestScene scene =
        np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[LANDSCAPE], NP_TEST_PLAYING);
    scene.status.place = (PlayQueuePlace){1, 4};
    scene.frame.shown_ms = 0;
    draw(scene, pixels);
    int left = tape_in((Rect){67, 57, 90, 60});
    scene.status.shuffle = SHUFFLE_SONGS;
    scene.status.place = (PlayQueuePlace){4, 4};
    scene.frame.shown_ms = scene.status.duration_ms;
    draw(scene, pixels);
    CHECK_EQUAL(left, tape_in((Rect){67, 57, 90, 60}));
}

static void the_tape_fill_grows_with_progress(void)
{
    static const Rect TAPE_ROW = {87, 158, 145, 1};
    int amber = -1;
    for (int step = 0; step <= STEPS; step++) {
        NpTestScene scene =
            np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[LANDSCAPE], NP_TEST_PLAYING);
        scene.frame.shown_ms = scene.status.duration_ms / STEPS * (uint32_t)step;
        draw(scene, pixels);
        int now = count_in(pixels, scene.layout, TAPE_ROW, AMBER);
        CHECK(now > amber);
        amber = now;
    }
    CHECK_EQUAL(145, amber);
}

static void an_unknown_duration_keeps_the_left_pack_full_and_the_tape_empty(void)
{
    NpTestLayout shape = NP_TEST_LAYOUTS[LANDSCAPE];
    NpTestScene scene = np_test_scene(NP_FACE_CASSETTE, shape, NP_TEST_PLAYING);
    scene.status.duration_ms = 0;
    draw(scene, pixels);
    CHECK_EQUAL(TAPE, np_test_pixel(pixels, shape, 99 + 30, 87));
    CHECK_EQUAL(DEEP, np_test_pixel(pixels, shape, 219 + 20, 87));
    CHECK_EQUAL(TAPE, np_test_pixel(pixels, shape, 90, 158));
}

static void the_pack_edges_stay_on_whole_pixels_for_seconds(void)
{
    static const Rect WINDOW = {67, 57, 185, 60};
    NpTestScene scene =
        np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[LANDSCAPE], NP_TEST_PLAYING);
    draw(scene, pixels);
    for (int step = 1; step <= STEPS * 2; step++) {
        scene.frame.shown_ms += STEP_MS;
        draw(scene, other);
        for (int y = WINDOW.y; y < WINDOW.y + WINDOW.height; y++) {
            for (int x = WINDOW.x; x < WINDOW.x + WINDOW.width; x++) {
                CHECK_EQUAL(np_test_pixel(pixels, scene.layout, x, y),
                            np_test_pixel(other, scene.layout, x, y));
            }
        }
    }
}

static void the_cover_is_never_drawn(void)
{
    for (int layout = LANDSCAPE; layout < NP_TEST_LAYOUT_COUNT; layout++) {
        NpTestScene scene =
            np_test_scene(NP_FACE_CASSETTE, NP_TEST_LAYOUTS[layout], NP_TEST_PLAYING);
        draw(scene, pixels);
        CHECK_EQUAL(0, count_in(pixels, scene.layout, scene.layout.area, COVER_RED));
        scene.frame.cover = (Image){0};
        draw(scene, other);
        CHECK(np_test_same(pixels, other, scene.layout));
    }
}

static void the_letter_a_is_drawn_in_the_face_colours(void)
{
    NpTestLayout land = NP_TEST_LAYOUTS[LANDSCAPE];
    draw(np_test_scene(NP_FACE_CASSETTE, land, NP_TEST_PLAYING), pixels);
    CHECK(count_in(pixels, land, (Rect){25, 59, 40, 56}, LABEL) > 0);
    NpTestLayout port = NP_TEST_LAYOUTS[PORTRAIT];
    draw(np_test_scene(NP_FACE_CASSETTE, port, NP_TEST_PLAYING), pixels);
    CHECK(count_in(pixels, port, (Rect){25, top_of(port) + 6, 14, 20}, RED) > 0);
}

static void the_title_changes_only_the_pixels_inside_its_line(void)
{
    for (int layout = LANDSCAPE; layout <= PORTRAIT; layout++) {
        NpTestLayout shape = NP_TEST_LAYOUTS[layout];
        Rect line = layout == LANDSCAPE ? (Rect){33, 8, 253, 20}
                                        : (Rect){16, top_of(shape) + CARD_OFFSET + 3, 137, 20};
        NpTestScene scene = np_test_scene(NP_FACE_CASSETTE, shape, NP_TEST_PLAYING);
        scene.frame.song = "";
        draw(scene, pixels);
        scene.frame.song = NP_TEST_LONG_TITLE;
        draw(scene, other);
        for (int y = 0; y < shape.height; y++) {
            for (int x = 0; x < shape.width; x++) {
                bool inside = x >= line.x && x < line.x + line.width && y >= line.y &&
                              y < line.y + line.height;
                CHECK(inside ||
                      np_test_pixel(pixels, shape, x, y) == np_test_pixel(other, shape, x, y));
            }
        }
    }
}

static bool counter_same(NpTestLayout shape)
{
    int top = top_of(shape) + COUNTER_OFFSET;
    for (int y = top; y < top + 32; y++) {
        for (int x = 0; x < shape.width; x++) {
            if (np_test_pixel(pixels, shape, x, y) != np_test_pixel(other, shape, x, y)) {
                return false;
            }
        }
    }
    return true;
}

static void the_counter_holds_still_then_turns_its_last_drum(void)
{
    NpTestLayout shape = NP_TEST_LAYOUTS[PORTRAIT];
    NpTestScene scene = np_test_scene(NP_FACE_CASSETTE, shape, NP_TEST_PLAYING);
    scene.frame.shown_ms = 0;
    draw(scene, pixels);
    CHECK_EQUAL(SHELL, np_test_pixel(pixels, shape, 85, top_of(shape) + COUNTER_OFFSET + 2));
    scene.frame.shown_ms = 1000;
    draw(scene, other);
    CHECK(counter_same(shape));
    scene.frame.shown_ms = COUNTER_GAP_MS;
    draw(scene, other);
    CHECK(!counter_same(shape));
}

static void the_counter_slows_down_as_the_tape_builds_up(void)
{
    NpTestLayout shape = NP_TEST_LAYOUTS[PORTRAIT];
    NpTestScene scene = np_test_scene(NP_FACE_CASSETTE, shape, NP_TEST_PLAYING);
    scene.frame.shown_ms = 0;
    draw(scene, pixels);
    scene.frame.shown_ms = COUNTER_GAP_MS;
    draw(scene, other);
    CHECK(!counter_same(shape));
    scene.frame.shown_ms = TEN_MINUTES_MS;
    draw(scene, pixels);
    scene.frame.shown_ms = TEN_MINUTES_MS + COUNTER_GAP_MS;
    draw(scene, other);
    CHECK(counter_same(shape));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_reels_turn_counter_clockwise),
        TEST_CASE(the_backdrop_colour_is_at_the_corner_of_the_area),
        TEST_CASE(landscape_pixels_show_every_state),
        TEST_CASE(portrait_pixels_show_every_state_at_both_status_bar_heights),
        TEST_CASE(landscape_text_uses_the_ink_paired_with_its_background),
        TEST_CASE(portrait_text_uses_the_ink_paired_with_its_background),
        TEST_CASE(feedback_is_set_in_the_strong_ink),
        TEST_CASE(portrait_times_are_set_in_the_strong_ink),
        TEST_CASE(landscape_times_are_set_in_the_light_ink_on_the_shell),
        TEST_CASE(the_portrait_card_hangs_below_the_cassette),
        TEST_CASE(paused_portrait_shows_the_pause_icon_between_the_reels),
        TEST_CASE(a_badge_replaces_the_pause_icon_while_paused),
        TEST_CASE(the_teeth_are_the_only_thing_that_moves_between_ticks),
        TEST_CASE(the_teeth_come_back_after_a_whole_turn),
        TEST_CASE(a_playing_frame_ignores_the_clock_when_the_title_fits),
        TEST_CASE(the_packs_move_from_the_left_reel_to_the_right_one),
        TEST_CASE(the_packs_move_across_the_whole_queue_with_each_track_an_equal_share),
        TEST_CASE(shuffled_songs_keep_the_tape_on_the_left_reel),
        TEST_CASE(the_tape_fill_grows_with_progress),
        TEST_CASE(an_unknown_duration_keeps_the_left_pack_full_and_the_tape_empty),
        TEST_CASE(the_pack_edges_stay_on_whole_pixels_for_seconds),
        TEST_CASE(the_cover_is_never_drawn),
        TEST_CASE(the_letter_a_is_drawn_in_the_face_colours),
        TEST_CASE(the_title_changes_only_the_pixels_inside_its_line),
        TEST_CASE(the_counter_holds_still_then_turns_its_last_drum),
        TEST_CASE(the_counter_slows_down_as_the_tape_builds_up),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
