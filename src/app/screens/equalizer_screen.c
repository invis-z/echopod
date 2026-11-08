#include "equalizer_screen.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/resident.h"
#include "core/string_builder.h"
#include "core/utf8.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/system.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"
#include "ui/navigation.h"

enum {
    SUMMARY_CHARACTERS = 7
};

static const char CHECK_MARK[] = "\xE2\x9C\x93";

const char *equalizer_screen_preset_name(EqualizerPreset preset)
{
    return preset == EQUALIZER_PRESET_OFF ? i18n_text(STR_OFF) : "";
}

static EqualizerChoice *choice_of(const EqualizerConfig *config)
{
    return &config->store->choice;
}

RIPPL_CODE2 static bool too_wide(void)
{
    return hal_audio_status().format.bits_per_sample > EQUALIZER_MAX_BITS;
}

RIPPL_CODE2 static bool above_equalizer_rate(void)
{
    return hal_audio_status().format.sample_rate > EQUALIZER_MAX_RATE || too_wide();
}

RIPPL_CODE2 static StringId bypass_hint(void)
{
    return too_wide() ? STR_SETTINGS_EQ_WIDE_HINT : STR_SETTINGS_EQ_BYPASS_HINT;
}

RIPPL_CODE2 static ScreenAction leave(const EqualizerConfig *config, ScreenAction action)
{
    bool leaving = action.type == SCREEN_ACTION_POP || action.type == SCREEN_ACTION_POP_TO_ROOT;
    if (leaving && !equalizer_store_save(config->store)) {
        toast_show(config->toast, i18n_text(STR_SETTINGS_EQ_SAVE_FAILED), hal_clock_milliseconds());
    }
    return action;
}

RIPPL_CODE2 static void use_preset(const EqualizerConfig *config, EqualizerPreset preset)
{
    EqualizerChoice *choice = choice_of(config);
    if (preset == EQUALIZER_PRESET_FILE) {
        return;
    }
    if (choice->preset != preset) {
        choice->preset = preset;
        equalizer_store_apply(config->store);
    }
}

static bool preview_file(EqualizerScreen *equalizer, int index);
static bool same_name(const char *first, const char *second);

RIPPL_CODE2 static bool file_is(const EqualizerScreen *equalizer, int file, const char *name)
{
    if (file < 0 || file >= equalizer->files.count) {
        return false;
    }
    char full[EQUALIZER_PRESET_NAME_CAPACITY];
    equalizer_files_name(&equalizer->files, file, full);
    return same_name(full, name);
}

RIPPL_CODE2 static int preset_count(const void *context)
{
    const EqualizerScreen *equalizer = context;
    return EQUALIZER_PRESET_FILE + equalizer->files.count;
}

RIPPL_CODE2 static const char *preset_label(const void *context, int index)
{
    const EqualizerScreen *equalizer = context;
    if (index < EQUALIZER_PRESET_FILE) {
        return i18n_text(STR_OFF);
    }
    int file = index - EQUALIZER_PRESET_FILE;
    return file < equalizer->files.count ? equalizer->files.names[file] : "";
}

RIPPL_CODE2 static const char *preset_mark(const void *context, int index)
{
    const EqualizerScreen *equalizer = context;
    const EqualizerChoice *choice = choice_of(&equalizer->config);
    if (index < EQUALIZER_PRESET_FILE) {
        return (int)choice->preset == index ? CHECK_MARK : NULL;
    }
    int file = index - EQUALIZER_PRESET_FILE;
    return choice->preset == EQUALIZER_PRESET_FILE && file_is(equalizer, file, choice->preset_name)
               ? CHECK_MARK
               : NULL;
}

RIPPL_CODE2 static ScreenAction choose_preset(void *context, int index)
{
    EqualizerScreen *equalizer = context;
    if (index >= EQUALIZER_PRESET_FILE) {
        if (!preview_file(equalizer, index)) {
            return screen_action_none();
        }
        equalizer->file_preview = index;
    }
    return screen_action_pop();
}

RIPPL_CODE2 static ScreenAction handle_picker_key(void *context, KeyEvent event, uint32_t now_ms)
{
    EqualizerScreen *equalizer = context;
    ListSelection selection = {.select = choose_preset, .hold = NULL, .context = equalizer};
    NavigationIntent intent = navigation_from_key(event).intent;
    ScreenAction action = list_navigation_handle(&equalizer->list, event, now_ms, selection);
    int selected = equalizer->list.selected;
    if (selected < EQUALIZER_PRESET_FILE) {
        use_preset(&equalizer->config, (EqualizerPreset)selected);
    } else if ((intent == NAVIGATION_NEXT || intent == NAVIGATION_PREVIOUS) &&
               selected != equalizer->file_preview) {
        equalizer->file_preview = selected;
        preview_file(equalizer, selected);
    }
    return leave(&equalizer->config, action);
}

RIPPL_CODE2 static bool same_name(const char *first, const char *second)
{
    uint32_t index = 0;
    while (first[index] != '\0' && first[index] == second[index]) {
        index++;
    }
    return first[index] == second[index];
}

typedef struct ProblemText {
    uint8_t problem;
    uint8_t has_line;
    uint16_t text;
} ProblemText;

static const ProblemText PROBLEM_TEXTS[] = {
    {EQUALIZER_TEXT_TOO_LARGE, 0, STR_SETTINGS_EQ_FILE_TOO_BIG},
    {EQUALIZER_TEXT_EMPTY, 0, STR_SETTINGS_EQ_FILE_EMPTY},
    {EQUALIZER_TEXT_TOO_MANY_FILTERS, 0, STR_SETTINGS_EQ_TOO_MANY},
    {EQUALIZER_TEXT_GAIN, 1, STR_SETTINGS_EQ_GAIN_ERROR},
    {EQUALIZER_TEXT_Q, 1, STR_SETTINGS_EQ_Q_ERROR},
    {EQUALIZER_TEXT_FREQUENCY, 1, STR_SETTINGS_EQ_RANGE_ERROR},
    {EQUALIZER_TEXT_PREAMP, 1, STR_SETTINGS_EQ_RANGE_ERROR},
};

RIPPL_CODE2 static void show_preset_error(EqualizerScreen *equalizer, EqualizerTextError error)
{
    StringId text = error.line > 0 ? STR_SETTINGS_EQ_PRESET_ERROR : STR_SETTINGS_EQ_PRESET_FAILED;
    for (size_t index = 0; index < sizeof PROBLEM_TEXTS / sizeof PROBLEM_TEXTS[0]; index++) {
        if (PROBLEM_TEXTS[index].problem == error.problem &&
            (PROBLEM_TEXTS[index].has_line == 0 || error.line > 0)) {
            text = (StringId)PROBLEM_TEXTS[index].text;
        }
    }
    char line[16];
    StringBuilder line_text = string_builder_make(line, sizeof line);
    string_builder_append_unsigned(&line_text, error.line);
    const char *arguments[] = {line};
    StringBuilder notice = string_builder_make(equalizer->notice, sizeof equalizer->notice);
    i18n_format(&notice, text, arguments, 1);
    toast_show(equalizer->config.toast, equalizer->notice, equalizer->now_ms);
}

RIPPL_CODE2 static void show_skipped(EqualizerScreen *equalizer, uint32_t skipped)
{
    StringBuilder notice = string_builder_make(equalizer->notice, sizeof equalizer->notice);
    if (skipped == 1) {
        string_builder_append(&notice, i18n_text(STR_SETTINGS_EQ_SKIPPED_ONE));
    } else {
        char count[16];
        StringBuilder count_text = string_builder_make(count, sizeof count);
        string_builder_append_unsigned(&count_text, skipped);
        const char *arguments[] = {count};
        i18n_format(&notice, STR_SETTINGS_EQ_SKIPPED, arguments, 1);
    }
    toast_show(equalizer->config.toast, equalizer->notice, equalizer->now_ms);
}

RIPPL_CODE2 static void enter_picker(void *context, uint32_t now_ms)
{
    EqualizerScreen *equalizer = context;
    equalizer->now_ms = now_ms;
    const EqualizerChoice *choice = choice_of(&equalizer->config);
    int selected = (int)choice->preset;
    if (choice->preset == EQUALIZER_PRESET_FILE) {
        selected = EQUALIZER_PRESET_FILE;
        for (int index = 0; index < equalizer->files.count; index++) {
            if (file_is(equalizer, index, choice->preset_name)) {
                selected = EQUALIZER_PRESET_FILE + index;
            }
        }
    }
    list_view_select(&equalizer->list, selected, now_ms);
    equalizer->file_preview = selected >= EQUALIZER_PRESET_FILE ? selected : -1;
    if (above_equalizer_rate()) {
        toast_show(equalizer->config.toast, i18n_text(bypass_hint()), now_ms);
    }
}

RIPPL_CODE2 static void update_picker(void *context, uint32_t now_ms)
{
    EqualizerScreen *equalizer = context;
    equalizer->now_ms = now_ms;
}

RIPPL_CODE2 static void draw_picker(void *context, Canvas *canvas)
{
    const EqualizerScreen *equalizer = context;
    const Theme *theme = equalizer->config.theme;
    header_draw(canvas, theme, equalizer->screen.title);
    list_view_draw(&equalizer->list, canvas, theme_content_area(theme), equalizer->now_ms);
}

RIPPL_CODE2 static bool preview_file(EqualizerScreen *equalizer, int index)
{
    int file = index - EQUALIZER_PRESET_FILE;
    if (file < 0 || file >= equalizer->files.count) {
        return false;
    }
    char name[EQUALIZER_PRESET_NAME_CAPACITY];
    equalizer_files_name(&equalizer->files, file, name);
    EqualizerChoice *choice = choice_of(&equalizer->config);
    if (choice->preset == EQUALIZER_PRESET_FILE && same_name(choice->preset_name, name)) {
        return true;
    }
    EqualizerTextError error;
    if (!equalizer_store_select_file(equalizer->config.store, name, &error)) {
        show_preset_error(equalizer, error);
        return false;
    }
    if (error.skipped > 0) {
        show_skipped(equalizer, error.skipped);
    }
    return true;
}

Screen *equalizer_screen_init(EqualizerScreen *equalizer, EqualizerConfig config)
{
    *equalizer = (EqualizerScreen){.config = config};
    equalizer->model = (ListModel){
        .context = equalizer, .count = preset_count, .label = preset_label, .detail = preset_mark};
    list_view_init(&equalizer->list, &equalizer->model, config.theme, 0);
    equalizer->screen = (Screen){
        .title = i18n_text(STR_SETTINGS_EQUALIZER),
        .context = equalizer,
        .enter = enter_picker,
        .handle_key = handle_picker_key,
        .update = update_picker,
        .draw = draw_picker,
    };
    return &equalizer->screen;
}

void equalizer_screen_refresh(EqualizerScreen *equalizer)
{
    equalizer_files_scan(&equalizer->files);
}

bool equalizer_screen_has_presets(const void *context)
{
    const EqualizerScreen *equalizer = context;
    return equalizer->files.count > 0;
}

ScreenAction equalizer_screen_open(void *context)
{
    EqualizerScreen *equalizer = context;
    uint32_t now_ms = hal_clock_milliseconds();
    if (!hal_system_code2_ready()) {
        toast_show(equalizer->config.toast, i18n_text(STR_CODE2_MISSING), now_ms);
        return screen_action_none();
    }
    equalizer_screen_refresh(equalizer);
    if (!equalizer_screen_has_presets(equalizer)) {
        toast_show(equalizer->config.toast, i18n_text(STR_SETTINGS_EQ_NO_PRESETS), now_ms);
        return screen_action_none();
    }
    return screen_action_push(&equalizer->screen);
}

const char *equalizer_screen_summary(const void *context)
{
    const EqualizerScreen *equalizer = context;
    const EqualizerChoice *choice = choice_of(&equalizer->config);
    if (choice->preset != EQUALIZER_PRESET_FILE) {
        return equalizer_screen_preset_name(choice->preset);
    }
    static char summary[SUMMARY_CHARACTERS + 1];
    size_t length = 0;
    size_t stem = equalizer_files_stem_length(choice->preset_name);
    while (length < SUMMARY_CHARACTERS && length < stem) {
        length++;
    }
    length = utf8_complete_length(choice->preset_name, length);
    for (size_t index = 0; index < length; index++) {
        summary[index] = choice->preset_name[index];
    }
    summary[length] = '\0';
    return summary;
}
