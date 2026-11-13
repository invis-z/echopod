#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/usb_audio.h"

enum {
    USB_AUDIO2_LAST_ALT = 3
};

UsbReply usb_audio2_descriptor(UsbAudio *audio, UsbSetup setup);
bool usb_audio2_owns_descriptor(UsbSetup setup);
UsbReply usb_audio2_class_in(UsbAudio *audio, UsbSetup setup);
UsbReply usb_audio2_class_out(UsbSetup setup);
UsbReply usb_audio2_receive(UsbAudio *audio, UsbSetup setup, const uint8_t *data, uint16_t length);
uint8_t usb_audio2_sample_bytes(uint8_t alt);
uint8_t usb_audio2_bits(uint8_t alt);
uint16_t usb_audio2_packet(UsbSpeed speed, uint8_t alt);
