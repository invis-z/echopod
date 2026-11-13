#include <stddef.h>

#include "check.h"
#include "device/usb_audio.h"

enum {
    CONFIGURATION_LENGTH = 283,
    CLOCK = 0x1000,
    FEATURE = 0x0200,
    FREQUENCY = 0x0100,
    CLOCK_VALID = 0x0200,
    MUTE = 0x0100,
    VOLUME = 0x0200
};

static UsbDevice device;

static UsbSetup setup(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                      uint16_t length)
{
    return (UsbSetup){type, request, value, index, length};
}

static UsbAudio ready_audio(UsbSpeed speed)
{
    UsbAudio audio;
    usb_audio_init_high_resolution(&audio, &device);
    usb_device_set_speed(&device, speed);
    return audio;
}

static UsbReply descriptor(UsbAudio *audio, uint16_t value)
{
    return usb_audio_setup(audio, setup(0x80, 6, value, 0, 512));
}

static const uint8_t *find_descriptor(UsbReply reply, uint8_t type, uint8_t subtype, int occurrence)
{
    for (uint16_t offset = 0; offset < reply.length; offset += reply.data[offset]) {
        const uint8_t *bytes = reply.data + offset;
        if (bytes[1] == type && (subtype == 0 || bytes[2] == subtype) && occurrence-- == 0) {
            return bytes;
        }
    }
    return NULL;
}

static uint32_t little_endian(const uint8_t *bytes, int length)
{
    uint32_t value = 0;
    for (int index = length - 1; index >= 0; index--) {
        value = value << 8 | bytes[index];
    }
    return value;
}

static void set_cur(UsbAudio *audio, UsbSetup request, const uint8_t *bytes, uint16_t length)
{
    CHECK_EQUAL(USB_REPLY_RECEIVE, usb_audio_setup(audio, request).kind);
    CHECK_EQUAL(USB_REPLY_STATUS, usb_audio_receive(audio, request, bytes, length).kind);
}

static void the_device_is_a_usb2_composite_with_its_own_product(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    UsbReply reply = descriptor(&audio, 0x0100);
    CHECK_EQUAL(18, reply.length);
    CHECK_EQUAL(0x0200, little_endian(reply.data + 2, 2));
    CHECK_EQUAL(0xEF, reply.data[4]);
    CHECK_EQUAL(0x02, reply.data[5]);
    CHECK_EQUAL(0x01, reply.data[6]);
    CHECK_EQUAL(0x1209, little_endian(reply.data + 8, 2));
    CHECK_EQUAL(0x0003, little_endian(reply.data + 10, 2));
    reply = descriptor(&audio, 0x0600);
    CHECK_EQUAL(10, reply.length);
    CHECK_EQUAL(0xEF, reply.data[4]);
}

static void both_speed_configurations_walk_to_their_length(void)
{
    for (int speed = 0; speed < 2; speed++) {
        UsbAudio audio = ready_audio(speed == 0 ? USB_SPEED_FULL : USB_SPEED_HIGH);
        for (int kind = 0; kind < 2; kind++) {
            UsbReply reply = descriptor(&audio, kind == 0 ? 0x0200 : 0x0700);
            CHECK_EQUAL(CONFIGURATION_LENGTH, reply.length);
            CHECK_EQUAL(kind == 0 ? 2 : 7, reply.data[1]);
            CHECK_EQUAL(CONFIGURATION_LENGTH, little_endian(reply.data + 2, 2));
            uint16_t offset = 0;
            while (offset < reply.length && reply.data[offset] > 0) {
                offset += reply.data[offset];
            }
            CHECK_EQUAL(CONFIGURATION_LENGTH, offset);
        }
    }
}

static void the_control_header_covers_its_units(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    UsbReply reply = descriptor(&audio, 0x0200);
    const uint8_t *header = find_descriptor(reply, 0x24, 1, 0);
    CHECK_EQUAL(0x0200, little_endian(header + 3, 2));
    uint16_t total = 0;
    for (const uint8_t *unit = header; unit[1] == 0x24; unit += unit[0]) {
        total += unit[0];
    }
    CHECK_EQUAL(little_endian(header + 6, 2), total);
    const uint8_t *clock = find_descriptor(reply, 0x24, 0x0A, 0);
    CHECK_EQUAL(0x10, clock[3]);
    CHECK_EQUAL(0x07, clock[5]);
}

static void three_settings_carry_sixteen_twenty_four_and_thirty_two_bits(void)
{
    static const uint8_t SUBSLOTS[] = {2, 4, 4};
    static const uint8_t BITS[] = {16, 24, 32};
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    UsbReply reply = descriptor(&audio, 0x0200);
    for (int setting = 0; setting < 3; setting++) {
        const uint8_t *format = find_descriptor(reply, 0x24, 0x02, setting + 1);
        CHECK(format != NULL);
        CHECK_EQUAL(6, format[0]);
        CHECK_EQUAL(SUBSLOTS[setting], format[4]);
        CHECK_EQUAL(BITS[setting], format[5]);
    }
}

static void high_speed_endpoints_fit_a_microframe_of_384_kilohertz(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    UsbReply reply = descriptor(&audio, 0x0200);
    for (int setting = 0; setting < 3; setting++) {
        const uint8_t *data = find_descriptor(reply, 5, 0, 2 * setting);
        const uint8_t *feedback = find_descriptor(reply, 5, 0, 2 * setting + 1);
        CHECK_EQUAL(USB_AUDIO_ENDPOINT, data[2]);
        CHECK_EQUAL(0x05, data[3]);
        CHECK_EQUAL(49 * 2 * (setting == 0 ? 2 : 4), little_endian(data + 4, 2));
        CHECK_EQUAL(1, data[6]);
        CHECK_EQUAL(USB_AUDIO_FEEDBACK_ENDPOINT, feedback[2]);
        CHECK_EQUAL(0x11, feedback[3]);
        CHECK_EQUAL(4, little_endian(feedback + 4, 2));
        CHECK_EQUAL(4, feedback[6]);
    }
    CHECK_EQUAL(USB_AUDIO2_HIGH_SPEED_MAX_PACKET, 49 * 8);
}

static void full_speed_endpoints_fit_a_frame_of_96_kilohertz(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_FULL);
    UsbReply reply = descriptor(&audio, 0x0200);
    const uint8_t *data = find_descriptor(reply, 5, 0, 4);
    const uint8_t *feedback = find_descriptor(reply, 5, 0, 5);
    CHECK_EQUAL(97 * 8, little_endian(data + 4, 2));
    CHECK_EQUAL(USB_AUDIO2_MAX_PACKET, 97 * 8);
    CHECK_EQUAL(3, little_endian(feedback + 4, 2));
    CHECK_EQUAL(1, feedback[6]);
    const uint8_t *hid = find_descriptor(reply, 5, 0, 6);
    CHECK_EQUAL(USB_HID_ENDPOINT, hid[2]);
    CHECK_EQUAL(USB_HID_INTERVAL_MS, hid[6]);
}

static void the_other_speed_configuration_describes_the_other_speed(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_FULL);
    UsbReply reply = descriptor(&audio, 0x0700);
    const uint8_t *data = find_descriptor(reply, 5, 0, 4);
    CHECK_EQUAL(49 * 8, little_endian(data + 4, 2));
}

static void the_stream_packet_follows_the_setting_and_speed(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    usb_audio_setup(&audio, setup(0x01, 11, 1, 1, 0));
    CHECK_EQUAL(196, usb_audio_stream_packet(&audio));
    CHECK_EQUAL(2, usb_audio_sample_bytes(&audio));
    CHECK_EQUAL(16, usb_audio_bits(&audio));
    usb_audio_setup(&audio, setup(0x01, 11, 2, 1, 0));
    CHECK_EQUAL(392, usb_audio_stream_packet(&audio));
    CHECK_EQUAL(4, usb_audio_sample_bytes(&audio));
    CHECK_EQUAL(24, usb_audio_bits(&audio));
    UsbReply reply = usb_audio_setup(&audio, setup(0x01, 11, 3, 1, 0));
    CHECK_EQUAL(USB_ACTION_STREAM, reply.action);
    CHECK_EQUAL(32, usb_audio_bits(&audio));
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(&audio, setup(0x01, 11, 4, 1, 0)).kind);
}

static void high_speed_offers_eight_rates_up_to_384_kilohertz(void)
{
    static const uint32_t RATES[] = {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    UsbReply reply = usb_audio_setup(&audio, setup(0xA1, 0x02, FREQUENCY, CLOCK, 2));
    CHECK_EQUAL(2, reply.length);
    CHECK_EQUAL(8, little_endian(reply.data, 2));
    reply = usb_audio_setup(&audio, setup(0xA1, 0x02, FREQUENCY, CLOCK, 255));
    CHECK_EQUAL(2 + 8 * 12, reply.length);
    for (int index = 0; index < 8; index++) {
        const uint8_t *entry = reply.data + 2 + 12 * index;
        CHECK_EQUAL(RATES[index], little_endian(entry, 4));
        CHECK_EQUAL(RATES[index], little_endian(entry + 4, 4));
        CHECK_EQUAL(0, little_endian(entry + 8, 4));
    }
}

static void full_speed_stops_at_96_kilohertz(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_FULL);
    UsbReply reply = usb_audio_setup(&audio, setup(0xA1, 0x02, FREQUENCY, CLOCK, 255));
    CHECK_EQUAL(4, little_endian(reply.data, 2));
    CHECK_EQUAL(96000, little_endian(reply.data + 2 + 3 * 12, 4));
    static const uint8_t rate[] = {0x00, 0xEE, 0x02, 0x00};
    UsbSetup request = setup(0x21, 0x01, FREQUENCY, CLOCK, 4);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_receive(&audio, request, rate, 4).kind);
    CHECK_EQUAL(48000, audio.rate);
}

static void the_host_sets_and_reads_the_clock(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    static const uint8_t rate[] = {0x00, 0xDC, 0x05, 0x00};
    set_cur(&audio, setup(0x21, 0x01, FREQUENCY, CLOCK, 4), rate, 4);
    CHECK_EQUAL(384000, audio.rate);
    CHECK_EQUAL(USB_AUDIO_CHANGED_FORMAT, usb_audio_take_changes(&audio));
    UsbReply reply = usb_audio_setup(&audio, setup(0xA1, 0x01, FREQUENCY, CLOCK, 4));
    CHECK_EQUAL(4, reply.length);
    CHECK_EQUAL(384000, little_endian(reply.data, 4));
    reply = usb_audio_setup(&audio, setup(0xA1, 0x01, CLOCK_VALID, CLOCK, 1));
    CHECK_EQUAL(1, reply.length);
    CHECK_EQUAL(1, reply.data[0]);
    set_cur(&audio, setup(0x21, 0x01, FREQUENCY, CLOCK, 4), rate, 4);
    CHECK_EQUAL(0, usb_audio_take_changes(&audio));
}

static void an_unknown_rate_stalls(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    static const uint8_t rate[] = {0x80, 0x3E, 0x00, 0x00};
    UsbSetup request = setup(0x21, 0x01, FREQUENCY, CLOCK, 4);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_receive(&audio, request, rate, 4).kind);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_receive(&audio, request, rate, 3).kind);
    CHECK_EQUAL(48000, audio.rate);
}

static void volume_and_mute_work_in_quarter_decibel_units(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    UsbReply reply = usb_audio_setup(&audio, setup(0xA1, 0x02, VOLUME, FEATURE, 8));
    CHECK_EQUAL(8, reply.length);
    CHECK_EQUAL(1, little_endian(reply.data, 2));
    CHECK_EQUAL(-16384, (int16_t)little_endian(reply.data + 2, 2));
    CHECK_EQUAL(0, little_endian(reply.data + 4, 2));
    CHECK_EQUAL(128, little_endian(reply.data + 6, 2));
    static const uint8_t volume[] = {0x00, 0xF6};
    set_cur(&audio, setup(0x21, 0x01, VOLUME, FEATURE, 2), volume, 2);
    CHECK_EQUAL(20, usb_audio_attenuation(&audio));
    reply = usb_audio_setup(&audio, setup(0xA1, 0x01, VOLUME, FEATURE, 2));
    CHECK_EQUAL(-2560, (int16_t)little_endian(reply.data, 2));
    static const uint8_t on[] = {1};
    set_cur(&audio, setup(0x21, 0x01, MUTE, FEATURE, 1), on, 1);
    CHECK_EQUAL(USB_AUDIO_MUTED, usb_audio_attenuation(&audio));
    CHECK_EQUAL(USB_AUDIO_CHANGED_VOLUME, usb_audio_take_changes(&audio));
    static const uint8_t below[] = {0x00, 0x80};
    set_cur(&audio, setup(0x21, 0x01, VOLUME, FEATURE, 2), below, 2);
    CHECK_EQUAL(-16384, audio.volume);
}

static void requests_for_other_entities_or_channels_stall(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    CHECK_EQUAL(USB_REPLY_STALL,
                usb_audio_setup(&audio, setup(0xA1, 0x01, VOLUME | 1, FEATURE, 2)).kind);
    CHECK_EQUAL(USB_REPLY_STALL,
                usb_audio_setup(&audio, setup(0xA1, 0x01, VOLUME, 0x0300, 2)).kind);
    CHECK_EQUAL(USB_REPLY_STALL,
                usb_audio_setup(&audio, setup(0x21, 0x01, CLOCK_VALID, CLOCK, 1)).kind);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(&audio, setup(0xA1, 0x02, MUTE, FEATURE, 2)).kind);
    CHECK_EQUAL(USB_REPLY_STALL, usb_audio_setup(&audio, setup(0xA2, 0x01, FREQUENCY, 1, 4)).kind);
}

static void the_media_keys_still_answer(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    UsbReply reply = usb_audio_setup(&audio, setup(0x81, 6, 0x2200, USB_HID_INTERFACE, 255));
    CHECK_EQUAL(29, reply.length);
    reply = usb_audio_setup(&audio, setup(0x81, 6, 0x2100, USB_HID_INTERFACE, 255));
    CHECK_EQUAL(9, reply.length);
    CHECK_EQUAL(0x21, reply.data[1]);
    usb_audio_set_report(&audio, USB_HID_NEXT);
    reply = usb_audio_setup(&audio, setup(0xA1, 0x01, 0x0100, USB_HID_INTERFACE, 1));
    CHECK_EQUAL(USB_HID_NEXT, reply.data[0]);
}

static void a_new_configuration_stops_streaming(void)
{
    UsbAudio audio = ready_audio(USB_SPEED_HIGH);
    usb_audio_setup(&audio, setup(0x01, 11, 3, 1, 0));
    UsbReply reply = usb_audio_setup(&audio, setup(0x00, 9, 1, 0, 0));
    CHECK_EQUAL(USB_ACTION_CONFIGURE, reply.action);
    CHECK_EQUAL(0, usb_audio_sample_bytes(&audio));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_device_is_a_usb2_composite_with_its_own_product),
        TEST_CASE(both_speed_configurations_walk_to_their_length),
        TEST_CASE(the_control_header_covers_its_units),
        TEST_CASE(three_settings_carry_sixteen_twenty_four_and_thirty_two_bits),
        TEST_CASE(high_speed_endpoints_fit_a_microframe_of_384_kilohertz),
        TEST_CASE(full_speed_endpoints_fit_a_frame_of_96_kilohertz),
        TEST_CASE(the_other_speed_configuration_describes_the_other_speed),
        TEST_CASE(the_stream_packet_follows_the_setting_and_speed),
        TEST_CASE(high_speed_offers_eight_rates_up_to_384_kilohertz),
        TEST_CASE(full_speed_stops_at_96_kilohertz),
        TEST_CASE(the_host_sets_and_reads_the_clock),
        TEST_CASE(an_unknown_rate_stalls),
        TEST_CASE(volume_and_mute_work_in_quarter_decibel_units),
        TEST_CASE(requests_for_other_entities_or_channels_stall),
        TEST_CASE(the_media_keys_still_answer),
        TEST_CASE(a_new_configuration_stops_streaming),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
