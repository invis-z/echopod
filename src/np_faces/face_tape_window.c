#include "core/format.h"
#include "core/resident.h"
#include "core/string_builder.h"
#include "face_text.h"
#include "faces.h"
#include "np_faces/face_shapes.h"
#include "ui/label.h"

enum {
    PAPER = COLOR_RGB(238, 236, 228),
    BAND = COLOR_RGB(112, 108, 100),
    RULE = COLOR_RGB(120, 120, 124),
    INK = COLOR_RGB(34, 34, 40),
    INK_SOFT = COLOR_RGB(96, 96, 104),
    SHELL = COLOR_RGB(24, 24, 26),
    SHELL_HI = COLOR_RGB(52, 52, 56),
    TAPE = COLOR_RGB(92, 58, 36),
    CLEAR = COLOR_RGB(200, 196, 186),
    HUB = COLOR_RGB(236, 236, 232),
    BORE = COLOR_RGB(52, 52, 56),
    SPINDLE = COLOR_RGB(20, 20, 22),
    SPINDLE_EDGE = COLOR_RGB(84, 84, 90),
    SPINDLE_TOP = COLOR_RGB(34, 34, 38),
    WIDTH = 320,
    HEIGHT = 170,
    STRIP_HEIGHT = 70,
    RULE_LEFT = 34,
    RULE_ROWS_FIRST = 21,
    RULE_ROWS_SECOND = 45,
    HUB_Y = 112,
    HUB_SPACING = 223,
    HUB_LEFT = (WIDTH - 1 - HUB_SPACING) / 2,
    HUB_REACH = 35,
    RIM_Q4 = 547,
    OPENING_Q4 = 464,
    BORE_Q4 = 434,
    SHAFT_Q4 = 294,
    SHAFT_EDGE_Q4 = 310,
    TOP_Q4 = 118,
    PACK_MIN_Q4 = 910,
    PACK_MAX_Q4 = 2149,
    TOOTH_FROM = 20,
    TOOTH_TO = 28,
    TOOTH_WIDTH_Q4 = 114,
    KEY_FROM = 13,
    KEY_TO = 26,
    KEY_WIDTH_Q4 = 126,
    EDGE_Q4 = 32,
    TEETH = 6,
    KEYS = 3,
    FINE_TURN = SHAPE_TURN * SHAPE_FINE_STEPS,
    FINE_QUARTER = FINE_TURN / 4,
    KEY_OFFSET = FINE_TURN / 12,
    HUB_TURN_MS = 3200,
    TEXT_LEFT = 39,
    TEXT_RIGHT = 316,
    TEXT_MIDDLE = 160,
    LETTER_RADIUS = 3,
    TEXT_BOXES = 5,
    BOXES_TURNED = 6,
    BADGE_RADIUS = 4,
    TILE_PIXELS = 1024,
    TEXT_CAPACITY = 96,
    COUNT_CAPACITY = 24,
    TIME_CAPACITY = 2 * FACE_TIME_CAPACITY + 4,
    WHOLE_PIXEL = ~(SHAPE_Q4 - 1)
};

typedef enum Turn {
    TURN_NONE,
    TURN_PORTRAIT,
    TURN_FLIPPED
} Turn;

typedef struct View {
    Canvas *canvas;
    Turn turn;
} View;

typedef struct Hub {
    Point pixel;
    Point centre;
} Hub;

RIPPL_NP_FACES_DATA static const Rect CUTOUT = {4, 70, 312, 86};
RIPPL_NP_FACES_DATA static const Rect WINDOW = {98, 80, 124, 66};
RIPPL_NP_FACES_DATA static const Rect BADGE_ICON = {151, 104, 18, 18};
RIPPL_NP_FACES_DATA static const Rect BOXES[BOXES_TURNED] = {
    {2, 4, 24, 24},
    {TEXT_LEFT, 2, TEXT_RIGHT - TEXT_LEFT, 19},
    {TEXT_LEFT, 25, TEXT_RIGHT - TEXT_LEFT, 19},
    {TEXT_LEFT, 49, TEXT_MIDDLE - TEXT_LEFT, 16},
    {TEXT_MIDDLE, 49, TEXT_RIGHT - TEXT_MIDDLE, 16},
    {148, 101, 24, 24},
};
RIPPL_NP_FACES_DATA static const char SIDE[] = "A";
RIPPL_NP_FACES_DATA static const char SEPARATOR[] = " \xC2\xB7 ";
RIPPL_NP_FACES_DATA static const char OF[] = " / ";

RIPPL_NP_FACES_CODE static bool present(const char *text)
{
    return text != NULL && text[0] != '\0';
}

RIPPL_NP_FACES_CODE static Rect turned_rect(Turn turn, Rect area)
{
    if (turn == TURN_PORTRAIT) {
        return (Rect){area.y, WIDTH - area.x - area.width, area.height, area.width};
    }
    if (turn == TURN_FLIPPED) {
        return (Rect){HEIGHT - area.y - area.height, area.x, area.height, area.width};
    }
    return area;
}

RIPPL_NP_FACES_CODE static Rect unturned_rect(Turn turn, Rect area)
{
    if (turn == TURN_PORTRAIT) {
        return (Rect){WIDTH - area.y - area.height, area.x, area.height, area.width};
    }
    if (turn == TURN_FLIPPED) {
        return (Rect){area.y, HEIGHT - area.x - area.width, area.height, area.width};
    }
    return area;
}

RIPPL_NP_FACES_CODE static Point turned_q4(Turn turn, Point point)
{
    if (turn == TURN_PORTRAIT) {
        return (Point){point.y, WIDTH * SHAPE_Q4 - point.x};
    }
    if (turn == TURN_FLIPPED) {
        return (Point){HEIGHT * SHAPE_Q4 - point.y, point.x};
    }
    return point;
}

RIPPL_NP_FACES_CODE static Point turned_pixel(Turn turn, Point point)
{
    if (turn == TURN_PORTRAIT) {
        return (Point){point.y, WIDTH - 1 - point.x};
    }
    if (turn == TURN_FLIPPED) {
        return (Point){HEIGHT - 1 - point.y, point.x};
    }
    return point;
}

RIPPL_NP_FACES_CODE static uint16_t turned_angle(Turn turn, uint16_t fine)
{
    if (turn == TURN_PORTRAIT) {
        return (uint16_t)(fine - FINE_QUARTER);
    }
    if (turn == TURN_FLIPPED) {
        return (uint16_t)(fine + FINE_QUARTER);
    }
    return fine;
}

RIPPL_NP_FACES_CODE static bool touches(const View *view, Rect area)
{
    Rect turned = turned_rect(view->turn, area);
    return !rect_is_empty(rect_intersection(view->canvas->clip, turned));
}

RIPPL_NP_FACES_CODE static void fill(const View *view, Rect area, Color color)
{
    canvas_fill_rect(view->canvas, turned_rect(view->turn, area), color);
}

RIPPL_NP_FACES_CODE static void ring(const View *view, Point centre, int outer_q4, int inner_q4,
                                     Color color)
{
    Point at = turned_q4(view->turn, centre);
    shape_ring(view->canvas,
               (ShapeRing){(int16_t)at.x, (int16_t)at.y, (int16_t)outer_q4, (int16_t)inner_q4},
               color);
}

RIPPL_NP_FACES_CODE static void disc(const View *view, Point centre, int radius_q4, Color color)
{
    ring(view, centre, radius_q4, 0, color);
}

RIPPL_NP_FACES_CODE static void bar(const View *view, ShapeSpoke spoke, uint16_t fine, Color color)
{
    spoke.pivot = turned_pixel(view->turn, spoke.pivot);
    shape_spoke_fine(view->canvas, &spoke, turned_angle(view->turn, fine), color);
}

RIPPL_NP_FACES_CODE static Hub hub_at(int index)
{
    int x = HUB_LEFT + index * HUB_SPACING;
    return (Hub){{x, HUB_Y}, {x * SHAPE_Q4 + SHAPE_Q4 / 2, HUB_Y * SHAPE_Q4 + SHAPE_Q4 / 2}};
}

RIPPL_NP_FACES_CODE static void draw_label(const View *view)
{
    fill(view, (Rect){0, 0, WIDTH, HEIGHT}, BAND);
    fill(view, (Rect){0, 0, WIDTH, STRIP_HEIGHT}, PAPER);
    fill(view, (Rect){RULE_LEFT, RULE_ROWS_FIRST, WIDTH - RULE_LEFT, 1}, RULE);
    fill(view, (Rect){RULE_LEFT, RULE_ROWS_SECOND, WIDTH - RULE_LEFT, 1}, RULE);
    shape_rounded(view->canvas, turned_rect(view->turn, CUTOUT), CUTOUT.height / 2, SHELL);
}

RIPPL_NP_FACES_CODE static void draw_tape(const View *view, const NowPlayingFrame *frame)
{
    if (!touches(view, WINDOW)) {
        return;
    }
    int high = PACK_MAX_Q4;
    int low = PACK_MIN_Q4;
    int played = face_queue_progress(frame, high * high - low * low);
    int packs[2] = {(int)shape_isqrt((uint32_t)(high * high - played)) & WHOLE_PIXEL,
                    (int)shape_isqrt((uint32_t)(low * low + played)) & WHOLE_PIXEL};
    Rect window = turned_rect(view->turn, WINDOW);
    Canvas clipped = canvas_clipped(view->canvas, window);
    View inside = {&clipped, view->turn};
    fill(&inside, WINDOW, CLEAR);
    for (int index = 0; index < 2; index++) {
        disc(&inside, hub_at(index).centre, packs[index], TAPE);
    }
    face_outline(view->canvas, window, SHELL_HI);
}

RIPPL_NP_FACES_CODE static void draw_spindle(const View *view, Hub hub, uint16_t angle)
{
    for (int index = 0; index < KEYS; index++) {
        uint16_t fine = (uint16_t)(angle + KEY_OFFSET + index * FINE_TURN / KEYS);
        bar(view, (ShapeSpoke){hub.pixel, 0, KEY_WIDTH_Q4 + EDGE_Q4, KEY_FROM - 1, KEY_TO + 1},
            fine, SPINDLE_EDGE);
        bar(view, (ShapeSpoke){hub.pixel, 0, KEY_WIDTH_Q4, KEY_FROM, KEY_TO}, fine, SPINDLE);
    }
    disc(view, hub.centre, SHAFT_EDGE_Q4, SPINDLE_EDGE);
    disc(view, hub.centre, SHAFT_Q4, SPINDLE);
    disc(view, hub.centre, TOP_Q4, SPINDLE_TOP);
}

RIPPL_NP_FACES_CODE static void draw_hub(const View *view, Hub hub, uint16_t angle)
{
    ring(view, hub.centre, RIM_Q4, RIM_Q4 - SHAPE_Q4, SHELL_HI);
    disc(view, hub.centre, OPENING_Q4, HUB);
    disc(view, hub.centre, BORE_Q4, BORE);
    draw_spindle(view, hub, angle);
    for (int index = 0; index < TEETH; index++) {
        uint16_t fine = (uint16_t)(angle + index * FINE_TURN / TEETH);
        bar(view, (ShapeSpoke){hub.pixel, 0, TOOTH_WIDTH_Q4, TOOTH_FROM, TOOTH_TO}, fine, HUB);
    }
}

RIPPL_NP_FACES_CODE static void draw_hubs(const View *view, const NowPlayingFrame *frame)
{
    uint32_t into = frame->status->position_ms % HUB_TURN_MS;
    uint16_t angle = (uint16_t)(0 - into * FINE_TURN / HUB_TURN_MS);
    for (int index = 0; index < 2; index++) {
        Hub hub = hub_at(index);
        Rect reach = {hub.pixel.x - HUB_REACH, hub.pixel.y - HUB_REACH, 2 * HUB_REACH + 1,
                      2 * HUB_REACH + 1};
        if (touches(view, reach)) {
            draw_hub(view, hub, angle);
        }
    }
}

RIPPL_NP_FACES_CODE static bool reaches(const Canvas *canvas, Rect area)
{
    return !rect_is_empty(rect_intersection(canvas->clip, area));
}

RIPPL_NP_FACES_CODE static void put(Canvas *canvas, TextStyle style, Rect area, const char *text,
                                    FaceAlign align)
{
    Rect fitted = face_fitted_area(style.font, area, text, align);
    label_draw(canvas, style, (Label){fitted, text});
}

RIPPL_NP_FACES_CODE static const char *byline(const NowPlayingFrame *frame, char *buffer)
{
    if (!present(frame->album)) {
        return frame->artist;
    }
    if (!present(frame->artist)) {
        return frame->album;
    }
    StringBuilder builder = string_builder_make(buffer, TEXT_CAPACITY);
    string_builder_append(&builder, frame->artist);
    string_builder_append(&builder, SEPARATOR);
    string_builder_append(&builder, frame->album);
    return buffer;
}

RIPPL_NP_FACES_CODE static void draw_place(Canvas *canvas, const NowPlayingFrame *frame,
                                           const Font *font)
{
    PlayQueuePlace place = frame->status->place;
    if (place.total == 0 || place.number == 0 || place.number > place.total) {
        return;
    }
    char text[COUNT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append_unsigned(&builder, place.number);
    string_builder_append_char(&builder, '/');
    string_builder_append_unsigned(&builder, place.total);
    put(canvas, (TextStyle){font, INK_SOFT}, BOXES[3], text, FACE_ALIGN_LEFT);
}

RIPPL_NP_FACES_CODE static void draw_time(Canvas *canvas, const NowPlayingFrame *frame,
                                          const Font *font)
{
    char elapsed[FACE_TIME_CAPACITY];
    char total[FACE_TIME_CAPACITY];
    char text[TIME_CAPACITY];
    format_duration(frame->shown_ms, elapsed, sizeof elapsed);
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append(&builder, elapsed);
    uint32_t duration = frame->status->duration_ms;
    if (duration != 0) {
        format_duration(duration, total, sizeof total);
        string_builder_append(&builder, OF);
        string_builder_append(&builder, total);
    }
    put(canvas, (TextStyle){font, INK_SOFT}, BOXES[4], text, FACE_ALIGN_RIGHT);
}

RIPPL_NP_FACES_CODE static void draw_text(Canvas *canvas, const NowPlayingFrame *frame)
{
    const ThemeFonts *fonts = &frame->theme->fonts;
    if (reaches(canvas, BOXES[0])) {
        shape_rounded(canvas, BOXES[0], LETTER_RADIUS, INK);
        put(canvas, (TextStyle){fonts->title, PAPER}, BOXES[0], SIDE, FACE_ALIGN_CENTRE);
    }
    if (reaches(canvas, BOXES[1])) {
        FaceLine title = {fonts->title, BOXES[1], INK, INK, FACE_ALIGN_LEFT};
        face_line(canvas, frame, &title, frame->song);
    }
    if (reaches(canvas, BOXES[2])) {
        char buffer[TEXT_CAPACITY];
        FaceLine info = {fonts->body, BOXES[2], INK, INK_SOFT, FACE_ALIGN_LEFT};
        face_feedback_line(canvas, frame, &info, byline(frame, buffer));
    }
    if (reaches(canvas, BOXES[3])) {
        draw_place(canvas, frame, fonts->small);
    }
    if (reaches(canvas, BOXES[4])) {
        draw_time(canvas, frame, fonts->small);
    }
}

RIPPL_NP_FACES_CODE static FaceIcon badge_of(const NowPlayingFrame *frame)
{
    FaceIcon icon = face_badge_kind(frame);
    return icon == FACE_ICON_NONE && !frame->playing ? FACE_ICON_PAUSE : icon;
}

RIPPL_NP_FACES_CODE static void draw_badge(Canvas *canvas, const NowPlayingFrame *frame)
{
    FaceIcon icon = badge_of(frame);
    if (icon == FACE_ICON_NONE || !reaches(canvas, BOXES[TEXT_BOXES])) {
        return;
    }
    shape_rounded(canvas, BOXES[TEXT_BOXES], BADGE_RADIUS, SHELL);
    face_icon_draw(canvas, icon, BADGE_ICON, HUB);
}

RIPPL_NP_FACES_CODE static void draw_shapes(const View *view, const NowPlayingFrame *frame)
{
    draw_label(view);
    draw_tape(view, frame);
    draw_hubs(view, frame);
}

RIPPL_NP_FACES_CODE static void draw_landscape(Canvas *canvas, const NowPlayingFrame *frame)
{
    View view = {canvas, TURN_NONE};
    draw_shapes(&view, frame);
    draw_text(canvas, frame);
    draw_badge(canvas, frame);
}

RIPPL_NP_FACES_CODE static void copy_turned(const View *view, const Canvas *tile)
{
    Canvas *canvas = view->canvas;
    Rect area = tile->bounds;
    for (int y = area.y; y < area.y + area.height; y++) {
        const Color *row = &tile->pixels[(y - area.y) * area.width];
        for (int x = area.x; x < area.x + area.width; x++) {
            Point at = turned_pixel(view->turn, (Point){x, y});
            canvas->pixels[(at.y - canvas->bounds.y) * canvas->bounds.width +
                           (at.x - canvas->bounds.x)] = row[x - area.x];
        }
    }
}

RIPPL_NP_FACES_CODE static void draw_turned_box(const View *view, const NowPlayingFrame *frame,
                                                Rect box)
{
    Rect shown = rect_intersection(view->canvas->clip, turned_rect(view->turn, box));
    if (rect_is_empty(shown)) {
        return;
    }
    Rect wanted = unturned_rect(view->turn, shown);
    int step = TILE_PIXELS / wanted.height;
    Color pixels[TILE_PIXELS];
    for (int x = wanted.x; x < wanted.x + wanted.width; x += step) {
        int columns = wanted.x + wanted.width - x;
        columns = columns < step ? columns : step;
        Canvas tile = canvas_make(pixels, (Rect){x, wanted.y, columns, wanted.height});
        draw_landscape(&tile, frame);
        copy_turned(view, &tile);
    }
}

RIPPL_NP_FACES_CODE void face_tape_window_draw(const NowPlayingFrame *frame, Canvas *canvas)
{
    if (frame->area.width > frame->area.height) {
        draw_landscape(canvas, frame);
        return;
    }
    View view = {canvas, frame->flipped ? TURN_FLIPPED : TURN_PORTRAIT};
    draw_shapes(&view, frame);
    int boxes = badge_of(frame) == FACE_ICON_NONE ? TEXT_BOXES : BOXES_TURNED;
    for (int index = 0; index < boxes; index++) {
        draw_turned_box(&view, frame, BOXES[index]);
    }
}
