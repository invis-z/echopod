#include "check.h"
#include "device/usb_feedback.h"

static void high_speed_reports_samples_per_microframe_in_sixteen_sixteen(void)
{
    UsbFeedback feedback;
    usb_feedback_start(&feedback, 48000, true, 1000);
    CHECK_EQUAL(6u << 16, usb_feedback_value(&feedback, 1000));
    CHECK_EQUAL(4, usb_feedback_length(&feedback));
    usb_feedback_start(&feedback, 384000, true, 1000);
    CHECK_EQUAL(48u << 16, usb_feedback_value(&feedback, 1000));
    usb_feedback_start(&feedback, 44100, true, 1000);
    CHECK_EQUAL((uint32_t)(44100ull * 65536 / 8000), usb_feedback_value(&feedback, 1000));
}

static void full_speed_reports_samples_per_frame_in_ten_fourteen(void)
{
    UsbFeedback feedback;
    usb_feedback_start(&feedback, 96000, false, 1000);
    CHECK_EQUAL(96u << 14, usb_feedback_value(&feedback, 1000));
    CHECK_EQUAL(3, usb_feedback_length(&feedback));
    usb_feedback_start(&feedback, 44100, false, 1000);
    CHECK_EQUAL((uint32_t)(44100ull * 16384 / 1000), usb_feedback_value(&feedback, 1000));
}

static void a_low_ring_asks_for_more_and_a_full_one_for_less(void)
{
    UsbFeedback feedback;
    usb_feedback_start(&feedback, 48000, true, 1000);
    CHECK(usb_feedback_value(&feedback, 990) > 6u << 16);
    usb_feedback_start(&feedback, 48000, true, 1000);
    CHECK(usb_feedback_value(&feedback, 1010) < 6u << 16);
}

static void a_fill_error_is_made_up_in_about_a_second(void)
{
    UsbFeedback feedback;
    usb_feedback_start(&feedback, 48000, true, 1000);
    uint32_t value = usb_feedback_value(&feedback, 960);
    uint32_t extra = value - (6u << 16);
    CHECK_EQUAL(40 * 65536 / 8000, extra);
}

static void the_correction_stays_within_half_a_percent(void)
{
    UsbFeedback feedback;
    usb_feedback_start(&feedback, 192000, true, 4000);
    uint32_t nominal = 24u << 16;
    CHECK_EQUAL(nominal + nominal / 200, usb_feedback_value(&feedback, -100000));
    usb_feedback_start(&feedback, 192000, true, 4000);
    CHECK_EQUAL(nominal - nominal / 200, usb_feedback_value(&feedback, 100000));
}

static void a_single_burst_moves_the_value_only_a_little(void)
{
    UsbFeedback feedback;
    usb_feedback_start(&feedback, 48000, true, 1000);
    for (int step = 0; step < 50; step++) {
        usb_feedback_value(&feedback, 1000);
    }
    uint32_t after_burst = usb_feedback_value(&feedback, 1800);
    uint32_t settled_jump = (6u << 16) - (800u * 65536 / 8000);
    CHECK(after_burst < 6u << 16);
    CHECK(after_burst > settled_jump);
    CHECK((6u << 16) - after_burst <= ((6u << 16) - settled_jump) / 8);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(high_speed_reports_samples_per_microframe_in_sixteen_sixteen),
        TEST_CASE(full_speed_reports_samples_per_frame_in_ten_fourteen),
        TEST_CASE(a_low_ring_asks_for_more_and_a_full_one_for_less),
        TEST_CASE(a_fill_error_is_made_up_in_about_a_second),
        TEST_CASE(the_correction_stays_within_half_a_percent),
        TEST_CASE(a_single_burst_moves_the_value_only_a_little),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
