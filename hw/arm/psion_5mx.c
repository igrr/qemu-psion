#include "qemu/osdep.h"
#include "exec/hwaddr.h"
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
#include "vcd.h"
#include "psion_5mx.h"
#include "psion_timer.h"
#include "hw/qdev-core.h"
#include "qemu/log.h"
#include "hw/display/psion5_fb.h"
#include "ui/input.h"


// #define ENABLE_RTC_DEBUG
#ifdef ENABLE_RTC_DEBUG
#define RTC_DEBUG(fmt, ...) qemu_log("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define RTC_DEBUG(fmt, ...)
#endif

// #define ENABLE_IRQ_DEBUG
#ifdef ENABLE_IRQ_DEBUG
#define IRQ_DEBUG(fmt, ...) qemu_log("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define IRQ_DEBUG(fmt, ...)
#endif

// #define ENABLE_REG_DEBUG
#ifdef ENABLE_REG_DEBUG
#define REG_DEBUG(fmt, ...) qemu_log("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define REG_DEBUG(fmt, ...)
#endif

#define ENABLE_UNHANDLED_REG_DEBUG
#ifdef ENABLE_UNHANDLED_REG_DEBUG
#define UNHANDLED_REG_DEBUG(fmt, ...) qemu_log("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define UNHANDLED_REG_DEBUG(fmt, ...)
#endif


// #define ENABLE_GPIO_DEBUG
#ifdef ENABLE_GPIO_DEBUG
#define GPIO_DEBUG(fmt, ...) qemu_log("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define GPIO_DEBUG(fmt, ...)
#endif

// #define ENABLE_SYNCIO_DEBUG
#ifdef ENABLE_SYNCIO_DEBUG
#define SYNCIO_DEBUG(fmt, ...) qemu_log("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define SYNCIO_DEBUG(fmt, ...)
#endif


#define TYPE_WINDERMERE "windermere"

OBJECT_DECLARE_SIMPLE_TYPE(WindermereState, WINDERMERE)

typedef struct WindermereState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMCPU *cpu;
    MemoryRegion iomem;

    struct {
        MemoryRegion iomem;
        uint8_t wake1;
        uint8_t wake2;
        uint8_t wake3;
    } etna;


    /* gpio ports A-E */
    uint8_t port_out[5];
    uint8_t port_dir[5];
    uint8_t port_in[5];

    /* timers */
    PsionTimerState timers[2];

    QEMUTimer rtc_timer;
    QEMUTimer wd_timer;

    /* syncio */
    uint16_t syncio_request;      /* lastSSIRequest */
    uint16_t syncio_response;
    int ssi_read_counter;         /* ssiReadCounter */
    int touch_x;                  /* touchX - digitizer X coordinate */
    int touch_y;                  /* touchY - digitizer Y coordinate */

    /* interrupts */
    qemu_irq irq;
    qemu_irq fiq;
    uint32_t irqstatus;      /* not a real register, used to track changes to interrupt status */
    uint32_t fiqstatus;      /* same for fiq */

    /* misc state */
    uint32_t intsr;          /* interrupt status register */
    uint32_t intmr;          /* interrupt mask register */
    uint64_t rtc_count;      /* rtc tick counter, 64 Hz */
    uint32_t rtcmr;          /* real time clock match register */


    /*lcd*/
    uint32_t lcd_bar1;     /* LCD frame buffer base address */
    Psion5FbState fb;
    
    /* keyboard */
    uint8_t keyboard_columns[8];  /* Keyboard matrix: 8 columns, 7 bits each */
    uint8_t kscan;                /* Keyboard scan register */
} WindermereState;

#define FIQ_INTERRUPTS (R_INTSR_EXTFIQ_MASK | R_INTSR_BLINT_MASK | R_INTSR_WEINT_MASK)
#define IRQ_INTERRUPTS (R_INTSR_TC1OI_MASK | R_INTSR_TC2OI_MASK | R_INTSR_SSEOTI_MASK | R_INTSR_TINT_MASK | R_INTSR_RTCMI_MASK)

static void windermere_update_irq(WindermereState* s)
{
    uint32_t new_intstatus = s->intsr & s->intmr;
    uint32_t new_irqstatus = new_intstatus & IRQ_INTERRUPTS;
    uint32_t new_fiqstatus = new_intstatus & FIQ_INTERRUPTS;
    if (new_irqstatus != s->irqstatus) {
        s->irqstatus = new_irqstatus;
        // VCD_WRITE(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL), A_INTSR, new_intstatus);
        if (new_irqstatus) {
            IRQ_DEBUG("windermere_update_irq: raising IRQ, st=0x%04x mask=0x%04x raw=0x%04x\n", new_irqstatus, s->intmr, s->intsr);
            qemu_irq_raise(s->irq);
        } else {
            qemu_irq_lower(s->irq);
        }
    }
    if (new_fiqstatus != s->fiqstatus) {
        s->fiqstatus = new_fiqstatus;
        if (new_fiqstatus) {
            qemu_irq_raise(s->fiq);
        } else {
            qemu_irq_lower(s->fiq);
        }
    }
}


static void windermere_timer_cb(void *opaque, int n, int level)
{
    assert (n >= 0 && n < 2);
    WindermereState *s = WINDERMERE(opaque);
    uint32_t irq_mask = (n == 0) ? R_INTSR_TC1OI_MASK : R_INTSR_TC2OI_MASK;
    if (level) {
        s->intsr |= irq_mask;
    } else {
        s->intsr &= ~irq_mask;
    }
    windermere_update_irq(s);
}

/* RTC clock value, in 64Hz ticks */
static uint64_t windermere_get_rtc(WindermereState* s)
{
    return s->rtc_count;
}


static void windermere_rtc_cb(void* opaque)
{
    WindermereState *s = WINDERMERE(opaque);
    s->rtc_count++;
    RTC_DEBUG("windermere_rtc_cb: rtc_count=%" PRId64 "\n", s->rtc_count);
    s->intsr |= R_INTSR_TINT_MASK;
    timer_mod_anticipate_ns(&s->rtc_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000 * 1000 * 1000 / 64);
    windermere_update_irq(s);
}

static void windermere_touch_update(void *opaque, int x, int y, int pressed)
{
    WindermereState *s = WINDERMERE(opaque);

    s->touch_x = x;
    s->touch_y = y;

    uint32_t old_intsr = s->intsr;

    s->intsr &= ~R_INTSR_EINT3_MASK;
    if (pressed)
        s->intsr |= R_INTSR_EINT3_MASK;

    if (old_intsr != s->intsr) {
        IRQ_DEBUG("windermere_touch_update: x=%d, y=%d, pressed=%d, intsr=0x%04x\n", x, y, pressed, s->intsr);
        windermere_update_irq(s);
    }
}

static void windermere_set_keyboard_key(WindermereState *s, QKeyCode qcode, int pressed)
{
    int column = -1;
    int bit = -1;
    
    /* Map QEMU key codes directly to keyboard matrix column/bit positions */
    switch (qcode) {
        /* Column 0 */
        case Q_KEY_CODE_F2: /* EStdKeyDictaphoneRecord (158) */ column = 0; bit = 6; break;
        case Q_KEY_CODE_1: column = 0; bit = 5; break;
        case Q_KEY_CODE_2: column = 0; bit = 4; break;
        case Q_KEY_CODE_3: column = 0; bit = 3; break;
        case Q_KEY_CODE_4: column = 0; bit = 2; break;
        case Q_KEY_CODE_5: column = 0; bit = 1; break;
        case Q_KEY_CODE_6: column = 0; bit = 0; break;
        
        /* Column 1 */
        case Q_KEY_CODE_F3: /* EStdKeyDictaphonePlay (156) */ column = 1; bit = 6; break;
        case Q_KEY_CODE_7: column = 1; bit = 5; break;
        case Q_KEY_CODE_8: column = 1; bit = 4; break;
        case Q_KEY_CODE_9: column = 1; bit = 3; break;
        case Q_KEY_CODE_0: column = 1; bit = 2; break;
        case Q_KEY_CODE_BACKSPACE: /* EStdKeyBackspace (1) */ column = 1; bit = 1; break;
        case Q_KEY_CODE_APOSTROPHE: /* EStdKeySingleQuote (126) */ column = 1; bit = 0; break;
        
        /* Column 2 */
        case Q_KEY_CODE_ESC: /* EStdKeyEscape (4) */ column = 2; bit = 6; break;
        case Q_KEY_CODE_Q: column = 2; bit = 5; break;
        case Q_KEY_CODE_W: column = 2; bit = 4; break;
        case Q_KEY_CODE_E: column = 2; bit = 3; break;
        case Q_KEY_CODE_R: column = 2; bit = 2; break;
        case Q_KEY_CODE_T: column = 2; bit = 1; break;
        case Q_KEY_CODE_Y: column = 2; bit = 0; break;
        
        /* Column 3 */
        case Q_KEY_CODE_MENU: /* EStdKeyMenu (148) - use MENU key if available, otherwise F3 */
        case Q_KEY_CODE_F1: /* EStdKeyMenu (148) fallback */ column = 3; bit = 6; break;
        case Q_KEY_CODE_U: column = 3; bit = 5; break;
        case Q_KEY_CODE_I: column = 3; bit = 4; break;
        case Q_KEY_CODE_O: column = 3; bit = 3; break;
        case Q_KEY_CODE_P: column = 3; bit = 2; break;
        case Q_KEY_CODE_L: column = 3; bit = 1; break;
        case Q_KEY_CODE_RET: /* EStdKeyEnter (3) */ column = 3; bit = 0; break;
        
        /* Column 4 */
        case Q_KEY_CODE_CTRL: /* EStdKeyLeftCtrl (22) */ column = 4; bit = 6; break;
        case Q_KEY_CODE_TAB: /* EStdKeyTab (2) */ column = 4; bit = 5; break;
        case Q_KEY_CODE_A: column = 4; bit = 4; break;
        case Q_KEY_CODE_S: column = 4; bit = 3; break;
        case Q_KEY_CODE_D: column = 4; bit = 2; break;
        case Q_KEY_CODE_F: column = 4; bit = 1; break;
        case Q_KEY_CODE_G: column = 4; bit = 0; break;
        
        /* Column 5 */
        case Q_KEY_CODE_ALT_R: /* EStdKeyLeftFunc (24) */ column = 5; bit = 6; break;
        case Q_KEY_CODE_H: column = 5; bit = 5; break;
        case Q_KEY_CODE_J: column = 5; bit = 4; break;
        case Q_KEY_CODE_K: column = 5; bit = 3; break;
        case Q_KEY_CODE_M: column = 5; bit = 2; break;
        case Q_KEY_CODE_DOT: /* EStdKeyFullStop (122) */ column = 5; bit = 1; break;
        case Q_KEY_CODE_DOWN: /* EStdKeyDownArrow (17) */ column = 5; bit = 0; break;
        
        /* Column 6 */
        case Q_KEY_CODE_SHIFT_R: /* EStdKeyRightShift (19) */ column = 6; bit = 6; break;
        case Q_KEY_CODE_Z: column = 6; bit = 5; break;
        case Q_KEY_CODE_X: column = 6; bit = 4; break;
        case Q_KEY_CODE_C: column = 6; bit = 3; break;
        case Q_KEY_CODE_V: column = 6; bit = 2; break;
        case Q_KEY_CODE_B: column = 6; bit = 1; break;
        case Q_KEY_CODE_N: column = 6; bit = 0; break;
        
        /* Column 7 */
        case Q_KEY_CODE_SHIFT: /* EStdKeyLeftShift (18) */ column = 7; bit = 6; break;
        case Q_KEY_CODE_F4: /* EStdKeyDictaphoneStop (157) */ column = 7; bit = 5; break;
        case Q_KEY_CODE_SPC: /* EStdKeySpace (5) */ column = 7; bit = 4; break;
        case Q_KEY_CODE_UP: /* EStdKeyUpArrow (16) */ column = 7; bit = 3; break;
        case Q_KEY_CODE_COMMA: /* EStdKeyComma (121) */ column = 7; bit = 2; break;
        case Q_KEY_CODE_LEFT: /* EStdKeyLeftArrow (14) */ column = 7; bit = 1; break;
        case Q_KEY_CODE_RIGHT: /* EStdKeyRightArrow (15) */ column = 7; bit = 0; break;
        
        default:
            return; /* Key not mapped */
    }
    
    if (column >= 0 && column < 8 && bit >= 0 && bit < 7) {
        if (pressed) {
            s->keyboard_columns[column] |= (1 << bit);
        } else {
            s->keyboard_columns[column] &= ~(1 << bit);
        }
    }
}

static void windermere_keyboard_update(void *opaque, QKeyCode qcode, int pressed)
{
    WindermereState *s = WINDERMERE(opaque);
    windermere_set_keyboard_key(s, qcode, pressed);
}

static uint8_t windermere_read_keyboard(WindermereState *s)
{
    if (s->kscan & 8) {
        /* Select one keyboard column */
        return s->keyboard_columns[s->kscan & 7];
    } else if (s->kscan == 0) {
        /* Report all columns combined */
        uint8_t val = 0;
        for (int i = 0; i < 8; i++) {
            val |= s->keyboard_columns[i];
        }
        return val;
    } else {
        return 0;
    }
}

static void windermere_handle_syncio_request(WindermereState* s, uint16_t value)
{
    /* On write to SSDR: update lastSSIRequest */
    if (value != 0) {
        s->syncio_request = (s->syncio_request >> 8) | (value & 0xFF00);
    }
    SYNCIO_DEBUG("windermere_handle_syncio_request: value=0x%04x, lastSSIRequest=0x%04x\n",
                 value, s->syncio_request);
}

static uint16_t windermere_get_syncio_response(WindermereState* s)
{
    uint16_t ssi_value = 0;
    uint32_t ret = 0;
    uint32_t left_bar_width = 48;

    /* Calculate ssiValue based on lastSSIRequest */
    switch (s->syncio_request) {
        case 0xD0D3:
            /* Touch X coordinate */
            ssi_value = (uint16_t)(50 + ((s->touch_x + left_bar_width) * 5.7));
            break;
        case 0x9093:
            /* Touch Y coordinate */
            ssi_value = (uint16_t)(3834 - (s->touch_y * 13.225));
            break;
        case 0xA4A4:
            /* Main Battery */
            ssi_value = 3100;
            break;
        case 0xE4E4:
            /* Backup Battery */
            ssi_value = 3100;
            break;
        default:
            ssi_value = 0;
            break;
    }

    /* Return different bits based on ssiReadCounter */
    if (s->ssi_read_counter == 4) {
        ret = (ssi_value >> 5) & 0x7F;
    }
    if (s->ssi_read_counter == 5) {
        ret = (ssi_value << 3) & 0xF8;
    }

    /* Increment counter and reset when it reaches 6 */
    s->ssi_read_counter++;
    if (s->ssi_read_counter == 6) {
        s->ssi_read_counter = 0;
    }

    SYNCIO_DEBUG("windermere_get_syncio_response: lastSSIRequest=0x%04x, counter=%d, ssiValue=%d, ret=0x%02x\n",
                 s->syncio_request, s->ssi_read_counter - 1, ssi_value, (uint8_t)ret);

    return (uint16_t)ret;
}

static uint64_t windermere_periph_read(void *opaque, hwaddr offset, unsigned size)
{
    WindermereState *s = WINDERMERE(opaque);
    uint32_t pc = s->cpu->env.regs[15];
    bool log_pc = true;
    uint32_t result = 0xffffffff;
    switch (offset) {
        case A_TC1CTRL: {
            FIELD_DP16(result, TC1CTRL, TC_MODE, s->timers[0].mode);
            FIELD_DP16(result, TC1CTRL, TC_CLKSEL, s->timers[0].clk);
            FIELD_DP16(result, TC1CTRL, TC_ENABLE, s->timers[0].enabled);
            break;
        }
        case A_TC2CTRL: {
            FIELD_DP16(result, TC2CTRL, TC_MODE, s->timers[1].mode);
            FIELD_DP16(result, TC2CTRL, TC_CLKSEL, s->timers[1].clk);
            FIELD_DP16(result, TC2CTRL, TC_ENABLE, s->timers[1].enabled);
            break;
        }
        case A_PADR: {
            /* When used for keyboard, return keyboard data based on kscan */
            result = windermere_read_keyboard(s);
            break;
        }
        case A_PBDR: {
            result = s->port_out[1];
            break;
        }
        case A_PCDR: {
            result = s->port_out[2];
            break;
        }
        case A_PDDR: {
            result = s->port_out[3];
            break;
        }
        case A_PADDR: {
            result = s->port_dir[0];
            break;
        }
        case A_PBDDR: {
            result = s->port_dir[1];
            break;
        }
        case A_PCDDR: {
            result = s->port_dir[2];
            break;
        }
        case A_PDDDR: {
            result = s->port_dir[3];
            break;
        }
        case A_PEDR: {
            result = s->port_out[4];
            break;
        }
        case A_PEDDR: {
            result = s->port_dir[4];
            break;
        }
        // case A_LCDCTL:

        case A_LCDST: {
            result = 0xffffffff;
            break;
        }
        case A_LCD_DBAR1: {
            result = s->lcd_bar1;
            break;
        }
        case A_PWRSR: {
            result = windermere_get_rtc(s) & 0x3f;
            break;
        }
        case A_INTSR: {
            result = s->intsr & s->intmr;
            break;
        }
        case A_INTENS: {
            result = s->intmr;
            break;
        }
        case A_INTRSR: {
            result = s->intsr;
            break;
        }
        case A_TC1VAL: {
            result = psion_timer_get_val(&s->timers[0]);
            break;
        }
        case A_TC2VAL: {
            result = psion_timer_get_val(&s->timers[1]);
            break;
        }
        case A_SSDR: {
            result = windermere_get_syncio_response(s);
            break;
        }
        case A_SSSR: {
            result = 0;
            break;
        }
        case A_RTCDRL: {
            result = (windermere_get_rtc(s) / 64) & 0xffff;
            break;
        }
        case A_RTCDRU: {
            result = (windermere_get_rtc(s) / 64) >> 16;
            break;
        }
        case A_KSCAN: {
            result = s->kscan;
            break;
        }
        case A_UART1_FLG: {
            result = 0;
            FIELD_DP32(result, UART1_FLG, UARTFLG_RXFE, 1);
            break;
        }
        case A_SSCR0: {
            result = 0;
            break;
        }
        default: {
            UNHANDLED_REG_DEBUG("windermere_periph_read: unhandled addr=%03x !!!!!!!!!!!!!!!!!!\n", (uint32_t) offset);
            break;
        }
    }
    if (log_pc)
    {
        REG_DEBUG("windermere_periph_read: addr=%03x result=0x%08x pc=0x%08x lr=0x%08x\n", (uint32_t) offset, (uint32_t) result, pc, s->cpu->env.regs[14]);
        // VCD_WRITE(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL), VCD_READ(offset), 1);
    }
    return result;
}

static void windermere_periph_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    WindermereState *s = WINDERMERE(opaque);
    uint32_t pc = s->cpu->env.regs[15];
    bool log_pc = true;
    // REG_DEBUG("windermere_periph_write: addr=%03x value=0x%08x pc=0x%08x\n", (uint32_t) offset, (uint32_t) value, pc);
    switch (offset) {
        case A_TC1CTRL: {
            psion_timer_update_settings(&s->timers[0], 
                FIELD_EX16(value, TC1CTRL, TC_CLKSEL), 
                FIELD_EX16(value, TC1CTRL, TC_MODE), 
                FIELD_EX16(value, TC1CTRL, TC_ENABLE));
            break;
        }
        case A_TC2CTRL: {
            psion_timer_update_settings(&s->timers[1], 
                FIELD_EX16(value, TC2CTRL, TC_CLKSEL), 
                FIELD_EX16(value, TC2CTRL, TC_MODE), 
                FIELD_EX16(value, TC2CTRL, TC_ENABLE));
            break;
        }

        case A_PADR: {
            s->port_out[0] = value & 0xff;
            GPIO_DEBUG("windermere_periph_write: PADR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PBDR: {
            s->port_out[1] = value & 0xff;
            GPIO_DEBUG("windermere_periph_write: PBDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PCDR: {
            s->port_out[2] = value & 0xff;
            GPIO_DEBUG("windermere_periph_write: PCDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PDDR: {
            s->port_out[3] = value & 0xff;
            GPIO_DEBUG("windermere_periph_write: PDDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PADDR: {
            s->port_dir[0] = value & 0xff;
            GPIO_DEBUG("windermere_periph_write: PADDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PBDDR: {
            s->port_dir[1] = value & 0xff;
            GPIO_DEBUG("windermere_periph_write: PBDDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PCDDR: {
            s->port_dir[2] = value & 0xff;
            GPIO_DEBUG("windermere_periph_write: PCDDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PDDDR: {
            s->port_dir[3] = value & 0xff;
            GPIO_DEBUG("windermere_periph_write: PDDDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PEDR: {
            s->port_out[4] = value & 0xff;
            GPIO_DEBUG("windermere_periph_write: PEDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PEDDR: {
            s->port_dir[4] = value & 0xff;
            GPIO_DEBUG("windermere_periph_write: PEDDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_INTENS : {
            s->intmr |= value & 0xffff;
            windermere_update_irq(s);
            break;
        }
        case A_INTENC : {
            s->intmr &= ~(value & 0xffff);
            windermere_update_irq(s);
            break;
        }
        case A_HALT: {
            cpu_interrupt(CPU(s->cpu), CPU_INTERRUPT_HALT);
            break;
        }
        case A_TEOI: {
            s->intsr &= ~R_INTSR_TINT_MASK;
            windermere_update_irq(s);
            break;
        }
        case A_SSDR: {
            windermere_handle_syncio_request(s, value);
            break;
        }
        case A_TC1LOAD: {
            psion_timer_load(&s->timers[0], value);
            break;
        }
        case A_TC1EOI: {
            s->intsr &= ~R_INTSR_TC1OI_MASK;
            windermere_update_irq(s);
            break;
        }
        case A_TC2LOAD: {
            psion_timer_load(&s->timers[1], value);
            break;
        }
        case A_TC2EOI: {
            s->intsr &= ~R_INTSR_TC2OI_MASK;
            windermere_update_irq(s);
            break;
        }
        case A_RTCDRL: {
            /* RTC counter: [16 bits][16 bits][6 bits]
            This updates the 2nd part
            */
            s->rtc_count = (((s->rtc_count / 64) & 0xffff0000) | (value & 0xffff)) * 64;
            break;
        }
        case A_RTCDRU: {
            /* RTC counter: [16 bits][16 bits][6 bits]
            This updates the 1st part
            */
            s->rtc_count = (((value & 0xffff) << 16) | ((s->rtc_count / 64) & 0xffff)) * 64;
            break;
        }
        case A_SSCR0: {
            break;
        }
        case A_KSCAN: {
            s->kscan = value & 0xff;
            break;
        }
        case A_LCD_DBAR1: {
            s->lcd_bar1 = value;
            qemu_log("windermere_periph_write: LCD_DBAR1=0x%08x\n", (uint32_t) value);
            psion5fb_set_base_addr(&s->fb, value);
            break;
        }
        default: {
            UNHANDLED_REG_DEBUG("windermere_periph_write: unhandled addr=%03x value=0x%08x\n", (uint32_t) offset, (uint32_t) value);
            break;
        }
    }
    if (log_pc)
    {
        // VCD_WRITE(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL), offset, value);
    }

}

static const MemoryRegionOps windermere_ops = {
        .read = windermere_periph_read,
        .write = windermere_periph_write,
        .endianness = DEVICE_LITTLE_ENDIAN
};

static uint64_t etna_read(void *opaque, hwaddr offset, unsigned size)
{
    WindermereState *s = WINDERMERE(opaque);
    // VCD_WRITE(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL), VCD_ETNA_READ(offset), 1);
    switch (offset) {
        case 0x8:
            return 0;
        case 0x9:
            return 1;
        case 0xc:
            return s->etna.wake1;
        case 0xd:
            return s->etna.wake2;
        case 0xf:
            return s->etna.wake3;
        default:
            UNHANDLED_REG_DEBUG("etna_read: unhandled addr=%03x\n", (uint32_t) offset);
            break;
    }
    return 0xff;
}

static void etna_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    WindermereState *s = WINDERMERE(opaque);
    // VCD_WRITE(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL), VCD_ETNA(offset), 1);
    switch (offset) {
        case 0xc:
            s->etna.wake1 = value;
            break;
        case 0xd:
            s->etna.wake2 = value;
            break;
        case 0xf:
            s->etna.wake3 = value;
            break;
        default:
            UNHANDLED_REG_DEBUG("etna_write: unhandled addr=%03x value=0x%08x\n", (uint32_t) offset, (uint32_t) value);
            break;
    }
}


static const MemoryRegionOps etna_ops = {
    .read = etna_read,
    .write = etna_write,
};


static uint64_t memory_read_empty(void *opaque, hwaddr addr, unsigned size)
{
    return size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffff;
}

static void memory_write_empty(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
}

static const MemoryRegionOps sram_bank_c0_ff_ops = {
    .read = &memory_read_empty,
    .write = &memory_write_empty,
    .endianness = DEVICE_LITTLE_ENDIAN
};

static void windermere_init(Object *obj)
{
    WindermereState *s = WINDERMERE(obj);

    MemoryRegion *address_space_mem = get_system_memory();

    /* 0x0000_0000 */
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom, NULL, "psion.rom", 16 * MiB, &error_abort);
    memory_region_add_subregion(address_space_mem, 0x00000000, rom);

    /* 0x1000_0000 */
    MemoryRegion *rom2 = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom2, NULL, "psion.rom2", 16 * MiB, &error_abort);
    memory_region_add_subregion(address_space_mem, 0x10000000, rom2);

    /* 0x2000_0000 */
    memory_region_init_io(&s->etna.iomem, obj, &etna_ops, s,
                          "etna", 0x1000);
    memory_region_add_subregion_overlap(address_space_mem, 0x20000000, &s->etna.iomem, 0);

    /* 0x8000_0000 */
    memory_region_init_io(&s->iomem, obj, &windermere_ops, s,
                          TYPE_WINDERMERE, 0x1000);
    memory_region_add_subregion_overlap(address_space_mem, 0x80000000, &s->iomem, 0);

    /* 0xc000_0000 -> 0xffff_ffff dummy placeholder, returning ff */
    MemoryRegion *sram_bank_c0_ff = g_new(MemoryRegion, 1);
    size_t c0_ff_size = 0x100000000 - 0xc0000000;
    memory_region_init_io(sram_bank_c0_ff, NULL, &sram_bank_c0_ff_ops, NULL, "psion.sram_c0_ff", c0_ff_size);
    memory_region_add_subregion_overlap(address_space_mem, 0xc0000000, sram_bank_c0_ff, 0);

    /* Based on linux-2.4.19-rmk2-5mx2.patch */
    MemoryRegion *sram_bank_c0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(sram_bank_c0, NULL, "psion.sram_c0", 8 * MiB, &error_abort);
    memory_region_add_subregion_overlap(address_space_mem, 0xc0000000, sram_bank_c0, 1);

    // MemoryRegion *sram_bank_c1 = g_new(MemoryRegion, 1);
    // memory_region_init_ram(sram_bank_c1, NULL, "psion.sram_c1", 8 * MiB, &error_abort);
    // memory_region_add_subregion_overlap(address_space_mem, 0xc1000000, sram_bank_c1, 1);

    // MemoryRegion *sram_bank_d0 = g_new(MemoryRegion, 1);
    // memory_region_init_alias(sram_bank_d0, NULL, "psion.sram_d0", sram_bank_c0, 0, 8 * MiB);
    // memory_region_add_subregion_overlap(address_space_mem, 0xd0000000, sram_bank_d0, 1);

    // MemoryRegion *sram_bank_d1 = g_new(MemoryRegion, 1);
    // memory_region_init_alias(sram_bank_d1, NULL, "psion.sram_d1", sram_bank_c0, 0, 8 * MiB);
    // memory_region_add_subregion_overlap(address_space_mem, 0xd1000000, sram_bank_d1, 1);


    size_t alias_step = 8 * MiB;
    for (int alias_index = 1; alias_index < 64; ++alias_index) {
        char name[32];
        MemoryRegion *sram_c0_alias = g_new(MemoryRegion, 1);
        snprintf(name, sizeof(name), "psion.sram_c0_alias_%d", alias_index);
        memory_region_init_alias(sram_c0_alias, NULL, name, sram_bank_c0, 0, 8 * MiB);
        memory_region_add_subregion_overlap(address_space_mem, 0xc0000000 + alias_index * alias_step, sram_c0_alias, 1);
    }

    timer_init_ns(&s->rtc_timer, QEMU_CLOCK_VIRTUAL, windermere_rtc_cb, s);

    for (int i = 0; i < 2; ++i) {
        char timer_name[32];
        snprintf(timer_name, sizeof(timer_name), "timer%d", i);
        object_initialize_child(obj, timer_name, &s->timers[i], TYPE_PSION_TIMER);
    }
    qdev_init_gpio_in_named(DEVICE(s), windermere_timer_cb, "timer_irq", 2);

    object_initialize_child(obj, "fb", &s->fb, TYPE_PSION5FB);
}


static void windermere_realize(DeviceState *dev, Error **errp)
{
    WindermereState *s = WINDERMERE(dev);
    s->irq = qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ);
    s->fiq = qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ);
    
    for (int i = 0; i < 2; ++i) {
        object_property_set_int(OBJECT(&s->timers[i]), "index", i, &error_fatal);
        qdev_connect_gpio_out(DEVICE(&s->timers[i]), 0,
                                qdev_get_gpio_in_named(DEVICE(s), "timer_irq", i));
        qdev_realize(DEVICE(&s->timers[i]), sysbus_get_default(), &error_fatal);
    }

    qdev_realize(DEVICE(&s->fb), sysbus_get_default(), &error_fatal);

    /* Set up touch callback to update touch coordinates in windermere */
    psion5fb_set_touch_callback(&s->fb, windermere_touch_update, s);
    
    /* Set up keyboard callback to update keyboard state in windermere */
    psion5fb_set_keyboard_callback(&s->fb, windermere_keyboard_update, s);

    // vcd_open(&vcd_file_info);
}

static void windermere_reset(DeviceState *dev)
{
    WindermereState *s = WINDERMERE(dev);
    ARMCPU *cpu = s->cpu;

    cpu_reset(CPU(cpu));
    timer_del(&s->rtc_timer);

    // set up the 64Hz RTC timer
    timer_mod_anticipate_ns(&s->rtc_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000 * 1000 * 1000 / 64);

    /* Initialize syncio state */
    s->syncio_request = 0;
    s->syncio_response = 0;
    s->ssi_read_counter = 0;
    s->touch_x = 0;
    s->touch_y = 0;
    
    /* Initialize keyboard state */
    s->kscan = 0;
    for (int i = 0; i < 8; i++) {
        s->keyboard_columns[i] = 0;
    }
}

static void windermere_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = windermere_reset;
    dc->realize = windermere_realize;
}


static const TypeInfo windermere_info = {
    .name = TYPE_WINDERMERE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(WindermereState),
    .instance_init = windermere_init,
    .class_init = windermere_class_init
};



static void psion_5mx_init(MachineState *machine)
{
    ARMCPU *cpu;

    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    cpu = ARM_CPU(object_new(machine->cpu_type));

    object_property_set_int(OBJECT(cpu), "midr", 0x41807100,
                            &error_fatal);
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);

    WindermereState *windermere = WINDERMERE(qdev_new(TYPE_WINDERMERE));
    windermere->cpu = cpu;
    sysbus_realize(SYS_BUS_DEVICE(windermere), &error_fatal);


    static struct arm_boot_info psion_s5_boot_info = {
            .loader_start = 0x0,
    };
    arm_load_kernel(cpu, machine, &psion_s5_boot_info);

    const char* firmware = "sysrom_5mx.bin";

    if (machine->firmware) {
        firmware = machine->firmware;
    }

    char *rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
    if (rom_binary == NULL) {
        error_report("Error: ROM code binary not found");
        exit(1);
    }

    qemu_log("Loading firmware '%s'\n", rom_binary);

    ssize_t size = load_image_targphys(rom_binary, 0, 16 * MiB);
    if (size < 0) {
        error_report("Error: could not load ROM binary '%s'", rom_binary);
        exit(1);
    }
    g_free(rom_binary);
}


static void psion_5mx_machine_init(MachineClass *mc)
{
    mc->desc = "Psion 5mx";
    mc->init = psion_5mx_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm710t");
    mc->default_ram_size = 16 * MiB;

}

DEFINE_MACHINE("psion_5mx", psion_5mx_machine_init)


static void psion_register_types(void)
{
    type_register_static(&windermere_info);
}

type_init(psion_register_types)
