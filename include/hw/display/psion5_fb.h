#pragma once

#include "exec/hwaddr.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "qom/object.h"
#include "exec/memory.h"
#include "ui/input.h"



struct Psion5FbState {
    SysBusDevice parent_obj;


    MemoryRegion fb_mr;
    MemoryRegionSection fbsection;
    QemuConsole *con;
    

    uint32_t cols;
    uint32_t rows;
    hwaddr base_addr;
    int invalidate;
    
    /* Palette and display format */
    uint16_t palette[16];
    int bpp;  /* bits per pixel: 1, 2, or 4 */
    uint32_t rgb_values[16];  /* Precomputed RGB values */
    
    /* Input handling */
    QemuInputHandlerState *input_handler;
    void (*touch_update_cb)(void *opaque, int x, int y, int pressed);
    void *touch_opaque;
    void (*keyboard_update_cb)(void *opaque, QKeyCode qcode, int pressed);
    void *keyboard_opaque;
    
    /* Current touch state */
    int touch_x;
    int touch_y;
    int touch_pressed;  /* 0 = not pressed, 1 = pressed */
};

#define TYPE_PSION5FB "psion5-fb"
OBJECT_DECLARE_SIMPLE_TYPE(Psion5FbState, PSION5FB)

void psion5fb_set_base_addr(Psion5FbState *s, hwaddr base_addr);
void psion5fb_set_touch_callback(Psion5FbState *s, void (*cb)(void *opaque, int x, int y, int pressed), void *opaque);
void psion5fb_set_keyboard_callback(Psion5FbState *s, void (*cb)(void *opaque, QKeyCode qcode, int pressed), void *opaque);

