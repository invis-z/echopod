#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "hal/system.h"
#include "overlay.h"

#define CODE2_IMAGE_OFFSET 0x438000u
#define CODE2_MAGIC 0x32444345u
#define CODE2_END 0x0307A000u

extern uint8_t device_code2_start[];
extern uint8_t device_code2_end[];

static bool ready;

bool board_code2_load(void)
{
    uint8_t *base = device_code2_start;
    uint32_t capacity = (uint32_t)(CODE2_END - (uintptr_t)base);
    BoardOverlay code = {CODE2_IMAGE_OFFSET, CODE2_MAGIC, base, capacity};
    ready = device_code2_end - device_code2_start > (int)OVERLAY_HEADER_BYTES &&
            board_overlay_load(&code);
    if (!ready) {
        for (uint32_t index = 0; index < capacity; index++) {
            base[index] = 0;
        }
    }
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    return ready;
}

bool hal_system_code2_ready(void)
{
    return ready;
}
