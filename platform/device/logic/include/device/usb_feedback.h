#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct UsbFeedback {
    uint32_t nominal;
    int32_t limit;
    int32_t target;
    int32_t smoothed;
    uint32_t intervals;
    uint32_t unit;
    bool high_speed;
    bool filled;
} UsbFeedback;

void usb_feedback_start(UsbFeedback *feedback, uint32_t rate, bool high_speed, uint32_t target);
uint32_t usb_feedback_value(UsbFeedback *feedback, int32_t fill);
uint8_t usb_feedback_length(const UsbFeedback *feedback);
