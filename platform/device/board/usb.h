#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/msc.h"
#include "device/usb_audio.h"
#include "device/usb_device.h"

enum {
    USB_PORT_PACKET_BYTES = (int)USB_AUDIO_MAX_PACKET > (int)USB_HIGH_SPEED_PACKET_SIZE
        ? USB_AUDIO_MAX_PACKET
        : USB_HIGH_SPEED_PACKET_SIZE,
    USB_PORT_PACKET_WORDS = (USB_PORT_PACKET_BYTES + 3) / 4
};

typedef enum UsbConnectStep {
    USB_CONNECT_OK,
    USB_CONNECT_AHB_IDLE,
    USB_CONNECT_SOFT_RESET,
    USB_CONNECT_FLUSH
} UsbConnectStep;

typedef struct UsbProbe {
    uint32_t core_id;
    uint32_t otg_control;
    uint32_t hardware_3;
    uint32_t hardware_4;
    uint32_t interrupts;
    uint32_t rx_fifo_size;
    uint32_t tx_fifo_size;
} UsbProbe;

typedef struct UsbLink {
    UsbConnectStep step;
    bool high_speed_allowed;
    bool dedicated_fifos;
} UsbLink;

typedef struct UsbPort {
    UsbLink link;
    bool configured;
    bool in_busy;
    bool setup_pending;
    uint16_t bulk_packet;
    uint32_t setup[2];
    bool control_out_pending;
    bool report_busy;
    uint16_t packet_frame;
    uint32_t configured_resets;
    UsbSetup control_out;
    uint32_t out_packet[USB_PORT_PACKET_WORDS];
    UsbDevice device;
    Msc *msc;
    UsbAudio *audio;
    volatile bool control_data_ready;
    uint16_t control_data_length;
    uint32_t control_data[USB_PACKET_SIZE / 4];
    bool interrupts;
    bool feedback_armed;
    bool feedback_odd;
    uint32_t feedback_armed_ms;
    uint32_t feedback_retries;
    uint32_t control_entries;
    uint32_t wrong_slots;
    uint32_t late_interrupts;
    uint32_t interrupt_last_us;
    uint32_t interrupt_gap_us;
    void (*receive_stream)(const uint8_t *bytes, uint32_t length);
} UsbPort;

bool usb_vbus_present(void);
void usb_power_up(void);
void usb_power_down(void);
void usb_silence_audio(void);
bool usb_host_reset_pending(void);
bool usb_connect(UsbLink *link);
bool usb_flush_fifos(void);
bool usb_port_connect(UsbPort *port);
bool usb_wait_for_host(UsbPort *port, uint32_t timeout_ms);
void usb_poll(UsbPort *port);
void usb_receive_interrupts_start(UsbPort *port);
void usb_receive_interrupts_stop(UsbPort *port);
bool usb_feedback_wanted(UsbPort *port);
void usb_feedback_send(UsbPort *port, uint32_t value);
UsbProbe usb_probe(void);
bool usb_reply_pending(const UsbPort *port);
bool usb_send_report(UsbPort *port, uint8_t report);
