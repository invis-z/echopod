#include "usb_session.h"

#include <stddef.h>

#include "app/app.h"
#include "board.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "device/usb_audio.h"
#include "device/usb_audio_sink.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "irq.h"
#include "settings/settings.h"
#include "usb.h"

enum {
    HOST_TIMEOUT_MS = 3000,
    STREAM_IDLE_MS = 100,
    STREAM_QUIET_MS = 10,
    POLLS_PER_SERVICE = 4,
    BITS_PER_BYTE = 8,
    STATS_REFRESH_MS = 1000,
    STATE_REFRESH_MS = 5000,
    REPORT_QUEUE_CAPACITY = 8
};

static const char TITLE[] = "USB DAC";

static UsbShared *shared;
static UsbPort port;
static UsbAudio audio;
static UsbAudioSink sink;
static uint32_t open_rate;
static volatile uint32_t last_packet_ms;
static volatile bool receiving;
static volatile bool finished;
static uint32_t stream_consumed;
static uint32_t stream_last_position;
static bool playing;
static uint32_t shown_ms;
static bool stats_shown;
static uint32_t output_starts;
static uint32_t output_stops;
static uint32_t format_changes;
static uint32_t loss_last;
static uint32_t loss_longest;
static uint32_t loss_gap_us;
static uint8_t reports[REPORT_QUEUE_CAPACITY];
static uint8_t report_start;
static uint8_t report_count;

static uint32_t consumed_frames(void)
{
    uint32_t position = board_audio_stream_position();
    if (position == BOARD_AUDIO_STREAM_STOPPED) {
        stream_consumed = 0;
        stream_last_position = 0;
        return 0;
    }
    stream_consumed += (position + sink.ring_frames - stream_last_position) % sink.ring_frames;
    stream_last_position = position;
    return stream_consumed;
}

static void restart_position(void)
{
    uint32_t primask = irq_save();
    stream_consumed = 0;
    stream_last_position = 0;
    irq_restore(primask);
}

static void note_loss(void)
{
    uint32_t run = ((uint32_t)port.packet_frame - sink.last_frame) & USB_AUDIO_SINK_FRAME_MASK;
    if (!sink.frame_known || !sink.playing || run <= 1) {
        return;
    }
    loss_last = run - 1;
    loss_gap_us = port.interrupt_gap_us;
    if (loss_last > loss_longest) {
        loss_longest = loss_last;
    }
}

static void receive_stream(const uint8_t *bytes, uint32_t length)
{
    if (receiving) {
        note_loss();
        last_packet_ms = hal_clock_milliseconds();
        finished = false;
        usb_audio_sink_count_frame(&sink, port.packet_frame);
        usb_audio_sink_write(&sink, bytes, length, consumed_frames());
    }
}

static void serve_usb(void)
{
    for (int poll = 0; poll < POLLS_PER_SERVICE; poll++) {
        usb_poll(&port);
    }
}

static void fill_stats(AppDacStats *stats)
{
    *stats = (AppDacStats){
        .missed = sink.stats.missed,
        .dropped = sink.stats.dropped,
        .repeated = sink.stats.repeated,
        .restarts = sink.stats.realigned,
        .starts = output_starts,
        .stops = output_stops,
        .formats = format_changes,
        .control = port.control_entries,
        .wrong_slot = port.wrong_slots,
        .late = port.late_interrupts,
        .loss_last = loss_last,
        .loss_longest = loss_longest,
        .loss_gap_us = loss_gap_us,
    };
}

static void show_state(void)
{
    AppDacStats stats;
    AppDacView view = {.title = TITLE,
                       .rate = audio.rate,
                       .bits = (uint8_t)(BITS_PER_BYTE * usb_audio_sample_bytes(&audio)),
                       .volume = shared->dac_volume};
    if (usb_audio_sample_bytes(&audio) == 0) {
        view.message = i18n_text(STR_USB_CHOOSE_OUTPUT);
    } else if (open_rate == 0) {
        view.message = i18n_text(STR_USB_OUTPUT_FAILED);
    }
    if (stats_shown) {
        fill_stats(&stats);
        view.stats = &stats;
    }
    shown_ms = hal_clock_milliseconds();
    app_show_dac(&view);
}

static void refresh_state(void)
{
    uint32_t every = stats_shown ? STATS_REFRESH_MS : STATE_REFRESH_MS;
    if (hal_clock_milliseconds() - shown_ms >= every) {
        show_state();
    }
}

static void apply_attenuation(void)
{
    uint8_t player = board_audio_stream_attenuation();
    uint8_t host = usb_audio_attenuation(&audio);
    uint32_t total = player + (uint32_t)host;
    bool muted = player == USB_AUDIO_SINK_MUTED || host == USB_AUDIO_MUTED;
    usb_audio_sink_set_attenuation(
        &sink, muted || total >= USB_AUDIO_SINK_MUTED ? USB_AUDIO_SINK_MUTED : (uint8_t)total);
}

static void stop_output(void)
{
    output_stops++;
    bool was_receiving = receiving;
    receiving = false;
    if (playing) {
        board_audio_stream_pause();
        playing = false;
    }
    restart_position();
    usb_audio_sink_start(&sink, audio.rate, usb_audio_sample_bytes(&audio));
    receiving = was_receiving;
}

static void follow_format(void)
{
    format_changes++;
    receiving = false;
    stop_output();
    if (usb_audio_sample_bytes(&audio) != 0 && audio.rate != open_rate) {
        open_rate = board_audio_stream_open(audio.rate, serve_usb) ? audio.rate : 0;
    }
    apply_attenuation();
    receiving = open_rate != 0 && usb_audio_sample_bytes(&audio) != 0;
    show_state();
}

static void follow_host(void)
{
    uint8_t changes = usb_audio_take_changes(&audio);
    if ((changes & USB_AUDIO_CHANGED_VOLUME) != 0) {
        apply_attenuation();
    }
    if ((changes & USB_AUDIO_CHANGED_FORMAT) != 0) {
        follow_format();
    }
}

static void finish_quietly(void)
{
    uint32_t primask = irq_save();
    usb_audio_sink_finish(&sink, consumed_frames());
    finished = true;
    irq_restore(primask);
}

static void keep_output(void)
{
    int32_t quiet_ms = (int32_t)(hal_clock_milliseconds() - last_packet_ms);
    if (playing && quiet_ms > STREAM_IDLE_MS) {
        stop_output();
    } else if (playing && !finished && quiet_ms > STREAM_QUIET_MS) {
        finish_quietly();
    } else if (!playing && receiving && usb_audio_sink_primed(&sink)) {
        output_starts++;
        playing = board_audio_stream_play();
        usb_audio_sink_set_playing(&sink, playing);
    }
}

static void track_output(void)
{
    uint32_t primask = irq_save();
    (void)consumed_frames();
    irq_restore(primask);
}

static void step_volume(int step)
{
    int level = shared->dac_volume + step;
    if (level < 0 || level > HAL_AUDIO_VOLUME_MAX) {
        return;
    }
    shared->dac_volume = (uint8_t)level;
    hal_audio_set_volume(shared->dac_volume);
    apply_attenuation();
    show_state();
}

static void queue_report(uint8_t report)
{
    if (report_count + 2 > REPORT_QUEUE_CAPACITY) {
        return;
    }
    reports[(report_start + report_count++) % REPORT_QUEUE_CAPACITY] = report;
    reports[(report_start + report_count++) % REPORT_QUEUE_CAPACITY] = 0;
}

static void send_reports(void)
{
    if (report_count > 0 && usb_send_report(&port, reports[report_start])) {
        report_start = (uint8_t)((report_start + 1) % REPORT_QUEUE_CAPACITY);
        report_count--;
    }
}

static bool user_left(void)
{
    for (;;) {
        switch (app_dac_next_key()) {
        case APP_DAC_KEY_NONE:
            return false;
        case APP_DAC_KEY_VOLUME_UP:
            step_volume(1);
            break;
        case APP_DAC_KEY_VOLUME_DOWN:
            step_volume(-1);
            break;
        case APP_DAC_KEY_PLAY_PAUSE:
            queue_report(USB_HID_PLAY_PAUSE);
            break;
        case APP_DAC_KEY_NEXT:
            queue_report(USB_HID_NEXT);
            break;
        case APP_DAC_KEY_PREVIOUS:
            queue_report(USB_HID_PREVIOUS);
            break;
        case APP_DAC_KEY_STATS:
            stats_shown = !stats_shown;
            show_state();
            break;
        case APP_DAC_KEY_LEAVE:
            return true;
        }
    }
}

static void serve(void)
{
    app_dac_keys_start();
    while (usb_vbus_present() && !user_left()) {
        serve_usb();
        hal_audio_service();
        board_display_service();
        follow_host();
        track_output();
        keep_output();
        send_reports();
        refresh_state();
    }
    app_dac_keys_stop();
}

static void start_session(void)
{
    uint32_t frames = 0;
    uint32_t *ring = board_audio_stream_ring(&frames);
    usb_audio_sink_init(&sink, ring, frames);
    open_rate = 0;
    receiving = false;
    playing = false;
    report_count = 0;
    stats_shown = false;
    output_starts = 0;
    output_stops = 0;
    format_changes = 0;
    loss_last = 0;
    loss_longest = 0;
    loss_gap_us = 0;
    port = (UsbPort){.link = shared->link,
                     .bulk_packet = USB_PACKET_SIZE,
                     .audio = &audio,
                     .receive_stream = receive_stream};
    usb_audio_init(&audio, &port.device);
    if (!shared->dac_volume_known) {
        shared->dac_volume = (uint8_t)SETTINGS_DEFAULT_DAC_VOLUME;
    }
    hal_audio_set_volume(shared->dac_volume);
}

void usb_dac_session(UsbShared *state)
{
    shared = state;
    app_show_notice(TITLE, i18n_text(STR_USB_COMPUTER_FOUND));
    uint8_t level = board_audio_volume_level();
    start_session();
    uint8_t starting_volume = shared->dac_volume;
    if (usb_wait_for_host(&port, HOST_TIMEOUT_MS)) {
        usb_receive_interrupts_start(&port);
        show_state();
        serve();
        usb_receive_interrupts_stop(&port);
    }
    receiving = false;
    stop_output();
    board_audio_stream_close();
    hal_audio_set_volume(level);
    usb_power_down();
    if (shared->dac_volume != starting_volume) {
        app_remember_dac_volume(shared->dac_volume);
    }
}
