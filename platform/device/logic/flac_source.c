#include "device/flac_source.h"

enum {
    MILLISECONDS_PER_SECOND = 1000
};

void flac_source_init(FlacSource *source, FlacFileOpener files, FlacDecoder *decoder)
{
    *source = (FlacSource){.files = files, .decoder = decoder, .open = false};
}

void flac_source_close(FlacSource *source)
{
    if (source->open) {
        source->files.close(source->files.context);
        source->open = false;
    }
}

bool flac_source_open_at(FlacSource *source, const char *path, uint32_t start_ms,
                         AudioTrackInfo *track)
{
    flac_source_close(source);
    FlacInput input;
    if (!source->files.open(source->files.context, path, &input)) {
        return false;
    }
    source->open = true;
    FlacDecoder *decoder = source->decoder;
    if (flac_open(decoder, input) != FLAC_OK) {
        flac_source_close(source);
        return false;
    }
    uint32_t target =
        (uint32_t)((uint64_t)start_ms * decoder->info.sample_rate / MILLISECONDS_PER_SECOND);
    if (target > 0 && flac_seek(decoder, target) != FLAC_OK) {
        flac_source_close(source);
        return false;
    }
    *track = (AudioTrackInfo){.stream = decoder->info,
                              .audio_bytes = input.size - decoder->audio_offset};
    return true;
}
