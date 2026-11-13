#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/frame_stats.h"
#include "core/strings.h"

void app_start(void);
void app_step(void);
void app_stop(void);
const FrameStats *app_frame_stats(void);
const char *app_screen_title(void);
int app_screen_depth(void);
void app_show_notice(const char *title, const char *message);
void app_show_usb_notice(const char *title, const char *message);
void app_prepare_notice(const char *title, const char *message);
typedef struct AppDacStats {
    uint32_t missed;
    uint32_t dropped;
    uint32_t repeated;
    uint32_t restarts;
    uint32_t starts;
    uint32_t stops;
    uint32_t formats;
    uint32_t retries;
    uint32_t control;
    uint32_t wrong_slot;
    uint32_t late;
    uint32_t loss_last;
    uint32_t loss_longest;
    uint32_t loss_gap_us;
} AppDacStats;

typedef struct AppDacView {
    const char *title;
    const char *message;
    uint32_t rate;
    uint8_t bits;
    uint8_t volume;
    const AppDacStats *stats;
} AppDacView;

void app_show_dac(const AppDacView *view);
void app_wake(void);
void app_release_audio(void);
void app_remember_dac_volume(uint8_t level);

typedef enum AppDacKey {
    APP_DAC_KEY_NONE,
    APP_DAC_KEY_VOLUME_UP,
    APP_DAC_KEY_VOLUME_DOWN,
    APP_DAC_KEY_PLAY_PAUSE,
    APP_DAC_KEY_NEXT,
    APP_DAC_KEY_PREVIOUS,
    APP_DAC_KEY_STATS,
    APP_DAC_KEY_LEAVE
} AppDacKey;

void app_dac_keys_start(void);
AppDacKey app_dac_next_key(void);
void app_dac_keys_stop(void);
void app_choice_show(const char *title, const StringId *options, int count, int current);
int app_choice_step(void);
void app_choice_close(void);
void app_request_rescan(void);
void app_request_rescan_if_music_changed(void);
void app_show_splash(void);
bool app_charging(void);
bool app_idle(void);
