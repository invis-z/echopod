#include "check.h"
#include "np_faces_test.h"

enum {
    PAPER = COLOR_RGB(238, 236, 228),
    BAND = COLOR_RGB(112, 108, 100),
    RULE = COLOR_RGB(120, 120, 124),
    SHELL = COLOR_RGB(24, 24, 26),
    TAPE = COLOR_RGB(92, 58, 36),
    CLEAR = COLOR_RGB(200, 196, 186),
    HUB = COLOR_RGB(236, 236, 232),
    SPINDLE = COLOR_RGB(20, 20, 22),
    SPINDLE_TOP = COLOR_RGB(34, 34, 38),
    LANDSCAPE = 0,
    PORTRAIT = 1,
    WIDTH = 320,
    HEIGHT = 170,
    LABEL_ROWS = 70,
    EDGE_SHARE = 25,
    HUB_TURN_MS = 3200
};

static Color pixels[NP_TEST_PIXELS];
static Color other[NP_TEST_PIXELS];

static NpTestScene scene_of(int layout, NpTestState state)
{
    return np_test_scene(NP_FACE_TAPE_WINDOW, NP_TEST_LAYOUTS[layout], state);
}

static Color at(int x, int y)
{
    return np_test_pixel(pixels, NP_TEST_LAYOUTS[LANDSCAPE], x, y);
}

static int count_in(const Color *image, Rect area, Color color)
{
    int count = 0;
    for (int y = area.y; y < area.y + area.height; y++) {
        for (int x = area.x; x < area.x + area.width; x++) {
            count += np_test_pixel(image, NP_TEST_LAYOUTS[LANDSCAPE], x, y) == color;
        }
    }
    return count;
}

static int written_in(const Color *image, Rect area)
{
    int count = 0;
    for (int y = area.y; y < area.y + area.height; y++) {
        for (int x = area.x; x < area.x + area.width; x++) {
            Color color = np_test_pixel(image, NP_TEST_LAYOUTS[LANDSCAPE], x, y);
            count += color != PAPER && color != RULE;
        }
    }
    return count;
}

static void the_face_paints_the_whole_screen_in_both_layouts(void)
{
    for (int layout = 0; layout < NP_TEST_LAYOUT_COUNT; layout++) {
        NpTestScene scene = scene_of(layout, NP_TEST_PLAYING);
        np_test_draw(&scene, pixels);
        CHECK(np_test_area_painted(pixels, scene.layout));
    }
}

static void strips_draw_the_same_picture_as_one_pass(void)
{
    for (int layout = 0; layout < NP_TEST_LAYOUT_COUNT; layout++) {
        for (int flipped = 0; flipped < 2; flipped++) {
            NpTestScene scene = scene_of(layout, NP_TEST_PLAYING);
            scene.frame.flipped = flipped != 0;
            np_test_draw(&scene, pixels);
            np_test_draw_strips(&scene, other);
            CHECK(np_test_same(pixels, other, scene.layout));
        }
    }
}

static void portrait_shows_the_landscape_picture_turned_with_the_player(void)
{
    NpTestScene land = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    np_test_draw(&land, pixels);
    for (int flipped = 0; flipped < 2; flipped++) {
        NpTestScene port = scene_of(PORTRAIT, NP_TEST_PLAYING);
        port.frame.flipped = flipped != 0;
        np_test_draw(&port, other);
        int label = 0;
        int edges = 0;
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                int px = flipped ? HEIGHT - 1 - y : y;
                int py = flipped ? x : WIDTH - 1 - x;
                bool different = np_test_pixel(other, port.layout, px, py) != at(x, y);
                label += different && y < LABEL_ROWS;
                edges += different;
            }
        }
        CHECK_EQUAL(0, label);
        CHECK(edges < WIDTH * HEIGHT / EDGE_SHARE);
        CHECK_EQUAL(at(160, 30),
                    np_test_pixel(other, port.layout, flipped ? 139 : 30, flipped ? 160 : 159));
    }
}

static void the_label_strip_is_paper_with_two_rules_over_a_grey_band(void)
{
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    np_test_draw(&scene, pixels);
    CHECK_EQUAL(PAPER, at(318, 10));
    CHECK_EQUAL(RULE, at(318, 21));
    CHECK_EQUAL(RULE, at(318, 45));
    CHECK_EQUAL(PAPER, at(318, 60));
    CHECK_EQUAL(BAND, at(1, 160));
    CHECK_EQUAL(BAND, at(318, 160));
    CHECK_EQUAL(SHELL, at(160, 75));
    CHECK_EQUAL(SHELL, at(160, 152));
}

static void the_window_shows_the_tape_between_the_hubs(void)
{
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    np_test_draw(&scene, pixels);
    CHECK_EQUAL(TAPE, at(100, 113));
    CHECK_EQUAL(CLEAR, at(155, 113));
    CHECK_EQUAL(TAPE, at(219, 113));
}

static void the_hubs_show_the_teeth_and_the_spindle(void)
{
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    np_test_draw(&scene, pixels);
    for (int hub = 0; hub < 2; hub++) {
        int x = hub == 0 ? 48 : 271;
        CHECK_EQUAL(SPINDLE_TOP, at(x, 112));
        CHECK(count_in(pixels, (Rect){x - 29, 83, 59, 59}, HUB) > 0);
        CHECK(count_in(pixels, (Rect){x - 29, 83, 59, 59}, SPINDLE) > 0);
    }
}

static int tape_left(void)
{
    return count_in(pixels, (Rect){99, 81, 61, 64}, TAPE);
}

static int tape_right(void)
{
    return count_in(pixels, (Rect){160, 81, 61, 64}, TAPE);
}

static void the_tape_winds_across_the_whole_queue(void)
{
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    scene.status.place = (PlayQueuePlace){1, 4};
    scene.frame.shown_ms = 0;
    np_test_draw(&scene, pixels);
    int left = tape_left();
    int right = tape_right();
    scene.status.place = (PlayQueuePlace){4, 4};
    scene.frame.shown_ms = scene.status.duration_ms;
    np_test_draw(&scene, pixels);
    CHECK(tape_left() < left);
    CHECK(tape_right() > right);
}

static void shuffled_songs_leave_the_tape_on_the_left_reel(void)
{
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    scene.status.place = (PlayQueuePlace){1, 4};
    scene.frame.shown_ms = 0;
    np_test_draw(&scene, pixels);
    int left = tape_left();
    scene.status.shuffle = SHUFFLE_SONGS;
    scene.status.place = (PlayQueuePlace){4, 4};
    scene.frame.shown_ms = scene.status.duration_ms;
    np_test_draw(&scene, pixels);
    CHECK_EQUAL(left, tape_left());
}

static void the_hubs_turn_with_the_play_position(void)
{
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    np_test_draw(&scene, pixels);
    scene.status.position_ms += NP_FACE_SMOOTH_TICK_MS;
    np_test_draw(&scene, other);
    CHECK(count_in(pixels, (Rect){19, 83, 59, 59}, HUB) > 0);
    CHECK(!np_test_same(pixels, other, scene.layout));
}

static bool hubs_match(void)
{
    static const Rect HUBS = {13, 77, 294, 71};
    for (int y = HUBS.y; y < HUBS.y + HUBS.height; y++) {
        for (int x = HUBS.x; x < HUBS.x + HUBS.width; x++) {
            bool in_window = x >= 98 && x < 222;
            if (!in_window && np_test_pixel(pixels, NP_TEST_LAYOUTS[LANDSCAPE], x, y) !=
                                  np_test_pixel(other, NP_TEST_LAYOUTS[LANDSCAPE], x, y)) {
                return false;
            }
        }
    }
    return true;
}

static void the_hubs_follow_the_play_position_and_come_round_every_turn(void)
{
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    np_test_draw(&scene, pixels);
    scene.frame.tick += 7;
    np_test_draw(&scene, other);
    CHECK(hubs_match());
    scene.status.position_ms += HUB_TURN_MS;
    np_test_draw(&scene, other);
    CHECK(hubs_match());
    scene.status.position_ms += HUB_TURN_MS / 4;
    np_test_draw(&scene, other);
    CHECK(!hubs_match());
}

static void a_paused_face_stays_still_as_time_passes(void)
{
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PAUSED);
    np_test_draw(&scene, pixels);
    scene.frame.now_ms += NP_TEST_PAUSED_SKEW_MS;
    np_test_draw(&scene, other);
    CHECK(np_test_same(pixels, other, scene.layout));
}

static void the_track_number_shows_on_the_left_of_the_time(void)
{
    static const Rect NUMBER = {39, 49, 100, 16};
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    np_test_draw(&scene, pixels);
    CHECK_EQUAL(0, written_in(pixels, NUMBER));
    CHECK(written_in(pixels, (Rect){200, 49, 116, 16}) > 0);
    scene.status.place = (PlayQueuePlace){7, 10};
    np_test_draw(&scene, pixels);
    CHECK(written_in(pixels, NUMBER) > 0);
}

static void the_title_is_written_on_the_first_rule(void)
{
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    np_test_draw(&scene, pixels);
    CHECK(written_in(pixels, (Rect){39, 2, 277, 19}) > 0);
    scene.frame.song = "";
    np_test_draw(&scene, pixels);
    CHECK_EQUAL(0, written_in(pixels, (Rect){39, 2, 277, 19}));
}

static void the_volume_takes_the_place_of_the_artist_line(void)
{
    static const Rect LINE = {39, 25, 277, 19};
    NpTestScene scene = scene_of(LANDSCAPE, NP_TEST_PAUSED);
    np_test_draw(&scene, pixels);
    NpTestScene volume = scene_of(LANDSCAPE, NP_TEST_VOLUME);
    np_test_draw(&volume, other);
    CHECK(!np_test_same(pixels, other, scene.layout));
    CHECK(written_in(other, LINE) != written_in(pixels, LINE));
}

static void paused_and_skipping_show_a_badge_in_the_window(void)
{
    static const Rect BADGE = {148, 101, 24, 24};
    NpTestScene playing = scene_of(LANDSCAPE, NP_TEST_PLAYING);
    np_test_draw(&playing, pixels);
    CHECK(count_in(pixels, BADGE, SHELL) < BADGE.width * BADGE.height / 4);
    static const NpTestState STATES[2] = {NP_TEST_PAUSED, NP_TEST_BADGE};
    for (int index = 0; index < 2; index++) {
        NpTestScene scene = scene_of(LANDSCAPE, STATES[index]);
        np_test_draw(&scene, pixels);
        CHECK(count_in(pixels, BADGE, SHELL) > BADGE.width * BADGE.height / 4);
        CHECK(count_in(pixels, BADGE, HUB) > 0);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_face_paints_the_whole_screen_in_both_layouts),
        TEST_CASE(strips_draw_the_same_picture_as_one_pass),
        TEST_CASE(portrait_shows_the_landscape_picture_turned_with_the_player),
        TEST_CASE(the_label_strip_is_paper_with_two_rules_over_a_grey_band),
        TEST_CASE(the_window_shows_the_tape_between_the_hubs),
        TEST_CASE(the_hubs_show_the_teeth_and_the_spindle),
        TEST_CASE(the_tape_winds_across_the_whole_queue),
        TEST_CASE(shuffled_songs_leave_the_tape_on_the_left_reel),
        TEST_CASE(the_hubs_turn_with_the_play_position),
        TEST_CASE(the_hubs_follow_the_play_position_and_come_round_every_turn),
        TEST_CASE(a_paused_face_stays_still_as_time_passes),
        TEST_CASE(the_track_number_shows_on_the_left_of_the_time),
        TEST_CASE(the_title_is_written_on_the_first_rule),
        TEST_CASE(the_volume_takes_the_place_of_the_artist_line),
        TEST_CASE(paused_and_skipping_show_a_badge_in_the_window),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
