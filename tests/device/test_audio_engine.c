#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/audio_engine.h"
#include "device/flac_source.h"

enum {
    RING_FRAMES = 8192,
    RING_WORDS = 2 * RING_FRAMES
};

typedef struct Fixture {
    uint8_t *bytes;
    uint32_t size;
} Fixture;

static FlacDecoder decoder;
static FlacBlockBuffer block;
static FlacSource source;
static uint32_t ring[RING_WORDS];
static AudioEngine engine;
static Fixture opened;

static Fixture load(const char *name, const char *extension)
{
    char path[512];
    snprintf(path, sizeof path, "%s/flac/%s.%s", RIPPL_FIXTURE_DIR, name, extension);
    Fixture fixture = {NULL, 0};
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return fixture;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    fixture.bytes = malloc((size_t)size);
    if (fixture.bytes != NULL && fread(fixture.bytes, 1, (size_t)size, file) == (size_t)size) {
        fixture.size = (uint32_t)size;
    }
    fclose(file);
    return fixture;
}

static bool read_fixture(void *context, uint32_t offset, void *destination, uint32_t length)
{
    const Fixture *fixture = context;
    if (offset > fixture->size || length > fixture->size - offset) {
        return false;
    }
    memcpy(destination, fixture->bytes + offset, length);
    return true;
}

static bool open_file(void *context, const char *path, FlacInput *input)
{
    (void)context;
    free(opened.bytes);
    opened = load(path, "flac");
    *input = (FlacInput){.read = read_fixture, .context = &opened, .size = opened.size};
    return opened.size > 0;
}

static void close_file(void *context)
{
    (void)context;
}

static AudioFrameResult decode_next(void *context, AudioFrame *frame)
{
    FlacDecoder *flac = ((FlacSource *)context)->decoder;
    FlacResult result = flac_decode_frame(flac);
    if (result != FLAC_OK) {
        return result == FLAC_END ? AUDIO_FRAME_END : AUDIO_FRAME_ERROR;
    }
    *frame = (AudioFrame){.left = flac_channel(flac, 0),
                          .right = flac_channel(flac, flac->info.channels > 1 ? 1 : 0),
                          .first_sample = flac->frame_first_sample,
                          .samples = flac->block_samples,
                          .bits = flac->info.bits_per_sample};
    return AUDIO_FRAME_READY;
}

static bool decode_open(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    return flac_source_open_at(context, path, start_ms, track);
}

static void decode_release(void *context)
{
    (void)context;
}

static void decode_close(void *context)
{
    flac_source_close(context);
}

static void start_engine(void)
{
    flac_use_block_buffer(&decoder, &block);
    flac_source_init(&source,
                     (FlacFileOpener){.open = open_file, .close = close_file, .context = NULL},
                     &decoder);
    audio_engine_init(&engine,
                      (AudioEngineConfig){.source = {decode_open, decode_next, decode_release,
                                                     decode_close, &source},
                                          .ring = ring,
                                          .ring_words = RING_WORDS});
}

typedef struct ScriptedSource {
    int32_t left[16];
    int32_t right[16];
    int waits_before_frame;
    int frames_left;
    int releases;
    int closes;
    uint32_t rate;
    uint32_t play_rate;
    uint8_t bits;
    AudioCodec codec;
} ScriptedSource;

static ScriptedSource scripted;

static bool scripted_open(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    (void)path;
    (void)start_ms;
    const ScriptedSource *source_state = context;
    uint32_t rate = source_state->rate == 0 ? 44100 : source_state->rate;
    uint8_t bits = source_state->bits == 0 ? 16 : source_state->bits;
    *track = (AudioTrackInfo){
        .stream = {.sample_rate = rate, .channels = 2, .bits_per_sample = bits, .max_block = 16},
        .play_rate = source_state->play_rate,
        .codec = source_state->codec};
    return true;
}

static AudioFrameResult scripted_next(void *context, AudioFrame *frame)
{
    ScriptedSource *source_state = context;
    if (source_state->waits_before_frame > 0) {
        source_state->waits_before_frame--;
        return AUDIO_FRAME_WAIT;
    }
    if (source_state->frames_left == 0) {
        return AUDIO_FRAME_END;
    }
    source_state->frames_left--;
    *frame = (AudioFrame){.left = source_state->left,
                          .right = source_state->right,
                          .first_sample = 0,
                          .samples = 16,
                          .bits = source_state->bits == 0 ? 16 : source_state->bits};
    return AUDIO_FRAME_READY;
}

static void scripted_release(void *context)
{
    ((ScriptedSource *)context)->releases++;
}

static void scripted_close(void *context)
{
    ((ScriptedSource *)context)->closes++;
}

static void start_scripted_engine(int waits, int frames)
{
    scripted = (ScriptedSource){.waits_before_frame = waits, .frames_left = frames};
    for (int index = 0; index < 16; index++) {
        scripted.left[index] = index + 1;
        scripted.right[index] = -(index + 1);
    }
    AudioSource interface = {scripted_open, scripted_next, scripted_release, scripted_close,
                             &scripted};
    audio_engine_init(
        &engine, (AudioEngineConfig){.source = interface, .ring = ring, .ring_words = RING_WORDS});
}

static int32_t expected_at(const Fixture *pcm, uint32_t index)
{
    int32_t value;
    memcpy(&value, pcm->bytes + 4 * index, sizeof value);
    return value;
}

static uint32_t word_of(int32_t sample, int bits)
{
    return (uint32_t)sample << (32 - bits);
}

static bool frame_is(uint32_t frame, int32_t left, int32_t right, int bits)
{
    uint32_t slot = frame % RING_FRAMES;
    return ring[2 * slot] == word_of(left, bits) && ring[2 * slot + 1] == word_of(right, bits);
}

static bool ring_holds(const char *name, uint32_t first_frame, uint32_t ring_start, uint32_t frames,
                       int bits)
{
    Fixture pcm = load(name, "pcm");
    bool ok = pcm.size > 0;
    for (uint32_t frame = 0; ok && frame < frames; frame++) {
        uint32_t index = 2 * (first_frame + frame);
        ok = frame_is(ring_start + frame, expected_at(&pcm, index), expected_at(&pcm, index + 1),
                      bits);
    }
    free(pcm.bytes);
    return ok;
}

static void pump_until_full(uint32_t consumed)
{
    for (int round = 0; round < 64; round++) {
        audio_engine_pump(&engine, consumed);
    }
}

static void playing_fills_the_ring_with_the_track(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "lpc_16_44k_stereo", 0, 7));
    CHECK_EQUAL(44100u, audio_engine_rate(&engine));
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 0, 0, 6000, 16));
    AudioEngineStatus status = audio_engine_status(&engine);
    CHECK_EQUAL(AUDIO_ENGINE_PLAYING, status.state);
    CHECK_EQUAL(7u, status.token);
    CHECK_EQUAL(0u, status.position_ms);
}

static void use_32_bit_frames(void)
{
    scripted.bits = 32;
    for (int index = 0; index < 16; index++) {
        scripted.left[index] = (int32_t)(0x12345671u * (uint32_t)(index + 1));
        scripted.right[index] = -scripted.left[index];
    }
}

static void a_32_bit_frame_reaches_the_ring_whole(void)
{
    start_scripted_engine(0, 1);
    use_32_bit_frames();
    CHECK(audio_engine_play(&engine, "scripted", 0, 1));
    pump_until_full(0);
    for (uint32_t frame = 0; frame < 16; frame++) {
        CHECK(frame_is(frame, scripted.left[frame], scripted.right[frame], 32));
    }
}

static void the_position_follows_what_the_dma_consumed(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "lpc_16_44k_stereo", 0, 7));
    pump_until_full(0);
    audio_engine_pump(&engine, 4410);
    CHECK_EQUAL(100u, audio_engine_status(&engine).position_ms);
}

static void twenty_four_bit_audio_reaches_the_ring_exactly(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "lpc_24_96k_stereo", 0, 1));
    CHECK_EQUAL(96000u, audio_engine_rate(&engine));
    pump_until_full(0);
    CHECK(ring_holds("lpc_24_96k_stereo", 0, 0, 5000, 24));
}

static void the_end_of_a_track_without_a_next_one_stops_and_goes_silent(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "odd_blocks_16", 0, 3));
    pump_until_full(0);
    CHECK(frame_is(3001 + 10, 0, 0, 16));
    audio_engine_pump(&engine, 3000);
    CHECK_EQUAL(AUDIO_ENGINE_PLAYING, audio_engine_status(&engine).state);
    audio_engine_pump(&engine, 3001);
    AudioEngineStatus status = audio_engine_status(&engine);
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, status.state);
    CHECK_EQUAL(3u, status.token);
    CHECK(!status.failed);
}

static void a_queued_track_follows_without_a_gap(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "odd_blocks_16", 0, 3));
    audio_engine_queue_next(&engine, "lpc_16_44k_stereo", 4);
    pump_until_full(0);
    CHECK(ring_holds("odd_blocks_16", 0, 0, 3001, 16));
    CHECK(ring_holds("lpc_16_44k_stereo", 0, 3001, 4000, 16));
    audio_engine_pump(&engine, 3000);
    CHECK_EQUAL(3u, audio_engine_status(&engine).token);
    audio_engine_pump(&engine, 3001 + 441);
    AudioEngineStatus status = audio_engine_status(&engine);
    CHECK_EQUAL(4u, status.token);
    CHECK_EQUAL(10u, status.position_ms);
}

static void the_status_describes_the_playing_tracks_stream(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "lpc_24_96k_stereo", 0, 1));
    AudioTrackInfo track = audio_engine_status(&engine).track;
    CHECK_EQUAL(96000u, track.stream.sample_rate);
    CHECK_EQUAL(24, track.stream.bits_per_sample);
    CHECK_EQUAL(2, track.stream.channels);
    CHECK_EQUAL(5000u, track.stream.total_samples);
    CHECK_EQUAL(25502u, track.audio_bytes);
}

static void the_stream_description_follows_the_audio_into_the_next_track(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "odd_blocks_16", 0, 3));
    audio_engine_queue_next(&engine, "lpc_16_44k_stereo", 4);
    pump_until_full(0);
    CHECK_EQUAL(9451u, audio_engine_status(&engine).track.audio_bytes);
    audio_engine_pump(&engine, 3001 + 441);
    CHECK_EQUAL(18719u, audio_engine_status(&engine).track.audio_bytes);
}

static void a_next_track_at_another_rate_waits_for_the_ring_to_drain(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "odd_blocks_16", 0, 3));
    audio_engine_queue_next(&engine, "lpc_24_96k_stereo", 4);
    pump_until_full(0);
    uint32_t rate = 0;
    CHECK(!audio_engine_wants_rate(&engine, &rate));
    audio_engine_pump(&engine, 3001);
    CHECK(audio_engine_wants_rate(&engine, &rate));
    CHECK_EQUAL(96000u, rate);
    audio_engine_restart(&engine);
    pump_until_full(0);
    CHECK(ring_holds("lpc_24_96k_stereo", 0, 0, 5000, 24));
    CHECK_EQUAL(4u, audio_engine_status(&engine).token);
    CHECK_EQUAL(96000u, audio_engine_rate(&engine));
}

static void waiting_for_a_new_rate_plays_silence_not_old_audio(void)
{
    start_engine();
    memset(ring, 0x55, sizeof ring);
    CHECK(audio_engine_play(&engine, "odd_blocks_16", 0, 3));
    audio_engine_queue_next(&engine, "lpc_24_96k_stereo", 4);
    pump_until_full(0);
    for (uint32_t frame = 3001; frame < RING_FRAMES; frame++) {
        CHECK(frame_is(frame, 0, 0, 16));
    }
    audio_engine_pump(&engine, 6000);
    CHECK_EQUAL(0u, audio_engine_stats(&engine).underruns);
    for (uint32_t frame = 0; frame < 6000; frame++) {
        CHECK(frame_is(RING_FRAMES + frame, 0, 0, 16));
    }
    uint32_t rate = 0;
    CHECK(audio_engine_wants_rate(&engine, &rate));
}

static void a_lead_in_plays_silence_before_the_track(void)
{
    start_engine();
    memset(ring, 0x55, sizeof ring);
    CHECK(audio_engine_play(&engine, "lpc_16_44k_stereo", 0, 7));
    audio_engine_lead_in(&engine, 100, 0);
    pump_until_full(0);
    for (uint32_t frame = 0; frame < 100; frame++) {
        CHECK(frame_is(frame, 0, 0, 16));
    }
    CHECK(ring_holds("lpc_16_44k_stereo", 0, 100, 4000, 16));
    audio_engine_pump(&engine, 50);
    CHECK_EQUAL(0u, audio_engine_status(&engine).position_ms);
    audio_engine_pump(&engine, 100 + 4410);
    CHECK_EQUAL(100u, audio_engine_status(&engine).position_ms);
    CHECK_EQUAL(7u, audio_engine_status(&engine).token);
}

static void a_lead_in_ramps_the_first_frames_up(void)
{
    start_scripted_engine(0, 4);
    for (int index = 0; index < 16; index++) {
        scripted.left[index] = 2000;
        scripted.right[index] = -2000;
    }
    CHECK(audio_engine_play(&engine, "scripted", 0, 1));
    audio_engine_lead_in(&engine, 10, 8);
    pump_until_full(0);
    for (uint32_t step = 0; step < 8; step++) {
        CHECK(frame_is(10 + step, (int32_t)(250 * step), -(int32_t)(250 * step), 16));
    }
    for (uint32_t frame = 18; frame < 10 + 64; frame++) {
        CHECK(frame_is(frame, 2000, -2000, 16));
    }
}

static void a_lead_in_only_applies_before_anything_was_written(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "lpc_16_44k_stereo", 0, 7));
    pump_until_full(0);
    audio_engine_lead_in(&engine, 100, 0);
    CHECK(ring_holds("lpc_16_44k_stereo", 0, 0, 1000, 16));
    audio_engine_pump(&engine, 4410);
    CHECK_EQUAL(100u, audio_engine_status(&engine).position_ms);
}

static void starting_part_way_skips_to_that_sample(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "lpc_16_44k_stereo", 100, 5));
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 4410, 0, 1000, 16));
    CHECK_EQUAL(100u, audio_engine_status(&engine).position_ms);
}

static void seeking_restarts_the_ring_at_the_new_place(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "lpc_16_44k_stereo", 0, 5));
    pump_until_full(0);
    audio_engine_pump(&engine, 1000);
    CHECK(audio_engine_seek(&engine, 50));
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 2205, 0, 1000, 16));
    CHECK_EQUAL(50u, audio_engine_status(&engine).position_ms);
    CHECK_EQUAL(5u, audio_engine_status(&engine).token);
}

static void seeking_after_the_next_track_opened_queues_it_again(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "odd_blocks_16", 0, 3));
    audio_engine_queue_next(&engine, "lpc_16_44k_stereo", 4);
    pump_until_full(0);
    CHECK(audio_engine_seek(&engine, 20));
    pump_until_full(0);
    CHECK_EQUAL(3u, audio_engine_status(&engine).token);
    audio_engine_pump(&engine, 3001 - 882 + 441);
    CHECK_EQUAL(4u, audio_engine_status(&engine).token);
    CHECK_EQUAL(10u, audio_engine_status(&engine).position_ms);
}

static void a_seek_while_a_new_rate_waits_keeps_the_next_track(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "odd_blocks_16", 0, 3));
    audio_engine_queue_next(&engine, "lpc_24_96k_stereo", 4);
    pump_until_full(0);
    CHECK(audio_engine_seek(&engine, 0));
    pump_until_full(0);
    audio_engine_pump(&engine, 3001);
    uint32_t rate = 0;
    CHECK(audio_engine_wants_rate(&engine, &rate));
    CHECK_EQUAL(96000u, rate);
}

static void closing_the_source_keeps_the_pause_and_the_position(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "lpc_16_44k_stereo", 0, 5));
    pump_until_full(0);
    audio_engine_pump(&engine, 2205);
    audio_engine_pause(&engine);
    audio_engine_close_source(&engine);
    AudioEngineStatus status = audio_engine_status(&engine);
    CHECK_EQUAL(AUDIO_ENGINE_PAUSED, status.state);
    CHECK_EQUAL(50u, status.position_ms);
    CHECK_EQUAL(5u, status.token);
    CHECK(audio_engine_seek(&engine, status.position_ms));
    CHECK_EQUAL(AUDIO_ENGINE_PAUSED, audio_engine_status(&engine).state);
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 2205, 0, 1000, 16));
}

static void mono_plays_on_both_channels(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "fixed_16_mono", 0, 1));
    pump_until_full(0);
    Fixture pcm = load("fixed_16_mono", "pcm");
    int32_t sample = expected_at(&pcm, 500);
    CHECK(frame_is(500, sample, sample, 16));
    free(pcm.bytes);
}

static bool scripted_rate_plays(uint32_t rate)
{
    start_scripted_engine(0, 1);
    scripted.rate = rate;
    return audio_engine_play(&engine, "scripted", 0, 1);
}

static void an_unsupported_rate_is_refused(void)
{
    CHECK(!scripted_rate_plays(7000));
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, audio_engine_status(&engine).state);
    AudioRefusal refusal = audio_engine_refusal(&engine);
    CHECK_EQUAL(AUDIO_REFUSAL_RATE, refusal.problem);
    CHECK_EQUAL(7000u, refusal.sample_rate);
}

static void a_queued_track_at_an_unsupported_rate_is_refused_with_its_rate(void)
{
    start_scripted_engine(0, 1);
    CHECK(audio_engine_play(&engine, "scripted", 0, 3));
    scripted.rate = 7000;
    scripted.frames_left = 1;
    audio_engine_queue_next(&engine, "scripted", 4);
    pump_until_full(0);
    CHECK(audio_engine_status(&engine).failed);
    CHECK_EQUAL(AUDIO_REFUSAL_RATE, audio_engine_refusal(&engine).problem);
    CHECK_EQUAL(7000u, audio_engine_refusal(&engine).sample_rate);
}

static void a_low_rate_plays_at_the_rate_its_source_raises_it_to(void)
{
    start_scripted_engine(0, 1);
    scripted.rate = 22050;
    scripted.play_rate = 44100;
    CHECK(audio_engine_play(&engine, "scripted", 0, 1));
    CHECK_EQUAL(44100u, audio_engine_rate(&engine));
    CHECK_EQUAL(22050u, audio_engine_status(&engine).track.stream.sample_rate);
    audio_engine_observe(&engine, 44100);
    CHECK_EQUAL(1000u, audio_engine_status(&engine).position_ms);
}

static void a_low_rate_no_source_raises_is_refused(void)
{
    CHECK(!scripted_rate_plays(22050));
    CHECK_EQUAL(AUDIO_REFUSAL_RATE, audio_engine_refusal(&engine).problem);
    CHECK_EQUAL(22050u, audio_engine_refusal(&engine).sample_rate);
}

static void a_32_khz_track_plays_at_its_own_rate(void)
{
    CHECK(scripted_rate_plays(32000));
    CHECK_EQUAL(32000u, audio_engine_rate(&engine));
}

static void rates_up_to_384_khz_play_and_higher_ones_are_refused(void)
{
    CHECK(scripted_rate_plays(192000));
    CHECK_EQUAL(192000u, audio_engine_rate(&engine));
    CHECK(scripted_rate_plays(176400));
    CHECK_EQUAL(176400u, audio_engine_rate(&engine));
    CHECK(scripted_rate_plays(96000));
    CHECK(!scripted_rate_plays(768000));
    CHECK_EQUAL(AUDIO_REFUSAL_RATE, audio_engine_refusal(&engine).problem);
    CHECK_EQUAL(768000u, audio_engine_refusal(&engine).sample_rate);
}

static bool scripted_depth_plays(AudioCodec codec, uint32_t rate, uint8_t bits)
{
    start_scripted_engine(0, 1);
    scripted.codec = codec;
    scripted.rate = rate;
    scripted.bits = bits;
    return audio_engine_play(&engine, "scripted", 0, 1);
}

static void compressed_samples_wider_than_24_bits_are_refused_above_96_khz(void)
{
    CHECK(scripted_depth_plays(AUDIO_CODEC_FLAC, 88200, 32));
    CHECK(scripted_depth_plays(AUDIO_CODEC_ALAC, 96000, 32));
    CHECK(scripted_depth_plays(AUDIO_CODEC_FLAC, 192000, 24));
    CHECK(scripted_depth_plays(AUDIO_CODEC_WAV, 192000, 32));
    CHECK(!scripted_depth_plays(AUDIO_CODEC_ALAC, 176400, 32));
    CHECK(!scripted_depth_plays(AUDIO_CODEC_FLAC, 192000, 32));
    AudioRefusal refusal = audio_engine_refusal(&engine);
    CHECK_EQUAL(AUDIO_REFUSAL_DEPTH, refusal.problem);
    CHECK_EQUAL(192000u, refusal.sample_rate);
    CHECK_EQUAL(32u, refusal.bits);
}

static void wav_plays_at_352_8_and_384_khz(void)
{
    CHECK(scripted_depth_plays(AUDIO_CODEC_WAV, 352800, 32));
    CHECK_EQUAL(352800u, audio_engine_rate(&engine));
    CHECK(scripted_depth_plays(AUDIO_CODEC_WAV, 384000, 32));
    CHECK_EQUAL(384000u, audio_engine_rate(&engine));
    CHECK(scripted_depth_plays(AUDIO_CODEC_WAV, 384000, 24));
}

static void flac_and_alac_stop_at_192_khz(void)
{
    CHECK(scripted_depth_plays(AUDIO_CODEC_FLAC, 192000, 24));
    CHECK(!scripted_depth_plays(AUDIO_CODEC_FLAC, 352800, 24));
    CHECK_EQUAL(AUDIO_REFUSAL_RATE, audio_engine_refusal(&engine).problem);
    CHECK_EQUAL(352800u, audio_engine_refusal(&engine).sample_rate);
    CHECK_EQUAL(0u, audio_engine_refusal(&engine).bits);
    CHECK(!scripted_depth_plays(AUDIO_CODEC_ALAC, 384000, 16));
}

static void a_missing_file_is_refused(void)
{
    start_engine();
    CHECK(!audio_engine_play(&engine, "no_such_file", 0, 1));
    CHECK_EQUAL(AUDIO_REFUSAL_UNREADABLE, audio_engine_refusal(&engine).problem);
}

static void the_ring_never_runs_ahead_of_the_dma_by_more_than_its_size(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "constant_and_noise", 0, 1));
    pump_until_full(0);
    CHECK(audio_engine_buffered(&engine, 0) <= RING_FRAMES);
    CHECK(audio_engine_buffered(&engine, 0) >= RING_FRAMES - 4608);
}

static void observing_the_dma_position_never_decodes(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "lpc_16_44k_stereo", 0, 7));
    pump_until_full(0);
    uint32_t produced = audio_engine_buffered(&engine, 0);
    audio_engine_observe(&engine, 4410);
    CHECK_EQUAL(100u, audio_engine_status(&engine).position_ms);
    CHECK_EQUAL(produced - 4410, audio_engine_buffered(&engine, 4410));
    audio_engine_pump(&engine, 4410);
    CHECK(audio_engine_buffered(&engine, 4410) > produced - 4410);
}

static void the_dma_overtaking_the_decoder_counts_an_underrun(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "constant_and_noise", 0, 7));
    pump_until_full(0);
    CHECK_EQUAL(0u, audio_engine_stats(&engine).underruns);
    audio_engine_pump(&engine, RING_FRAMES + 100);
    CHECK_EQUAL(1u, audio_engine_stats(&engine).underruns);
    CHECK(audio_engine_buffered(&engine, RING_FRAMES + 100) <= RING_FRAMES);
    audio_engine_pump(&engine, RING_FRAMES + 200);
    CHECK_EQUAL(1u, audio_engine_stats(&engine).underruns);
}

static void what_the_dma_played_is_silenced_so_a_late_lap_plays_nothing_old(void)
{
    start_scripted_engine(0, 1);
    CHECK(audio_engine_play(&engine, "scripted", 0, 1));
    audio_engine_pump(&engine, 0);
    CHECK(frame_is(0, 1, -1, 16));
    audio_engine_observe(&engine, 8);
    CHECK(frame_is(0, 0, 0, 16));
    CHECK(frame_is(7, 0, 0, 16));
    CHECK(frame_is(8, 9, -9, 16));
    audio_engine_observe(&engine, 3 * RING_FRAMES);
    CHECK(frame_is(8, 0, 0, 16));
    CHECK(frame_is(RING_FRAMES - 1, 0, 0, 16));
}

static void every_decoded_frame_is_counted(void)
{
    start_engine();
    CHECK(audio_engine_play(&engine, "lpc_16_44k_stereo", 0, 7));
    CHECK_EQUAL(0u, audio_engine_stats(&engine).frames);
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(2u, audio_engine_stats(&engine).frames);
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(2u, audio_engine_stats(&engine).frames);
    audio_engine_pump(&engine, 4096);
    CHECK_EQUAL(2u, audio_engine_stats(&engine).frames);
}

static void the_current_path_is_the_track_being_played(void)
{
    start_engine();
    CHECK(audio_engine_current_path(&engine) == NULL);
    CHECK(audio_engine_play(&engine, "lpc_16_44k_stereo", 0, 1));
    CHECK(strcmp("lpc_16_44k_stereo", audio_engine_current_path(&engine)) == 0);
}

static void a_long_card_path_is_kept_whole(void)
{
    start_scripted_engine(0, 1);
    char path[1000];
    memset(path, 'a', sizeof path - 1);
    path[0] = '/';
    path[sizeof path - 1] = '\0';
    CHECK(audio_engine_play(&engine, path, 0, 1));
    CHECK(strcmp(path, audio_engine_current_path(&engine)) == 0);
}

static void a_source_that_is_not_ready_leaves_the_ring_alone(void)
{
    start_scripted_engine(2, 1);
    CHECK(audio_engine_play(&engine, "scripted", 0, 1));
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(0u, audio_engine_buffered(&engine, 0));
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(0u, audio_engine_buffered(&engine, 0));
    audio_engine_pump(&engine, 0);
    CHECK(audio_engine_buffered(&engine, 0) >= 16u);
    CHECK(frame_is(0, 1, -1, 16));
    CHECK(frame_is(15, 16, -16, 16));
}

static void a_frame_is_released_once_its_samples_are_in_the_ring(void)
{
    start_scripted_engine(0, 2);
    CHECK(audio_engine_play(&engine, "scripted", 0, 1));
    CHECK_EQUAL(0, scripted.releases);
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(2, scripted.releases);
    audio_engine_stop(&engine);
    CHECK_EQUAL(1, scripted.closes);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_current_path_is_the_track_being_played),
        TEST_CASE(a_long_card_path_is_kept_whole),
        TEST_CASE(a_source_that_is_not_ready_leaves_the_ring_alone),
        TEST_CASE(a_frame_is_released_once_its_samples_are_in_the_ring),
        TEST_CASE(observing_the_dma_position_never_decodes),
        TEST_CASE(the_dma_overtaking_the_decoder_counts_an_underrun),
        TEST_CASE(what_the_dma_played_is_silenced_so_a_late_lap_plays_nothing_old),
        TEST_CASE(every_decoded_frame_is_counted),
        TEST_CASE(playing_fills_the_ring_with_the_track),
        TEST_CASE(a_32_bit_frame_reaches_the_ring_whole),
        TEST_CASE(the_position_follows_what_the_dma_consumed),
        TEST_CASE(twenty_four_bit_audio_reaches_the_ring_exactly),
        TEST_CASE(the_end_of_a_track_without_a_next_one_stops_and_goes_silent),
        TEST_CASE(a_queued_track_follows_without_a_gap),
        TEST_CASE(the_status_describes_the_playing_tracks_stream),
        TEST_CASE(the_stream_description_follows_the_audio_into_the_next_track),
        TEST_CASE(a_next_track_at_another_rate_waits_for_the_ring_to_drain),
        TEST_CASE(waiting_for_a_new_rate_plays_silence_not_old_audio),
        TEST_CASE(a_lead_in_plays_silence_before_the_track),
        TEST_CASE(a_lead_in_ramps_the_first_frames_up),
        TEST_CASE(a_lead_in_only_applies_before_anything_was_written),
        TEST_CASE(starting_part_way_skips_to_that_sample),
        TEST_CASE(seeking_restarts_the_ring_at_the_new_place),
        TEST_CASE(seeking_after_the_next_track_opened_queues_it_again),
        TEST_CASE(a_seek_while_a_new_rate_waits_keeps_the_next_track),
        TEST_CASE(closing_the_source_keeps_the_pause_and_the_position),
        TEST_CASE(mono_plays_on_both_channels),
        TEST_CASE(a_low_rate_plays_at_the_rate_its_source_raises_it_to),
        TEST_CASE(a_low_rate_no_source_raises_is_refused),
        TEST_CASE(a_32_khz_track_plays_at_its_own_rate),
        TEST_CASE(an_unsupported_rate_is_refused),
        TEST_CASE(rates_up_to_384_khz_play_and_higher_ones_are_refused),
        TEST_CASE(a_queued_track_at_an_unsupported_rate_is_refused_with_its_rate),
        TEST_CASE(compressed_samples_wider_than_24_bits_are_refused_above_96_khz),
        TEST_CASE(wav_plays_at_352_8_and_384_khz),
        TEST_CASE(flac_and_alac_stop_at_192_khz),
        TEST_CASE(a_missing_file_is_refused),
        TEST_CASE(the_ring_never_runs_ahead_of_the_dma_by_more_than_its_size),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
