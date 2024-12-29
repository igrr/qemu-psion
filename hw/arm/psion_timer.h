#pragma once

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/arm/boot.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "hw/block/flash.h"
#include "ui/console.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/bitbang_i2c.h"
#include "hw/irq.h"
#include "hw/or-irq.h"
#include "hw/audio/wm8750.h"
#include "sysemu/block-backend.h"
#include "sysemu/runstate.h"
#include "sysemu/dma.h"
#include "ui/pixel_ops.h"
#include "qemu/cutils.h"
#include "qom/object.h"
#include "audio/audio.h"
#include "qemu/error-report.h"
#include "qemu/datadir.h"
#include "hw/loader.h"
#include "hw/hw.h"
#include "hw/registerfields.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include <stdint.h>

/* CL-PS7110 and Windermere timers */

#define TYPE_PSION_TIMER "psion_timer"

OBJECT_DECLARE_SIMPLE_TYPE(PsionTimerState, PSION_TIMER)

typedef enum timer_clk_t {
    TIMER_CLK_512KHZ = 1,
    TIMER_CLK_2KHZ = 0,
} timer_clk_t;

typedef enum timer_mode_t {
    TIMER_MODE_PRESCALE = 1,
    TIMER_MODE_FREE_RUNNING = 0,
} timer_mode_t;


typedef struct PsionTimerState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    timer_clk_t clk;
    timer_mode_t mode;
    bool enabled;
    uint16_t interval;
    uint16_t base_value;
    int64_t base_ns;
    QEMUTimer timer;
    int index;
    qemu_irq irq;
} PsionTimerState;


void psion_timer_update_alarm(PsionTimerState* timer);
void psion_timer_update_settings(PsionTimerState *timer, timer_clk_t clk, timer_mode_t mode, bool enabled);
void psion_timer_load(PsionTimerState *timer, uint32_t new_val);
int16_t psion_timer_get_val(PsionTimerState* timer);
