#include "usb_audio2.h"

#include <stddef.h>

enum {
    TYPE_STANDARD_DEVICE_IN = 0x80,
    REQUEST_GET_DESCRIPTOR = 6,
    REQUEST_CUR = 0x01,
    REQUEST_RANGE = 0x02,
    DESCRIPTOR_DEVICE = 1,
    DESCRIPTOR_CONFIGURATION = 2,
    DESCRIPTOR_DEVICE_QUALIFIER = 6,
    DESCRIPTOR_OTHER_SPEED_CONFIGURATION = 7,
    CONTROL_INTERFACE = 0,
    CLOCK_ID = 0x10,
    FEATURE_UNIT_ID = 2,
    SELECTOR_FREQUENCY = 1,
    SELECTOR_CLOCK_VALID = 2,
    SELECTOR_MUTE = 1,
    SELECTOR_VOLUME = 2,
    VOLUME_MIN = -16384,
    VOLUME_MAX = 0,
    VOLUME_RESOLUTION = 128,
    HIGH_SPEED_INTERVALS = 8000,
    FULL_SPEED_INTERVALS = 1000,
    FULL_SPEED_TOP_RATE = 96000,
    HIGH_SPEED_FEEDBACK_INTERVAL = 4,
    HIGH_SPEED_HID_INTERVAL = 7,
    HIGH_SPEED_FEEDBACK_BYTES = 4,
    FULL_SPEED_FEEDBACK_BYTES = 3,
    RANGE_ENTRY_BYTES = 12
};

static const uint32_t RATES[] = {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};

static const uint8_t DEVICE_DESCRIPTOR[] = {
    18,   1,    0x00, 0x02, 0xEF, 0x02, 0x01, USB_PACKET_SIZE, 0x09, 0x12, 0x03,
    0x00, 0x00, 0x01, 1,    2,    0,    1};

static const uint8_t DEVICE_QUALIFIER[] = {10, 6, 0x00, 0x02, 0xEF, 0x02, 0x01, USB_PACKET_SIZE,
                                           1,  0};

static const uint8_t HEAD[] = {9,
                               2,
                               USB_AUDIO2_CONFIGURATION_LENGTH & 0xFF,
                               USB_AUDIO2_CONFIGURATION_LENGTH >> 8,
                               3,
                               1,
                               0,
                               0x80,
                               50,
                               8,
                               0x0B,
                               0,
                               2,
                               1,
                               0,
                               0x20,
                               0,
                               9,
                               4,
                               CONTROL_INTERFACE,
                               0,
                               0,
                               1,
                               1,
                               0x20,
                               0,
                               9,
                               0x24,
                               1,
                               0x00,
                               0x02,
                               0x01,
                               64,
                               0,
                               0,
                               8,
                               0x24,
                               0x0A,
                               CLOCK_ID,
                               0x03,
                               0x07,
                               0,
                               0,
                               17,
                               0x24,
                               0x02,
                               1,
                               0x01,
                               0x01,
                               0,
                               CLOCK_ID,
                               2,
                               0x03,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               18,
                               0x24,
                               0x06,
                               FEATURE_UNIT_ID,
                               1,
                               0x0F,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               12,
                               0x24,
                               0x03,
                               3,
                               0x02,
                               0x03,
                               0,
                               FEATURE_UNIT_ID,
                               CLOCK_ID,
                               0,
                               0,
                               0,
                               9,
                               4,
                               1,
                               0,
                               0,
                               1,
                               2,
                               0x20,
                               0};

static const uint8_t SETTING[] = {9,
                                  4,
                                  1,
                                  0,
                                  2,
                                  1,
                                  2,
                                  0x20,
                                  0,
                                  16,
                                  0x24,
                                  0x01,
                                  1,
                                  0,
                                  1,
                                  0x01,
                                  0,
                                  0,
                                  0,
                                  2,
                                  0x03,
                                  0,
                                  0,
                                  0,
                                  0,
                                  6,
                                  0x24,
                                  0x02,
                                  1,
                                  0,
                                  0,
                                  7,
                                  5,
                                  USB_AUDIO_ENDPOINT,
                                  0x05,
                                  0,
                                  0,
                                  1,
                                  8,
                                  0x25,
                                  0x01,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  7,
                                  5,
                                  USB_AUDIO_FEEDBACK_ENDPOINT,
                                  0x11,
                                  0,
                                  0,
                                  1};

static const uint8_t TAIL[] = {9,
                               4,
                               USB_HID_INTERFACE,
                               0,
                               1,
                               3,
                               0,
                               0,
                               0,
                               9,
                               0x21,
                               0x11,
                               0x01,
                               0,
                               1,
                               0x22,
                               29,
                               0,
                               7,
                               5,
                               USB_HID_ENDPOINT,
                               3,
                               USB_HID_PACKET_SIZE,
                               0,
                               0};

enum {
    SETTING_ALT = 3,
    SETTING_SUBSLOT = 9 + 16 + 4,
    SETTING_BITS = 9 + 16 + 5,
    SETTING_DATA_PACKET = 9 + 16 + 6 + 4,
    SETTING_FEEDBACK_PACKET = 9 + 16 + 6 + 7 + 8 + 4,
    SETTING_FEEDBACK_INTERVAL = 9 + 16 + 6 + 7 + 8 + 6,
    TAIL_HID_INTERVAL = 9 + 9 + 6
};

_Static_assert(sizeof HEAD + USB_AUDIO2_LAST_ALT * sizeof SETTING + sizeof TAIL ==
                   USB_AUDIO2_CONFIGURATION_LENGTH,
               "the configuration length matches its parts");

static UsbReply stall(void)
{
    return (UsbReply){USB_REPLY_STALL, NULL, 0, USB_ACTION_NONE, 0};
}

static UsbReply status(void)
{
    return (UsbReply){USB_REPLY_STATUS, NULL, 0, USB_ACTION_NONE, 0};
}

static UsbReply data_reply(const uint8_t *bytes, uint16_t length, UsbSetup setup)
{
    uint16_t sent = length < setup.length ? length : setup.length;
    return (UsbReply){USB_REPLY_DATA, bytes, sent, USB_ACTION_NONE, 0};
}

static void put_little_endian(uint8_t *bytes, uint32_t value, uint16_t length)
{
    for (uint16_t index = 0; index < length; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

uint8_t usb_audio2_sample_bytes(uint8_t alt)
{
    return alt == 0 ? 0 : alt == 1 ? 2 : 4;
}

uint8_t usb_audio2_bits(uint8_t alt)
{
    return (uint8_t)(alt == 0 ? 0 : 8 + 8 * alt);
}

static uint32_t top_rate(UsbSpeed speed)
{
    return speed == USB_SPEED_HIGH ? RATES[sizeof RATES / sizeof RATES[0] - 1]
                                   : FULL_SPEED_TOP_RATE;
}

uint16_t usb_audio2_packet(UsbSpeed speed, uint8_t alt)
{
    uint32_t intervals = speed == USB_SPEED_HIGH ? HIGH_SPEED_INTERVALS : FULL_SPEED_INTERVALS;
    uint32_t frames = (top_rate(speed) + intervals - 1) / intervals + 1;
    return (uint16_t)(frames * 2u * usb_audio2_sample_bytes(alt));
}

static uint8_t *append(uint8_t *at, const uint8_t *bytes, size_t length)
{
    for (size_t index = 0; index < length; index++) {
        at[index] = bytes[index];
    }
    return at + length;
}

static UsbReply configuration(UsbAudio *audio, uint8_t type, UsbSpeed speed, UsbSetup setup)
{
    bool high = speed == USB_SPEED_HIGH;
    uint8_t *at = append(audio->configuration, HEAD, sizeof HEAD);
    audio->configuration[1] = type;
    for (uint8_t alt = 1; alt <= USB_AUDIO2_LAST_ALT; alt++) {
        uint8_t *setting = at;
        at = append(at, SETTING, sizeof SETTING);
        setting[SETTING_ALT] = alt;
        setting[SETTING_SUBSLOT] = usb_audio2_sample_bytes(alt);
        setting[SETTING_BITS] = usb_audio2_bits(alt);
        put_little_endian(setting + SETTING_DATA_PACKET, usb_audio2_packet(speed, alt), 2);
        setting[SETTING_FEEDBACK_PACKET] =
            high ? HIGH_SPEED_FEEDBACK_BYTES : FULL_SPEED_FEEDBACK_BYTES;
        setting[SETTING_FEEDBACK_INTERVAL] = high ? HIGH_SPEED_FEEDBACK_INTERVAL : 1;
    }
    uint8_t *tail = at;
    append(at, TAIL, sizeof TAIL);
    tail[TAIL_HID_INTERVAL] = high ? HIGH_SPEED_HID_INTERVAL : USB_HID_INTERVAL_MS;
    return data_reply(audio->configuration, USB_AUDIO2_CONFIGURATION_LENGTH, setup);
}

bool usb_audio2_owns_descriptor(UsbSetup setup)
{
    uint8_t type = (uint8_t)(setup.value >> 8);
    return setup.request_type == TYPE_STANDARD_DEVICE_IN &&
           setup.request == REQUEST_GET_DESCRIPTOR &&
           (type == DESCRIPTOR_DEVICE || type == DESCRIPTOR_CONFIGURATION ||
            type == DESCRIPTOR_DEVICE_QUALIFIER || type == DESCRIPTOR_OTHER_SPEED_CONFIGURATION);
}

UsbReply usb_audio2_descriptor(UsbAudio *audio, UsbSetup setup)
{
    UsbSpeed speed = audio->device->speed;
    switch (setup.value >> 8) {
    case DESCRIPTOR_DEVICE:
        return data_reply(DEVICE_DESCRIPTOR, sizeof DEVICE_DESCRIPTOR, setup);
    case DESCRIPTOR_DEVICE_QUALIFIER:
        return data_reply(DEVICE_QUALIFIER, sizeof DEVICE_QUALIFIER, setup);
    case DESCRIPTOR_OTHER_SPEED_CONFIGURATION:
        return configuration(audio, DESCRIPTOR_OTHER_SPEED_CONFIGURATION,
                             speed == USB_SPEED_HIGH ? USB_SPEED_FULL : USB_SPEED_HIGH, setup);
    default:
        return configuration(audio, DESCRIPTOR_CONFIGURATION, speed, setup);
    }
}

typedef enum Control2 {
    CONTROL2_NONE,
    CONTROL2_FREQUENCY,
    CONTROL2_CLOCK_VALID,
    CONTROL2_MUTE,
    CONTROL2_VOLUME
} Control2;

static Control2 control_of(UsbSetup setup)
{
    uint8_t entity = (uint8_t)(setup.index >> 8);
    uint8_t selector = (uint8_t)(setup.value >> 8);
    uint8_t channel = (uint8_t)setup.value;
    if ((setup.index & 0xFF) != CONTROL_INTERFACE || channel != 0) {
        return CONTROL2_NONE;
    }
    if (entity == CLOCK_ID) {
        return selector == SELECTOR_FREQUENCY     ? CONTROL2_FREQUENCY
               : selector == SELECTOR_CLOCK_VALID ? CONTROL2_CLOCK_VALID
                                                  : CONTROL2_NONE;
    }
    if (entity == FEATURE_UNIT_ID) {
        return selector == SELECTOR_MUTE     ? CONTROL2_MUTE
               : selector == SELECTOR_VOLUME ? CONTROL2_VOLUME
                                             : CONTROL2_NONE;
    }
    return CONTROL2_NONE;
}

static bool rate_offered(UsbSpeed speed, uint32_t rate)
{
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        if (RATES[index] == rate) {
            return rate <= top_rate(speed);
        }
    }
    return false;
}

static UsbReply rate_range(UsbAudio *audio, UsbSetup setup)
{
    uint16_t count = 0;
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        if (rate_offered(audio->device->speed, RATES[index])) {
            uint8_t *entry = audio->range + 2 + count * RANGE_ENTRY_BYTES;
            put_little_endian(entry, RATES[index], 4);
            put_little_endian(entry + 4, RATES[index], 4);
            put_little_endian(entry + 8, 0, 4);
            count++;
        }
    }
    put_little_endian(audio->range, count, 2);
    return data_reply(audio->range, (uint16_t)(2 + count * RANGE_ENTRY_BYTES), setup);
}

static UsbReply volume_range(UsbAudio *audio, UsbSetup setup)
{
    put_little_endian(audio->range, 1, 2);
    put_little_endian(audio->range + 2, (uint16_t)VOLUME_MIN, 2);
    put_little_endian(audio->range + 4, VOLUME_MAX, 2);
    put_little_endian(audio->range + 6, VOLUME_RESOLUTION, 2);
    return data_reply(audio->range, 8, setup);
}

static UsbReply current(UsbAudio *audio, uint32_t value, uint16_t length, UsbSetup setup)
{
    put_little_endian(audio->reply, value, length);
    return data_reply(audio->reply, length, setup);
}

UsbReply usb_audio2_class_in(UsbAudio *audio, UsbSetup setup)
{
    Control2 control = control_of(setup);
    if (setup.request == REQUEST_RANGE) {
        return control == CONTROL2_FREQUENCY ? rate_range(audio, setup)
               : control == CONTROL2_VOLUME  ? volume_range(audio, setup)
                                             : stall();
    }
    if (setup.request != REQUEST_CUR) {
        return stall();
    }
    switch (control) {
    case CONTROL2_FREQUENCY:
        return current(audio, audio->rate, 4, setup);
    case CONTROL2_CLOCK_VALID:
        return current(audio, 1, 1, setup);
    case CONTROL2_MUTE:
        return current(audio, audio->muted, 1, setup);
    case CONTROL2_VOLUME:
        return current(audio, (uint16_t)audio->volume, 2, setup);
    default:
        return stall();
    }
}

static bool writable(Control2 control)
{
    return control == CONTROL2_FREQUENCY || control == CONTROL2_MUTE || control == CONTROL2_VOLUME;
}

UsbReply usb_audio2_class_out(UsbSetup setup)
{
    bool known = setup.request == REQUEST_CUR && setup.length > 0 && writable(control_of(setup));
    return known ? (UsbReply){USB_REPLY_RECEIVE, NULL, setup.length, USB_ACTION_NONE, 0} : stall();
}

static uint32_t read_little_endian(const uint8_t *data, uint16_t length)
{
    uint32_t value = 0;
    for (uint16_t index = length; index > 0; index--) {
        value = value << 8 | data[index - 1];
    }
    return value;
}

UsbReply usb_audio2_receive(UsbAudio *audio, UsbSetup setup, const uint8_t *data, uint16_t length)
{
    Control2 control = control_of(setup);
    uint16_t needed = (uint16_t)(control == CONTROL2_FREQUENCY ? 4
                                 : control == CONTROL2_VOLUME  ? 2
                                                               : 1);
    if (!writable(control) || length < needed) {
        return stall();
    }
    uint32_t value = read_little_endian(data, needed);
    if (control == CONTROL2_FREQUENCY) {
        if (!rate_offered(audio->device->speed, value)) {
            return stall();
        }
        if (audio->rate != value) {
            audio->rate = value;
            audio->changes |= USB_AUDIO_CHANGED_FORMAT;
        }
        return status();
    }
    if (control == CONTROL2_MUTE) {
        audio->muted = value != 0;
    } else {
        int32_t volume = (int16_t)value;
        audio->volume = (int16_t)(volume < VOLUME_MIN   ? VOLUME_MIN
                                  : volume > VOLUME_MAX ? VOLUME_MAX
                                                        : volume);
    }
    audio->changes |= USB_AUDIO_CHANGED_VOLUME;
    return status();
}
