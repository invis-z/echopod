#include "device/usb_feedback.h"

enum {
    HIGH_SPEED_INTERVALS = 8000,
    FULL_SPEED_INTERVALS = 1000,
    HIGH_SPEED_SHIFT = 16,
    FULL_SPEED_SHIFT = 14,
    SMOOTHING_SHIFT = 4,
    LIMIT_DIVISOR = 200
};

void usb_feedback_start(UsbFeedback *feedback, uint32_t rate, bool high_speed, uint32_t target)
{
    uint32_t intervals = high_speed ? HIGH_SPEED_INTERVALS : FULL_SPEED_INTERVALS;
    uint32_t shift = high_speed ? HIGH_SPEED_SHIFT : FULL_SPEED_SHIFT;
    uint32_t nominal = (uint32_t)(((uint64_t)rate << shift) / intervals);
    *feedback = (UsbFeedback){.nominal = nominal,
                              .limit = (int32_t)(nominal / LIMIT_DIVISOR),
                              .target = (int32_t)target,
                              .intervals = intervals,
                              .unit = 1u << shift,
                              .high_speed = high_speed};
}

uint32_t usb_feedback_value(UsbFeedback *feedback, int32_t fill)
{
    int32_t error = feedback->target - fill;
    if (!feedback->filled) {
        feedback->smoothed = error;
        feedback->filled = true;
    } else {
        feedback->smoothed += (error - feedback->smoothed) / (1 << SMOOTHING_SHIFT);
    }
    int64_t correction =
        (int64_t)feedback->smoothed * (int64_t)feedback->unit / (int64_t)feedback->intervals;
    if (correction > feedback->limit) {
        correction = feedback->limit;
    } else if (correction < -feedback->limit) {
        correction = -feedback->limit;
    }
    return (uint32_t)((int64_t)feedback->nominal + correction);
}

uint8_t usb_feedback_length(const UsbFeedback *feedback)
{
    return feedback->high_speed ? 4 : 3;
}
