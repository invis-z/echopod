#include "core/resident.h"
#include "face_text.h"
#include "faces.h"
#include "np_faces/face_shapes.h"
#include "ui/label.h"
#include "ui/rippl_logo.h"

#define WALNUT_TOP COLOR_RGB(72, 44, 24)
#define WALNUT_BOTTOM COLOR_RGB(40, 24, 16)
#define VINYL COLOR_RGB(16, 16, 24)
#define ALU COLOR_RGB(200, 200, 192)
#define ALU_DARK COLOR_RGB(104, 104, 96)
#define CREAM COLOR_RGB(248, 236, 208)
#define TAN COLOR_RGB(216, 184, 136)
#define TAN_DIM COLOR_RGB(192, 160, 120)
#define AMBER COLOR_RGB(248, 164, 40)
#define LABEL_RED COLOR_RGB(176, 36, 32)

enum {
    Q4 = SHAPE_Q4,
    CENTRE_X = 77,
    CENTRE_DROP = 73,
    ARM_DX = 75,
    ARM_DY = -47,
    LAMP_DX = 80,
    LAMP_DY = 59,
    RIM_OUTER = 67,
    RIM_INNER = 64,
    LABEL_RADIUS = 28,
    LABEL_MASK = 40,
    FRACTION_BITS = 8,
    FRACTION_ONE = 1 << FRACTION_BITS,
    FRACTION_MASK = FRACTION_ONE - 1,
    GROOVE_FIRST = 45,
    GROOVE_STEP = 7,
    GROOVE_COUNT = 3,
    TURN_STEPS = 18,
    SPINDLE = 48,
    BADGE_DISC = 320,
    BADGE_SIDE = 18,
    ARM_REST = 140,
    ARM_STEPS = 12,
    ARM_REACH = 84,
    ARM_HEAD_TURN = 10,
    HALF_TURN = 128,
    LINE_BIG = 22,
    LINE_SMALL = 16,
    BAR_HEIGHT = 3,
    PIXEL_CENTRE = 8,
    LAMP_RADIUS = 48
};

typedef struct Deck {
    Canvas *canvas;
    const NowPlayingFrame *frame;
    Point centre;
} Deck;

typedef struct Rows {
    int x;
    int width;
    FaceAlign align;
    int title;
    int artist;
    int album;
    int format;
    int detail;
    int modes;
    int bar;
    int times;
    bool has_detail;
} Rows;

typedef struct Slot {
    const Font *font;
    int row;
    int height;
    Color color;
} Slot;

RIPPL_NP_FACES_DATA static const Rows LANDSCAPE_ROWS = {
    170, 140, FACE_ALIGN_LEFT, 4, 26, 46, 73, 92, 107, 124, 129, true};
RIPPL_NP_FACES_DATA static const Rows PORTRAIT_ROWS = {
    10, 150, FACE_ALIGN_CENTRE, 144, 166, 188, 212, 0, -42, -23, -18, false};

RIPPL_NP_FACES_CODE static ShapeRing ring_of(Point at, int outer, int inner)
{
    return (ShapeRing){(int16_t)(at.x * Q4 + PIXEL_CENTRE), (int16_t)(at.y * Q4 + PIXEL_CENTRE),
                       (int16_t)outer, (int16_t)inner};
}

RIPPL_NP_FACES_CODE static void ring(const Deck *deck, int outer, int inner, Color color)
{
    shape_ring(deck->canvas, ring_of(deck->centre, outer, inner), color);
}

RIPPL_NP_FACES_CODE static void dot(Canvas *canvas, Point at, int radius, Color color)
{
    shape_ring(canvas, ring_of(at, radius, 0), color);
}

RIPPL_NP_FACES_CODE static void spoke(Canvas *canvas, ShapeSpoke geometry, Color color)
{
    shape_spoke(canvas, &geometry, color);
}

RIPPL_NP_FACES_CODE static void plate(const Deck *deck)
{
    canvas_fill_gradient(deck->canvas, deck->frame->area, WALNUT_TOP, WALNUT_BOTTOM);
    ring(deck, RIM_OUTER * Q4, RIM_INNER * Q4, ALU);
    ring(deck, RIM_INNER * Q4, 0, VINYL);
}

RIPPL_NP_FACES_CODE static uint8_t label_angle(const Deck *deck)
{
    return (uint8_t)(deck->frame->tick % TURN_STEPS * SHAPE_TURN / TURN_STEPS);
}

RIPPL_NP_FACES_CODE static int clamped(int value, int limit)
{
    return value < 0 ? 0 : value >= limit ? limit - 1 : value;
}

RIPPL_NP_FACES_CODE static void cover_pixel(Canvas *canvas, Point at, Image cover, Point from)
{
    Rect clip = canvas->clip;
    if (at.x >= clip.x && at.x < clip.x + clip.width && at.y >= clip.y &&
        at.y < clip.y + clip.height) {
        int offset = (at.y - canvas->bounds.y) * canvas->bounds.width + at.x - canvas->bounds.x;
        canvas->pixels[offset] =
            cover
                .pixels[clamped(from.y, cover.height) * cover.width + clamped(from.x, cover.width)];
    }
}

RIPPL_NP_FACES_CODE static void turned_cover(const Deck *deck, Image cover, uint8_t angle)
{
    int sine = shape_sine(angle);
    int cosine = shape_cosine(angle);
    int span = 2 * LABEL_RADIUS;
    int reach = span * SHAPE_UNIT;
    for (int row = 0; row < span; row++) {
        int dy = 2 * row + 1 - span;
        for (int column = 0; column < span; column++) {
            int dx = 2 * column + 1 - span;
            if (dx * dx + dy * dy <= span * span) {
                int u = dx * cosine + dy * sine + reach;
                int v = dy * cosine - dx * sine + reach;
                Point at = {deck->centre.x - LABEL_RADIUS + column,
                            deck->centre.y - LABEL_RADIUS + row};
                cover_pixel(deck->canvas, at, cover,
                            (Point){u * cover.width / (2 * reach), v * cover.height / (2 * reach)});
            }
        }
    }
    ring(deck, LABEL_MASK * Q4, LABEL_RADIUS * Q4, VINYL);
}

RIPPL_NP_FACES_CODE static int logo_level(int x, int y)
{
    return rippl_logo_alpha(&RIPPL_LOGO_LABEL, x, y);
}

RIPPL_NP_FACES_CODE static uint8_t logo_alpha(int u, int v)
{
    int x = u >> FRACTION_BITS;
    int y = v >> FRACTION_BITS;
    int across = u & FRACTION_MASK;
    int down = v & FRACTION_MASK;
    int top = logo_level(x, y) * (FRACTION_ONE - across) + logo_level(x + 1, y) * across;
    int bottom = logo_level(x, y + 1) * (FRACTION_ONE - across) + logo_level(x + 1, y + 1) * across;
    int rounding = 1 << (2 * FRACTION_BITS - 1);
    return (uint8_t)((top * (FRACTION_ONE - down) + bottom * down + rounding) >>
                     (2 * FRACTION_BITS));
}

RIPPL_NP_FACES_CODE static void turned_logo(const Deck *deck, uint8_t angle)
{
    int sine = shape_sine(angle);
    int cosine = shape_cosine(angle);
    int side = RIPPL_LOGO_LABEL.side;
    int middle = side * FRACTION_ONE / 2 - FRACTION_ONE / 2;
    for (int row = 0; row < side; row++) {
        int dy = 2 * row + 1 - side;
        for (int column = 0; column < side; column++) {
            int dx = 2 * column + 1 - side;
            uint8_t alpha = logo_alpha(middle + (dx * cosine + dy * sine) / 2,
                                       middle + (dy * cosine - dx * sine) / 2);
            if (alpha != 0) {
                Point at = {deck->centre.x - side / 2 + column, deck->centre.y - side / 2 + row};
                canvas_blend_pixel(deck->canvas, at, CREAM, alpha);
            }
        }
    }
}

RIPPL_NP_FACES_CODE static void label_art(const Deck *deck)
{
    Image cover = deck->frame->cover;
    uint8_t angle = label_angle(deck);
    if (cover.pixels == NULL) {
        ring(deck, LABEL_RADIUS * Q4, 0, LABEL_RED);
        turned_logo(deck, angle);
    } else {
        turned_cover(deck, cover, angle);
    }
}

RIPPL_NP_FACES_CODE static void grooves(const Deck *deck)
{
    for (int index = 0; index < GROOVE_COUNT; index++) {
        int radius = GROOVE_FIRST + GROOVE_STEP * index;
        ring(deck, (radius + 1) * Q4, radius * Q4, ALU_DARK);
    }
    ring(deck, SPINDLE, 0, ALU);
}

RIPPL_NP_FACES_CODE static void badge(const Deck *deck)
{
    if (face_badge_kind(deck->frame) == FACE_ICON_NONE) {
        return;
    }
    Point at = deck->centre;
    ring(deck, BADGE_DISC, 0, VINYL);
    face_badge_draw(deck->canvas, deck->frame,
                    (Rect){at.x - BADGE_SIDE / 2, at.y - BADGE_SIDE / 2, BADGE_SIDE, BADGE_SIDE},
                    CREAM);
}

RIPPL_NP_FACES_CODE static void arm(const Deck *deck)
{
    const NowPlayingFrame *frame = deck->frame;
    int steps = face_progress_width(frame->shown_ms, frame->status->duration_ms, ARM_STEPS);
    uint8_t angle = (uint8_t)(ARM_REST + steps);
    uint8_t head = (uint8_t)(angle + ARM_HEAD_TURN);
    Point pivot = {deck->centre.x + ARM_DX, deck->centre.y + ARM_DY};
    Point stylus = shape_polar(pivot, angle, ARM_REACH);
    Canvas *canvas = deck->canvas;
    spoke(canvas, (ShapeSpoke){pivot, (uint8_t)(angle + HALF_TURN), 160, 7, 21}, ALU_DARK);
    dot(canvas, pivot, 160, ALU_DARK);
    dot(canvas, pivot, 112, ALU);
    spoke(canvas, (ShapeSpoke){pivot, angle, 64, 0, 72}, ALU);
    spoke(canvas, (ShapeSpoke){stylus, head, 112, -14, 3}, ALU);
    spoke(canvas, (ShapeSpoke){stylus, head, 48, -11, 0}, VINYL);
    dot(canvas, pivot, 48, ALU_DARK);
}

RIPPL_NP_FACES_CODE static void lamp(const Deck *deck)
{
    Point at = {deck->centre.x + LAMP_DX, deck->centre.y + LAMP_DY};
    dot(deck->canvas, at, LAMP_RADIUS, deck->frame->playing ? AMBER : ALU_DARK);
}

RIPPL_NP_FACES_CODE static const Rows *rows_of(const NowPlayingFrame *frame)
{
    return frame->area.height > frame->area.width ? &PORTRAIT_ROWS : &LANDSCAPE_ROWS;
}

RIPPL_NP_FACES_CODE static FaceLine line_of(const NowPlayingFrame *frame, Slot slot)
{
    const Rows *rows = rows_of(frame);
    Rect area = frame->area;
    int y = slot.row >= 0 ? area.y + slot.row : area.y + area.height + slot.row;
    return (FaceLine){
        slot.font, {rows->x, y, rows->width, slot.height}, slot.color, slot.color, rows->align};
}

RIPPL_NP_FACES_CODE static void note(const Deck *deck, FaceLine line, const char *text)
{
    if (text != NULL) {
        Rect fitted = face_fitted_area(line.font, line.area, text, line.align);
        label_draw(deck->canvas, (TextStyle){line.font, line.color}, (Label){fitted, text});
    }
}

RIPPL_NP_FACES_CODE static void heading(const Deck *deck)
{
    const NowPlayingFrame *frame = deck->frame;
    const Rows *rows = rows_of(frame);
    const char *lyric = frame->lyric;
    bool sung = lyric != NULL && lyric[0] != '\0';
    FaceLine title =
        line_of(frame, (Slot){frame->theme->fonts.title, rows->title, LINE_BIG, CREAM});
    FaceLine artist = line_of(frame, (Slot){frame->theme->fonts.body, rows->artist, LINE_BIG, TAN});
    FaceLine album = line_of(
        frame, (Slot){frame->theme->fonts.body, rows->album, LINE_BIG, sung ? CREAM : TAN_DIM});
    face_line(deck->canvas, frame, &title, frame->song);
    face_line(deck->canvas, frame, &artist, frame->artist);
    face_line(deck->canvas, frame, &album, sung ? lyric : frame->album);
}

RIPPL_NP_FACES_CODE static void modes(const Deck *deck)
{
    const NowPlayingFrame *frame = deck->frame;
    char buffer[FACE_FEEDBACK_CAPACITY];
    const char *feedback = face_feedback_text(frame, buffer, sizeof buffer);
    Slot slot = {frame->theme->fonts.small, rows_of(frame)->modes, LINE_SMALL,
                 feedback != NULL ? CREAM : TAN_DIM};
    note(deck, line_of(frame, slot), feedback != NULL ? feedback : frame->modes);
}

RIPPL_NP_FACES_CODE static void details(const Deck *deck)
{
    const NowPlayingFrame *frame = deck->frame;
    const Rows *rows = rows_of(frame);
    const Font *small = frame->theme->fonts.small;
    FaceLine format = line_of(frame, (Slot){small, rows->format, LINE_SMALL, TAN});
    face_format_row(deck->canvas, frame, &format);
    if (rows->has_detail) {
        note(deck, line_of(frame, (Slot){small, rows->detail, LINE_SMALL, TAN_DIM}), frame->detail);
    }
    modes(deck);
}

RIPPL_NP_FACES_CODE static void progress(const Deck *deck)
{
    const NowPlayingFrame *frame = deck->frame;
    const Rows *rows = rows_of(frame);
    const Font *small = frame->theme->fonts.small;
    FaceLine bar = line_of(frame, (Slot){small, rows->bar, BAR_HEIGHT, TAN});
    face_bar_draw(deck->canvas, frame, &(FaceBar){bar.area, ALU_DARK, CREAM, AMBER});
    FaceLine times = line_of(frame, (Slot){small, rows->times, LINE_SMALL, TAN});
    face_times_draw(deck->canvas, frame, &times);
}

RIPPL_NP_FACES_CODE void face_turntable_draw(const NowPlayingFrame *frame, Canvas *canvas)
{
    Deck deck = {canvas, frame, {CENTRE_X, frame->area.y + CENTRE_DROP}};
    plate(&deck);
    label_art(&deck);
    grooves(&deck);
    badge(&deck);
    arm(&deck);
    lamp(&deck);
    heading(&deck);
    details(&deck);
    progress(&deck);
}
