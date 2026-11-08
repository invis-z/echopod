#include "device/core1_pcm.h"

#include "device/wav_format.h"

enum {
    CHUNK_BYTES = 480,
    BITS_PER_BYTE = 8,
    WIDEST_BITS = 32
};

static void start_pcm(void *context, FlacInput input, FlacInfo info, uint32_t first_sample,
                      const bool *starved)
{
    Core1Pcm *pcm = context;
    pcm->input = input;
    pcm->starved = starved;
    pcm->position = 0;
    pcm->next_sample = first_sample;
    pcm->channels = info.channels > 1 ? 2u : 1u;
    pcm->bits = info.bits_per_sample;
    pcm->frame_bytes = pcm->channels * pcm->bits / BITS_PER_BYTE;
}

static bool layout_ok(const Core1Pcm *pcm)
{
    uint32_t bits = pcm->bits;
    return (bits == 8 || bits == 16 || bits == 24 || bits == WIDEST_BITS) && pcm->frame_bytes > 0;
}

static bool convert(Core1Pcm *pcm, uint32_t frames)
{
    uint8_t chunk[CHUNK_BYTES];
    uint32_t per_chunk = CHUNK_BYTES / pcm->frame_bytes;
    uint32_t sample_bytes = pcm->bits / BITS_PER_BYTE;
    for (uint32_t done = 0; done < frames;) {
        uint32_t count = frames - done < per_chunk ? frames - done : per_chunk;
        uint32_t offset = pcm->position + done * pcm->frame_bytes;
        if (!pcm->input.read(pcm->input.context, offset, chunk, count * pcm->frame_bytes)) {
            return false;
        }
        for (uint32_t index = 0; index < count; index++) {
            const uint8_t *frame = chunk + index * pcm->frame_bytes;
            int32_t left = wav_sample(frame, pcm->bits);
            pcm->left[done + index] = left;
            pcm->right[done + index] =
                pcm->channels > 1 ? wav_sample(frame + sample_bytes, pcm->bits) : left;
        }
        done += count;
    }
    return true;
}

static Core1DecodeResult decode_pcm(void *context, Core1Block *block)
{
    Core1Pcm *pcm = context;
    if (!layout_ok(pcm)) {
        return CORE1_FAILED;
    }
    uint32_t frames = (pcm->input.size - pcm->position) / pcm->frame_bytes;
    if (frames == 0) {
        return CORE1_FINISHED;
    }
    uint32_t most = core1_pcm_block_frames(pcm->frame_bytes);
    frames = frames < most ? frames : most;
    uint8_t last = 0;
    uint32_t end = pcm->position + frames * pcm->frame_bytes;
    if (!pcm->input.read(pcm->input.context, end - 1, &last, 1) || !convert(pcm, frames)) {
        return *pcm->starved ? CORE1_STARVED : CORE1_FAILED;
    }
    block->channels[0] = pcm->left;
    block->channels[1] = pcm->right;
    block->first_sample = pcm->next_sample;
    block->samples = frames;
    block->position = end;
    pcm->position = end;
    pcm->next_sample += frames;
    return CORE1_DECODED;
}

Core1Decoder core1_pcm_decoder(Core1Pcm *pcm, int32_t *left, int32_t *right)
{
    *pcm = (Core1Pcm){.left = left, .right = right};
    return (Core1Decoder){start_pcm, decode_pcm, pcm};
}
