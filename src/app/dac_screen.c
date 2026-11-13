#include "dac_screen.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "fonts/fonts.h"
#include "frame_overlay.h"
#include "gfx/text.h"
#include "hal/audio.h"
#include "ui/header.h"
#include "ui/label.h"

enum {
    HZ_PER_KHZ = 1000,
    STATS_LINE_HEIGHT_LANDSCAPE = 16,
    STATS_LINE_HEIGHT_PORTRAIT = 15,
    COUNT_THOUSANDS_FROM = 10000,
    COUNT_MILLIONS_FROM = 10000000,
    STATS_PADDING = 2,
    STATS_INSET = 6,
    VOLUME_BAR_HEIGHT = 3,
    VOLUME_BLOCK_HEIGHT = 22,
    VOLUME_TEXT_GAP = 2,
    VOLUME_TEXT_HEIGHT = 16,
    DIGIT_CAP_HEIGHT = 35,
    LINE_GAP = 6,
    MESSAGE_LINES = 3,
    MESSAGE_CAPACITY = 96,
    VOLUME_CAPACITY = 8,
    BITS_CAPACITY = 12,
    LINE_CAPACITY = 40
};

static const Color STATS_TEXT = COLOR_RGB(124, 252, 0);
static const char KHZ[] = "kHz";

void dac_screen_rate_text(uint32_t rate, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    string_builder_append_unsigned(&builder, rate / HZ_PER_KHZ);
    if (rate % HZ_PER_KHZ != 0) {
        string_builder_append_char(&builder, '.');
        string_builder_append_unsigned(&builder, rate % HZ_PER_KHZ / 100);
    }
}

static void append_count(StringBuilder *line, uint32_t value)
{
    if (value < COUNT_THOUSANDS_FROM) {
        string_builder_append_unsigned(line, value);
    } else if (value < COUNT_MILLIONS_FROM) {
        string_builder_append_unsigned(line, value / 1000u);
        string_builder_append_char(line, 'k');
    } else {
        string_builder_append_unsigned(line, value / 1000000u);
        string_builder_append_char(line, 'M');
    }
}

typedef struct StatsItem {
    const char *name;
    uint32_t value;
} StatsItem;

static int lines_of(const StatsItem *items, const uint8_t *breaks, int count,
                    char lines[DAC_SCREEN_STATS_LINES][DAC_SCREEN_STATS_CAPACITY])
{
    int item = 0;
    for (int line = 0; line < count; line++) {
        StringBuilder builder = string_builder_make(lines[line], DAC_SCREEN_STATS_CAPACITY);
        for (int index = 0; index < breaks[line]; index++, item++) {
            if (index > 0) {
                string_builder_append(&builder, "  ");
            }
            string_builder_append(&builder, items[item].name);
            string_builder_append_char(&builder, ' ');
            append_count(&builder, items[item].value);
        }
    }
    return count;
}

int dac_screen_stats_lines(const AppDacStats *stats, bool portrait,
                           char lines[DAC_SCREEN_STATS_LINES][DAC_SCREEN_STATS_CAPACITY])
{
    const StatsItem items[] = {
        {"missed", stats->missed},     {"dropped", stats->dropped},
        {"repeated", stats->repeated}, {"restarts", stats->restarts},
        {"starts", stats->starts},     {"stops", stats->stops},
        {"formats", stats->formats},   {"retries", stats->retries},
        {"control", stats->control},   {"wrong slot", stats->wrong_slot},
        {"late irq", stats->late},
    };
    static const uint8_t LANDSCAPE[] = {4, 4, 3};
    static const uint8_t PORTRAIT[] = {2, 2, 2, 2, 1, 1, 1};
    int count = portrait ? lines_of(items, PORTRAIT, sizeof PORTRAIT, lines)
                         : lines_of(items, LANDSCAPE, sizeof LANDSCAPE, lines);
    StringBuilder loss = string_builder_make(lines[count], DAC_SCREEN_STATS_CAPACITY);
    string_builder_append(&loss, "loss ");
    append_count(&loss, stats->loss_last);
    string_builder_append(&loss, " last, ");
    append_count(&loss, stats->loss_longest);
    string_builder_append(&loss, " longest");
    if (!portrait) {
        string_builder_append(&loss, ", ");
        append_count(&loss, stats->loss_gap_us);
        string_builder_append(&loss, " us gap");
    }
    return count + 1;
}

static int stats_line_height(const Theme *theme)
{
    return theme_is_portrait(theme) ? STATS_LINE_HEIGHT_PORTRAIT : STATS_LINE_HEIGHT_LANDSCAPE;
}

Rect dac_screen_stats_area(const Theme *theme, int lines)
{
    int height = lines * stats_line_height(theme) + 2 * STATS_PADDING;
    return (Rect){0, theme->metrics.screen_height - height, theme->metrics.screen_width, height};
}

static void draw_stats(Canvas *canvas, const Theme *theme, const AppDacStats *stats, Rect area)
{
    char lines[DAC_SCREEN_STATS_LINES][DAC_SCREEN_STATS_CAPACITY];
    int count = dac_screen_stats_lines(stats, theme_is_portrait(theme), lines);
    canvas_fill_rect(canvas, area, frame_overlay_background());
    TextStyle style = {.font = theme->fonts.small, .color = STATS_TEXT};
    int height = stats_line_height(theme);
    for (int index = 0; index < count; index++) {
        Rect row = {area.x + STATS_INSET, area.y + STATS_PADDING + index * height,
                    area.width - 2 * STATS_INSET, height};
        label_draw(canvas, style, (Label){.area = row, .text = lines[index]});
    }
}

static void draw_volume(Canvas *canvas, const Theme *theme, uint8_t volume, int bottom)
{
    int padding = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * padding;
    Rect bar = {padding, bottom - VOLUME_BLOCK_HEIGHT, width, VOLUME_BAR_HEIGHT};
    uint8_t level = volume < HAL_AUDIO_VOLUME_MAX ? volume : HAL_AUDIO_VOLUME_MAX;
    canvas_fill_rect(canvas, bar, theme->colors.divider);
    canvas_fill_rect(canvas, (Rect){bar.x, bar.y, width * level / HAL_AUDIO_VOLUME_MAX, bar.height},
                     theme->colors.selection_background);
    Rect row = {padding, bar.y + VOLUME_BAR_HEIGHT + VOLUME_TEXT_GAP, width, VOLUME_TEXT_HEIGHT};
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    label_draw(canvas, style, (Label){.area = row, .text = i18n_text(STR_MENU_VOLUME)});
    char number[VOLUME_CAPACITY];
    StringBuilder builder = string_builder_make(number, sizeof number);
    string_builder_append_unsigned(&builder, volume);
    int number_width = text_width(style.font, number);
    label_draw(canvas, style,
               (Label){.area = {row.x + row.width - number_width, row.y, number_width, row.height},
                       .text = number});
}

static void draw_centered(Canvas *canvas, TextStyle style, int width, int baseline,
                          const char *text)
{
    text_draw(canvas, style, (Point){(width - text_width(style.font, text)) / 2, baseline}, text);
}

static size_t word_end(const char *text, size_t from)
{
    while (text[from] != '\0' && text[from] != ' ') {
        from++;
    }
    return from;
}

static int wrap_message(const Font *font, const char *message, int width,
                        char lines[MESSAGE_LINES][MESSAGE_CAPACITY])
{
    int count = 0;
    const char *rest = message;
    while (*rest != '\0' && count < MESSAGE_LINES) {
        size_t taken = word_end(rest, 0);
        for (;;) {
            size_t next = rest[taken] == ' ' ? word_end(rest, taken + 1) : taken;
            if (next == taken || count == MESSAGE_LINES - 1) {
                taken = next == taken ? taken : word_end(rest, next);
                break;
            }
            char probe[MESSAGE_CAPACITY];
            size_t length = next < sizeof probe - 1 ? next : sizeof probe - 1;
            for (size_t index = 0; index < length; index++) {
                probe[index] = rest[index];
            }
            probe[length] = '\0';
            if (text_width(font, probe) > width) {
                break;
            }
            taken = next;
        }
        if (count == MESSAGE_LINES - 1) {
            while (rest[taken] != '\0') {
                taken++;
            }
        }
        size_t length = taken < MESSAGE_CAPACITY - 1 ? taken : MESSAGE_CAPACITY - 1;
        for (size_t index = 0; index < length; index++) {
            lines[count][index] = rest[index];
        }
        lines[count][length] = '\0';
        count++;
        rest += taken;
        while (*rest == ' ') {
            rest++;
        }
    }
    return count;
}

static void draw_message(Canvas *canvas, const Theme *theme, const char *message, Rect area)
{
    char lines[MESSAGE_LINES][MESSAGE_CAPACITY];
    int padding = theme->metrics.padding;
    TextStyle style = {.font = theme->fonts.body, .color = theme->colors.text};
    int count = wrap_message(style.font, message, area.width - 2 * padding, lines);
    int height = font_line_height(style.font);
    int top = area.y + (area.height - count * height) / 2;
    for (int index = 0; index < count; index++) {
        Rect row = {area.x + padding, top + index * height, area.width - 2 * padding, height};
        label_draw(canvas, style,
                   (Label){.area = label_centered_area(style.font, row, lines[index]),
                           .text = lines[index]});
    }
}

static void bits_text(uint8_t bits, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    string_builder_append_unsigned(&builder, bits);
    string_builder_append(&builder, "-bit");
}

static void draw_compact_format(Canvas *canvas, const Theme *theme, const AppDacView *view,
                                Rect area)
{
    char line[LINE_CAPACITY];
    StringBuilder builder = string_builder_make(line, sizeof line);
    char rate[DAC_SCREEN_RATE_CAPACITY];
    char bits[BITS_CAPACITY];
    dac_screen_rate_text(view->rate, rate, sizeof rate);
    bits_text(view->bits, bits, sizeof bits);
    string_builder_append(&builder, rate);
    string_builder_append_char(&builder, ' ');
    string_builder_append(&builder, KHZ);
    string_builder_append(&builder, "  ");
    string_builder_append(&builder, bits);
    TextStyle style = {.font = theme->fonts.title, .color = theme->colors.text};
    label_draw(canvas, style,
               (Label){.area = label_centered_area(style.font, area, line), .text = line});
}

static void draw_format(Canvas *canvas, const Theme *theme, const AppDacView *view, Rect area)
{
    char rate[DAC_SCREEN_RATE_CAPACITY];
    char bits[BITS_CAPACITY];
    dac_screen_rate_text(view->rate, rate, sizeof rate);
    bits_text(view->bits, bits, sizeof bits);
    TextStyle digits = {.font = &FONT_INTER_SEMIBOLD_48, .color = theme->colors.text};
    TextStyle unit = {.font = theme->fonts.title, .color = theme->colors.text_secondary};
    TextStyle depth = {.font = theme->fonts.title, .color = theme->colors.selection_top};
    int title_height = font_line_height(theme->fonts.title);
    int width = theme->metrics.screen_width;
    if (theme_is_portrait(theme)) {
        int block = DIGIT_CAP_HEIGHT + 2 * (LINE_GAP + title_height);
        int baseline = area.y + (area.height - block) / 2 + DIGIT_CAP_HEIGHT;
        draw_centered(canvas, digits, width, baseline, rate);
        int unit_baseline = baseline + LINE_GAP + title_height;
        draw_centered(canvas, unit, width, unit_baseline, KHZ);
        draw_centered(canvas, depth, width, unit_baseline + LINE_GAP + title_height, bits);
        return;
    }
    int block = DIGIT_CAP_HEIGHT + LINE_GAP + title_height;
    int baseline = area.y + (area.height - block) / 2 + DIGIT_CAP_HEIGHT;
    int rate_width = text_width(digits.font, rate);
    int unit_width = text_width(unit.font, KHZ) + LINE_GAP;
    int left = (width - rate_width - unit_width) / 2;
    int after = text_draw(canvas, digits, (Point){left, baseline}, rate);
    text_draw(canvas, unit, (Point){after + LINE_GAP, baseline}, KHZ);
    draw_centered(canvas, depth, width, baseline + LINE_GAP + title_height, bits);
}

void dac_screen_draw(Canvas *canvas, const Theme *theme, const AppDacView *view)
{
    Rect content = theme_content_area(theme);
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, view->title);
    int bottom = theme->metrics.screen_height;
    if (view->stats != NULL) {
        char lines[DAC_SCREEN_STATS_LINES][DAC_SCREEN_STATS_CAPACITY];
        Rect stats = dac_screen_stats_area(
            theme, dac_screen_stats_lines(view->stats, theme_is_portrait(theme), lines));
        draw_stats(canvas, theme, view->stats, stats);
        bottom = stats.y;
    }
    draw_volume(canvas, theme, view->volume, bottom);
    Rect area = {content.x, content.y, content.width,
                 bottom - VOLUME_BLOCK_HEIGHT - LINE_GAP - content.y};
    if (view->message != NULL) {
        draw_message(canvas, theme, view->message, area);
    } else if (view->stats != NULL && !theme_is_portrait(theme)) {
        draw_compact_format(canvas, theme, view, area);
    } else {
        draw_format(canvas, theme, view, area);
    }
}
