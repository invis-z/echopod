#include "core/resident.h"
#include "faces.h"

RIPPL_NP_FACES_CODE static void draw(const NowPlayingFrame *frame, Canvas *canvas)
{
    switch (frame->face) {
    case NP_FACE_CASSETTE:
        face_cassette_draw(frame, canvas);
        break;
    case NP_FACE_TURNTABLE:
        face_turntable_draw(frame, canvas);
        break;
    case NP_FACE_TAPE_WINDOW:
        face_tape_window_draw(frame, canvas);
        break;
    default:
        break;
    }
}

RIPPL_NP_FACES_ENTRY const NowPlayingFaces NOW_PLAYING_FACES = {
    .magic = NP_FACES_MAGIC,
    .draw = draw,
};
