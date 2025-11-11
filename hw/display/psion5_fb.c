/*
 * Psion5mx Framebuffer Emulation
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "ui/input.h"
#include "framebuffer.h"
#include "ui/pixel_ops.h"
#include "qom/object.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/display/psion5_fb.h"


static void psion5fb_draw_line(void *opaque, uint8_t *d, const uint8_t *src,
                             int width, int pitch)
{
    Psion5FbState *s = PSION5FB(opaque);
    uint32_t *buf = (uint32_t *)d;
    int bpp = s->bpp;
    int ppb = 8 / bpp;  /* pixels per byte */
    int x;

    /* src points to the start of pixel data for this line (offset 0x20 from framebuffer start) */
    for (x = 0; x < s->cols; x++) {
        int byte_idx = x / ppb;
        uint8_t byte = src[byte_idx];
        int shift = (x & (ppb - 1)) * bpp;
        int mask = (1 << bpp) - 1;
        int pal_idx = (byte >> shift) & mask;
        int pal_value = s->palette[pal_idx];
        
        /* Convert palette value to RGB using precomputed rgb_values */
        buf[x] = s->rgb_values[pal_value & 0xF];
    }
}

static void psion5fb_update(void *opaque)
{
    Psion5FbState *s = PSION5FB(opaque);
    MemoryRegion *sysmem = get_system_memory();
    int dest_width = 4;
    int src_width = 0;
    int first = 0;
    int last  = 0;
    DisplaySurface *surface = qemu_console_surface(s->con);
    uint8_t fb_header[0x20];
    int i;

    if (s->base_addr == 0) {
        s->invalidate = 0;
        return;
    }
    if (s->invalidate || true) {
        /* Read framebuffer header (first 0x20 bytes) to get palette and bpp */
        address_space_read(&address_space_memory, s->base_addr, MEMTXATTRS_UNSPECIFIED,
                            fb_header, sizeof(fb_header));

        /* Extract bpp from byte 1, upper 4 bits */
        s->bpp = 1 << ((fb_header[1] >> 4) & 0x3);
        if (s->bpp == 0 || s->bpp > 4) {
            s->bpp = 2;  /* Default to 2 bpp if invalid */
        }

        /* Read palette (16 entries, 2 bytes each) */
        for (i = 0; i < 16; i++) {
            s->palette[i] = fb_header[i * 2] | ((fb_header[i * 2 + 1] << 8) & 0xF00);
        }

        /* Calculate source width: pixel data starts at offset 0x20 */
        src_width = (s->cols * s->bpp) / 8;
        dest_width = s->cols * 4;

        /* Update memory section to point to pixel data (offset 0x20 from base) */
        framebuffer_update_memory_section(&s->fbsection,
                                            sysmem,
                                            s->base_addr + 0x20,
                                            s->rows, src_width);
        s->invalidate = 0;
    }

    if (s->base_addr != 0 && src_width > 0) {
        framebuffer_update_display(surface, &s->fbsection, s->cols, s->rows,
                                   src_width, dest_width, 0, 1, psion5fb_draw_line,
                                   s, &first, &last);

        dpy_gfx_update(s->con, 0, 0, s->cols, s->rows);
    }
}

static void psion5fb_invalidate(void *opaque)
{
    Psion5FbState *s = PSION5FB(opaque);
    s->invalidate = 1;
}

static void psion5fb_input_event(DeviceState *dev, QemuConsole *src,
                                 InputEvent *evt)
{
    Psion5FbState *s = PSION5FB(dev);
    InputBtnEvent *btn;
    InputMoveEvent *move;
    int x, y;

    switch (evt->type) {
    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (btn->button == INPUT_BUTTON_LEFT) {
            s->touch_pressed = btn->down ? 1 : 0;
            if (s->touch_update_cb) {
                s->touch_update_cb(s->touch_opaque, s->touch_x, s->touch_y, s->touch_pressed);
            }
        }
        break;

    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs.data;
        switch (move->axis) {
        case INPUT_AXIS_X:
            /* Scale from 0-0x7FFF to 0-cols */
            x = (move->value * s->cols) / 0x7FFF;
            if (x < 0) x = 0;
            if (x >= (int)s->cols) x = s->cols - 1;
            s->touch_x = x;
            break;
        case INPUT_AXIS_Y:
            /* Scale from 0-0x7FFF to 0-rows */
            y = (move->value * s->rows) / 0x7FFF;
            if (y < 0) y = 0;
            if (y >= (int)s->rows) y = s->rows - 1;
            s->touch_y = y;
            break;
        default:
            break;
        }
        if (s->touch_update_cb) {
            s->touch_update_cb(s->touch_opaque, s->touch_x, s->touch_y, s->touch_pressed);
        }
        break;

    case INPUT_EVENT_KIND_REL:
        /* Relative movement - update position */
        move = evt->u.rel.data;
        switch (move->axis) {
        case INPUT_AXIS_X:
            s->touch_x += move->value;
            if (s->touch_x < 0) s->touch_x = 0;
            if (s->touch_x >= (int)s->cols) s->touch_x = s->cols - 1;
            break;
        case INPUT_AXIS_Y:
            s->touch_y += move->value;
            if (s->touch_y < 0) s->touch_y = 0;
            if (s->touch_y >= (int)s->rows) s->touch_y = s->rows - 1;
            break;
        default:
            break;
        }
        if (s->touch_update_cb) {
            s->touch_update_cb(s->touch_opaque, s->touch_x, s->touch_y, s->touch_pressed);
        }
        break;

    case INPUT_EVENT_KIND_KEY: {
        InputKeyEvent *key = evt->u.key.data;
        
        /* Pass QEMU key codes directly to the callback for mapping in psion_5mx.c */
        if (key->key->type == KEY_VALUE_KIND_QCODE && s->keyboard_update_cb) {
            QKeyCode qcode = key->key->u.qcode.data;
            s->keyboard_update_cb(s->keyboard_opaque, qcode, key->down);
        }
        break;
    }

    default:
        break;
    }
}

static void psion5fb_input_sync(DeviceState *dev)
{
    /* Sync is called after a batch of input events */
    /* For now, we don't need to do anything special */
}

static const QemuInputHandler psion5fb_input_handler = {
    .name  = "Psion5mx Touch/Keyboard",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS | INPUT_EVENT_MASK_REL | INPUT_EVENT_MASK_KEY,
    .event = psion5fb_input_event,
    .sync  = psion5fb_input_sync,
};

static const GraphicHwOps psion5fb_ops = {
    .invalidate  = psion5fb_invalidate,
    .gfx_update  = psion5fb_update,
};

static void psion5fb_realize(DeviceState *dev, Error **errp)
{
    Psion5FbState *s = PSION5FB(dev);
    int i;

    s->invalidate = 1;
    s->base_addr = 0x0;  /* Initialize to 0, will be set at runtime */
    s->bpp = 2;  /* Default to 2 bpp */

    /* Initialize RGB values */
    for (i = 0; i < 16; i++) {
        int r = (0x99 * i) / 15;
        int g = (0xAA * i) / 15;
        int b = (0x88 * i) / 15;
        s->rgb_values[15 - i] = r | (g << 8) | (b << 16) | 0xFF000000;
    }

    /* Initialize input state */
    s->touch_x = 0;
    s->touch_y = 0;
    s->touch_pressed = 0;
    s->touch_update_cb = NULL;
    s->touch_opaque = NULL;
    s->keyboard_update_cb = NULL;
    s->keyboard_opaque = NULL;

    s->con = graphic_console_init(dev, 0, &psion5fb_ops, s);
    qemu_console_resize(s->con, s->cols, s->rows);

    /* Register input handler - binding is optional and not needed here */
    s->input_handler = qemu_input_handler_register(dev, &psion5fb_input_handler);
}

void psion5fb_set_base_addr(Psion5FbState *s, hwaddr base_addr)
{
    s->base_addr = base_addr;
    s->invalidate = 1;
    /* Trigger a display update to read from the new base address */
    if (s->con) {
        dpy_gfx_update_full(s->con);
    }
}

void psion5fb_set_touch_callback(Psion5FbState *s, void (*cb)(void *opaque, int x, int y, int pressed), void *opaque)
{
    s->touch_update_cb = cb;
    s->touch_opaque = opaque;
}

void psion5fb_set_keyboard_callback(Psion5FbState *s, void (*cb)(void *opaque, QKeyCode qcode, int pressed), void *opaque)
{
    s->keyboard_update_cb = cb;
    s->keyboard_opaque = opaque;
}


static Property psion5fb_properties[] = {
    DEFINE_PROP_UINT32("cols", Psion5FbState, cols, 640),
    DEFINE_PROP_UINT32("rows", Psion5FbState, rows, 240),
    DEFINE_PROP_END_OF_LIST(),
};

static void psion5fb_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->realize = psion5fb_realize;
    device_class_set_props(dc, psion5fb_properties);

    /* Note: This device does not have any state that we have to reset or migrate */
}

static const TypeInfo psion5fb_info = {
    .name          = TYPE_PSION5FB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Psion5FbState),
    .class_init    = psion5fb_class_init,
};

static void psion5fb_register_types(void)
{
    type_register_static(&psion5fb_info);
}

type_init(psion5fb_register_types)

