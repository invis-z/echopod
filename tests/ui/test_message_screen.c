#include "check.h"
#include "gfx/text.h"
#include "ui/message_screen.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

static MessageScreen message;

static Screen *open_message(void)
{
    MessageConfig config = {.title = "Note", .message = "AB", .theme = &TEST_THEME};
    return message_screen_init(&message, config);
}

static ScreenAction press(Screen *screen, Key key, KeyGesture gesture)
{
    return screen->handle_key(screen->context, (KeyEvent){key, gesture}, 100);
}

static void the_message_title_is_the_screen_title(void)
{
    Screen *screen = open_message();
    CHECK(screen->title == message.config.title);
}

static void back_pops_and_a_long_back_goes_home(void)
{
    Screen *screen = open_message();
    CHECK_EQUAL(SCREEN_ACTION_POP, press(screen, KEY_BACK, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(SCREEN_ACTION_POP_TO_ROOT, press(screen, KEY_BACK, KEY_GESTURE_LONG_PRESS).type);
}

static void other_keys_do_nothing(void)
{
    Screen *screen = open_message();
    CHECK_EQUAL(SCREEN_ACTION_NONE, press(screen, KEY_PLAY, KEY_GESTURE_PRESS).type);
    CHECK_EQUAL(SCREEN_ACTION_NONE, press(screen, KEY_VOLUME_UP, KEY_GESTURE_PRESS).type);
}

static void the_message_is_drawn_below_the_header(void)
{
    Screen *screen = open_message();
    Canvas canvas = ui_test_canvas();
    screen->draw(screen->context, &canvas);
    Rect content = theme_content_area(&TEST_THEME);
    CHECK(ui_test_area_has_color(&canvas, content, TEST_THEME.colors.text_secondary));
    CHECK(ui_test_area_has_color(&canvas, theme_header_area(&TEST_THEME),
                                 TEST_THEME.colors.header_background));
}

static void a_message_too_wide_for_the_screen_wraps_onto_more_lines(void)
{
    MessageConfig config = {
        .title = "Note", .message = "AB AB AB AB AB AB AB AB AB AB AB AB", .theme = &TEST_THEME};
    Screen *screen = message_screen_init(&message, config);
    Canvas canvas = ui_test_canvas();
    screen->draw(screen->context, &canvas);
    Rect content = theme_content_area(&TEST_THEME);
    int first = -1;
    int last = -1;
    for (int y = content.y; y < content.y + content.height; y++) {
        Rect row = {content.x, y, content.width, 1};
        if (ui_test_area_has_color(&canvas, row, TEST_THEME.colors.text_secondary)) {
            first = first < 0 ? y : first;
            last = y;
        }
    }
    CHECK(first >= 0);
    CHECK(last - first >= font_line_height(TEST_THEME.fonts.body));
}

enum {
    STRIP_ROWS = 7,
    SCREEN_PIXELS = TEST_SCREEN_WIDTH * TEST_SCREEN_HEIGHT
};

static const char LONG_MESSAGE[] = "AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB";
static Color whole[SCREEN_PIXELS];
static Color strips[SCREEN_PIXELS];

static void draw_whole(Screen *screen, Color *copy)
{
    Canvas canvas = ui_test_canvas();
    screen->draw(screen->context, &canvas);
    for (int index = 0; index < SCREEN_PIXELS; index++) {
        copy[index] = canvas.pixels[index];
    }
}

static bool same_pixels(void)
{
    for (int index = 0; index < SCREEN_PIXELS; index++) {
        if (whole[index] != strips[index]) {
            return false;
        }
    }
    return true;
}

static void a_message_drawn_in_strips_matches_one_drawn_whole(void)
{
    MessageConfig config = {.title = "Note", .message = LONG_MESSAGE, .theme = &TEST_THEME};
    Screen *screen = message_screen_init(&message, config);
    draw_whole(screen, whole);
    for (int top = 0; top < TEST_SCREEN_HEIGHT; top += STRIP_ROWS) {
        int rows = TEST_SCREEN_HEIGHT - top < STRIP_ROWS ? TEST_SCREEN_HEIGHT - top : STRIP_ROWS;
        Canvas strip =
            canvas_make(strips + top * TEST_SCREEN_WIDTH, (Rect){0, top, TEST_SCREEN_WIDTH, rows});
        canvas_clear(&strip, 0x0821);
        screen->draw(screen->context, &strip);
    }
    CHECK(same_pixels());
}

static void a_new_message_is_wrapped_again(void)
{
    MessageConfig short_config = {.title = "Note", .message = "AB", .theme = &TEST_THEME};
    MessageConfig long_config = {.title = "Note", .message = LONG_MESSAGE, .theme = &TEST_THEME};
    draw_whole(message_screen_init(&message, short_config), whole);
    draw_whole(message_screen_init(&message, long_config), strips);
    draw_whole(message_screen_init(&message, short_config), strips);
    CHECK(same_pixels());
}

static void draw_message(const char *text)
{
    MessageConfig config = {.title = "Note", .message = text, .theme = &TEST_THEME};
    Screen *screen = message_screen_init(&message, config);
    Canvas canvas = ui_test_canvas();
    screen->draw(screen->context, &canvas);
}

static void a_line_break_puts_what_follows_on_its_own_line(void)
{
    draw_message("Don't unplug. \n45%");
    CHECK_EQUAL(2, message.layout.count);
    CHECK_EQUAL(13, message.layout.length[0]);
    CHECK_EQUAL(15, message.layout.start[1]);
    CHECK_EQUAL(3, message.layout.length[1]);
}

static void a_word_wider_than_the_screen_breaks_between_characters(void)
{
    static char text[128];
    Rect content = theme_content_area(&TEST_THEME);
    int width = content.width - 2 * TEST_THEME.metrics.padding;
    size_t used = 0;
    while (text_width(TEST_THEME.fonts.body, text) <= width + width / 2) {
        text[used++] = 'A';
        text[used++] = 'B';
    }
    text[used++] = '\n';
    text[used++] = '4';
    text[used++] = '5';
    text[used] = '%';
    draw_message(text);
    CHECK_EQUAL(3, message.layout.count);
    for (int index = 0; index < message.layout.count; index++) {
        char line[128] = {0};
        for (int at = 0; at < message.layout.length[index]; at++) {
            line[at] = message.config.message[message.layout.start[index] + at];
        }
        CHECK(text_width(TEST_THEME.fonts.body, line) <= width);
    }
    CHECK_EQUAL(message.layout.start[0] + message.layout.length[0], message.layout.start[1]);
    CHECK_EQUAL(3, message.layout.length[2]);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_message_title_is_the_screen_title),
        TEST_CASE(back_pops_and_a_long_back_goes_home),
        TEST_CASE(other_keys_do_nothing),
        TEST_CASE(the_message_is_drawn_below_the_header),
        TEST_CASE(a_message_too_wide_for_the_screen_wraps_onto_more_lines),
        TEST_CASE(a_message_drawn_in_strips_matches_one_drawn_whole),
        TEST_CASE(a_new_message_is_wrapped_again),
        TEST_CASE(a_line_break_puts_what_follows_on_its_own_line),
        TEST_CASE(a_word_wider_than_the_screen_breaks_between_characters),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
