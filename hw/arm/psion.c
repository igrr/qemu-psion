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

#define ENABLE_TIMER_DEBUG
#ifdef ENABLE_TIMER_DEBUG
#define TIMER_DEBUG(fmt, ...) qemu_log("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define TIMER_DEBUG(fmt, ...)
#endif


/* CL-PS7110 peripheral registers */

REG8(PADR, 0x00)
REG8(PBDR, 0x01)
REG8(PCDR, 0x02)
REG8(PDDR, 0x03)
REG8(PADDR, 0x40)
REG8(PBDDR, 0x41)
REG8(PCDDR, 0x42)
REG8(PDDDR, 0x43)
REG8(PEDR, 0x80)
REG8(PEDDR, 0xC0)


REG32(SYSCON, 0x100)
    FIELD(SYSCON, KSCAN, 0, 4)      /* keyboard scan column selection */
    FIELD(SYSCON, TC1M, 4, 1)       /* timer 1 mode: prescale (1) or free-running (0) */
    FIELD(SYSCON, TC1S, 5, 1)       /* timer 1 source: 512kHz (1) or 2kHz (0) */
    FIELD(SYSCON, TC2M, 6, 1)       /* timer 2 mode: prescale (1) or free-running (0) */
    FIELD(SYSCON, TC2S, 7, 1)       /* timer 2 source: 512kHz (1) or 2kHz (0) */
    FIELD(SYSCON, UARTEN, 8, 1)
    FIELD(SYSCON, BZTOG, 9, 1)
    FIELD(SYSCON, BZMOD, 10, 1)
    FIELD(SYSCON, DBGEN, 11, 1)
    FIELD(SYSCON, LCDEN, 12, 1)
    FIELD(SYSCON, CDENTX, 13, 1)
    FIELD(SYSCON, CDENRX, 14, 1)
    FIELD(SYSCON, SIREN, 15, 1)
    FIELD(SYSCON, ADCKSEL, 16, 2)
    FIELD(SYSCON, EXCKEN, 18, 1)
    FIELD(SYSCON, WAKEDIS, 19, 1)
    FIELD(SYSCON, IRTXM, 20, 1)

REG32(SYSFLG, 0x140)
    FIELD(SYSFLG, MCDR, 0, 1)       /* status of media change input */
    FIELD(SYSFLG, DCDET, 1, 1)      /* set to 1 if on external power */
    FIELD(SYSFLG, WUDR, 2, 1)       /* non-latched state of wakeup signal */
    FIELD(SYSFLG, WUON, 3, 1)       /* 1 if system came out of standby due to the wakeup signal */
    FIELD(SYSFLG, DID, 4, 4)        /* last state of display data lines before enabling the LCD controller */
    FIELD(SYSFLG, CTS, 8, 1)        /* current state of CTS input */
    FIELD(SYSFLG, DSR, 9, 1)        /* current state of DSR input */
    FIELD(SYSFLG, DCD, 10, 1)       /* current state of DCD input */
    FIELD(SYSFLG, UBUSY, 11, 1)     /* UART is busy transmitting */
    FIELD(SYSFLG, NBFLG, 12, 1)     /* set if a rising edge occured on NBATCHG */
    FIELD(SYSFLG, RSTFLG, 13, 1)    /* reset flag (reset button pressed) */
    FIELD(SYSFLG, PFFLG, 14, 1)     /* power fail flag  */
    FIELD(SYSFLG, CLDFLG, 15, 1)    /* cold start flag */
    FIELD(SYSFLG, RTCDIV, 16, 6)    /* number of 64Hz ticks since the last RTC increment */
    FIELD(SYSFLG, URXFE, 22, 1)     /* UART receive FIFO is empty */
    FIELD(SYSFLG, UTXFF, 23, 1)     /* UART transmit FIFO is full */
    FIELD(SYSFLG, CRXFE, 24, 1)     /* codec receive FIFO is empty */
    FIELD(SYSFLG, CTXFF, 25, 1)     /* codec transmit FIFO is full */
    FIELD(SYSFLG, SSIBUSY, 26, 1)       /* synchronous serial interface is busy */
    FIELD(SYSFLG, BOOT8BIT, 27, 1)      /* 1 if booting from 8-bit ROM */
    FIELD(SYSFLG, VERID, 30, 2)         /* CLPS-7110 version ID */

REG32(MEMCFG1, 0x180)
REG32(MEMCFG2, 0x1C0)
REG32(DRFPR, 0x200)

REG32(INTSR, 0x240)
    FIELD(INTSR, EXTFIQ, 0, 1)      /* external fast interrupt */
    FIELD(INTSR, BLINT, 1, 1)       /* battery low interrupt */
    FIELD(INTSR, WEINT, 2, 1)       /* watchdog expired interrupt */
    FIELD(INTSR, MCINT, 3, 1)       /* media change interrupt */
    FIELD(INTSR, CSINT, 4, 1)       /* codec interrupt */
    FIELD(INTSR, EINT1, 5, 1)       /* external interrupt 1 */
    FIELD(INTSR, EINT2, 6, 1)       /* external interrupt 2 */
    FIELD(INTSR, EINT3, 7, 1)       /* external interrupt 3 */
    FIELD(INTSR, TC1OI, 8, 1)       /* timer 1 overflow interrupt */
    FIELD(INTSR, TC2OI, 9, 1)       /* timer 2 overflow interrupt */
    FIELD(INTSR, RTCMI, 10, 1)      /* RTC match interrupt */
    FIELD(INTSR, TINT, 11, 1)       /* tick interrupt */
    FIELD(INTSR, UTXINT, 12, 1)     /* UART transmit FIFO half-empty */
    FIELD(INTSR, URXINT1, 13, 1)    /* UART receive FIFO half-full */
    FIELD(INTSR, UMSINT, 14, 1)     /* UART modem status changed */
    FIELD(INTSR, SSEOTI, 15, 1)     /* synchronous serial interface end of transfer */

REG32(INTMR, 0x280)
    FIELD(INTMR, EXTFIQ, 0, 1)      /* external fast interrupt */
    FIELD(INTMR, BLINT, 1, 1)       /* battery low interrupt */
    FIELD(INTMR, WEINT, 2, 1)       /* watchdog expired interrupt */
    FIELD(INTMR, MCINT, 3, 1)       /* media change interrupt */
    FIELD(INTMR, CSINT, 4, 1)       /* codec interrupt */
    FIELD(INTMR, EINT1, 5, 1)       /* external interrupt 1 */
    FIELD(INTMR, EINT2, 6, 1)       /* external interrupt 2 */
    FIELD(INTMR, EINT3, 7, 1)       /* external interrupt 3 */
    FIELD(INTMR, TC1OI, 8, 1)       /* timer 1 overflow interrupt */
    FIELD(INTMR, TC2OI, 9, 1)       /* timer 2 overflow interrupt */
    FIELD(INTMR, RTCMI, 10, 1)      /* RTC match interrupt */
    FIELD(INTMR, TINT, 11, 1)       /* tick interrupt */
    FIELD(INTMR, UTXINT, 12, 1)     /* UART transmit FIFO half-empty */
    FIELD(INTMR, URXINT1, 13, 1)    /* UART receive FIFO half-full */
    FIELD(INTMR, UMSINT, 14, 1)     /* UART modem status changed */
    FIELD(INTMR, SSEOTI, 15, 1)     /* synchronous serial interface end of transfer */

#define FIQ_INTERRUPTS (R_INTSR_EXTFIQ_MASK | R_INTSR_BLINT_MASK | R_INTSR_WEINT_MASK | R_INTSR_MCINT_MASK)
#define IRQ_INTERRUPTS (R_INTSR_CSINT_MASK | \
                        R_INTSR_EINT1_MASK | R_INTSR_EINT2_MASK | \
                        R_INTSR_EINT3_MASK | R_INTSR_TC1OI_MASK | \
                        R_INTSR_TC2OI_MASK | R_INTSR_RTCMI_MASK | \
                        R_INTSR_TINT_MASK | R_INTSR_UTXINT_MASK | \
                        R_INTSR_URXINT1_MASK | R_INTSR_UMSINT_MASK | \
                        R_INTSR_SSEOTI_MASK)

REG32(LCDCON, 0x2C0)
    FIELD(LCDCON, VBUFSIZE, 0, 13)      /* sizeof(video_buf) * 8 / 128 - 1 */
    FIELD(LCDCON, LINE_LEN, 13, 6)      /* num_pixels_in_line / 16 - 1 */
    FIELD(LCDCON, PIX_PRESCALE, 19, 6)  /* 526628 / pixels in display - 1 (i.e. 36.864 MHz clock) */
    FIELD(LCDCON, AC_PRESCALE, 25, 5)   /* LCD AC bias frequency */
    FIELD(LCDCON, GSEN, 30, 1)          /* Grayscale enable. If not set, pixel state = bit state. */
    FIELD(LCDCON, GSMD, 31, 1)          /* Grayscale mode. 4bpp if set, 2bpp if cleared. */

REG32(TC1D, 0x300)
REG32(TC2D, 0x340)
REG32(RTCDR, 0x380)
REG32(RTCMR, 0x3C0)
REG32(PMPCON, 0x400)
REG32(CODR, 0x440)
REG32(UARTDR, 0x480)
REG32(UBRLCR, 0x4C0)
REG32(SYNCIO, 0x500)
REG32(PALLSW, 0x540)
REG32(PALMSW, 0x580)
REG32(STFCLR, 0x5C0)
REG32(BLEOI, 0x600)
REG32(MCEOI, 0x640)
REG32(TEOI, 0x680)
REG32(TC1EOI, 0x6C0)
REG32(TC2EOI, 0x700)
REG32(RTCEOI, 0x740)
REG32(UMSEOI, 0x780)
REG32(COEOI, 0x7C0)
REG32(HALT, 0x800)
REG32(STDBY, 0x840)


typedef enum timer_clk_t {
    TIMER_CLK_512KHZ = 1,
    TIMER_CLK_2KHZ = 0,
} timer_clk_t;

typedef enum timer_mode_t {
    TIMER_MODE_PRESCALE = 1,
    TIMER_MODE_FREE_RUNNING = 0,
} timer_mode_t;

typedef struct clps7100_timer_t {
    timer_clk_t clk;
    timer_mode_t mode;
    uint16_t interval;
    uint16_t base_value;
    int64_t base_ns;
    QEMUTimer timer;
    uint32_t irq_mask;
    int index;
    void *parent;
} clps7100_timer_t;

#define TYPE_CLPS7100 "clps7100"

OBJECT_DECLARE_SIMPLE_TYPE(Clps7100State, CLPS7100)

typedef struct Clps7100State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMCPU *cpu;
    MemoryRegion iomem;

    /* gpio ports A-E */
    uint8_t port_out[5];
    uint8_t port_dir[5];
    uint8_t port_in[5];
    bool port_dir_inverted[5];

    /* timers */
    clps7100_timer_t timers[2];

    QEMUTimer rtc_timer;
    QEMUTimer wd_timer;

    /* interrupts */
    qemu_irq irq;
    qemu_irq fiq;
    uint32_t irqstatus;      /* not a real register, used to track changes to interrupt status */
    uint32_t fiqstatus;      /* same for fiq */

    /* misc state */
    uint32_t kscan;          /* value from the keyboard */
    uint32_t sysflg;         /* system status flags register */
    uint32_t memcfg1;        /* expansion and ROM memory configuration register 1 */
    uint32_t memcfg2;        /* expansion and ROM memory configuration register 2 */
    uint32_t drfpr;          /* DRAM refresh period register */
    uint32_t intsr;          /* interrupt status register */
    uint32_t intmr;          /* interrupt mask register */
    uint32_t lcdcon;         /* LCD control register */
    uint32_t rtcmr;          /* real time clock match register */
    uint32_t pmpcon;         /* DC/DC pump control register */
    uint32_t ublcr;          /* UART baud rate and line control register */
    uint64_t palette;       /* LCD palette (low & high) registers */
} Clps7100State;


static void clps7100_update_irq(Clps7100State* s)
{
    uint32_t new_intstatus = s->intsr & s->intmr;
    uint32_t new_irqstatus = new_intstatus & IRQ_INTERRUPTS;
    uint32_t new_fiqstatus = new_intstatus & FIQ_INTERRUPTS;
    if (new_irqstatus != s->irqstatus) {
        s->irqstatus = new_irqstatus;
        if (new_irqstatus) {
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

/* RTC clock value, in 64Hz ticks */
static uint64_t clps7100_get_rtc(Clps7100State* s)
{
    return muldiv64(qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL), 64, 1000);
}

static int16_t clps7100_get_timer_val(clps7100_timer_t* timer)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t delta_us = now - timer->base_ns / 1000;
    uint32_t freq_khz = (timer->clk == TIMER_CLK_512KHZ) ? 512 : 2;
    int64_t ticks = timer->base_value - (int64_t) muldiv64(delta_us, freq_khz, 1000);
    if (ticks < 0) {
        TIMER_DEBUG("timer %d underrun, value=%" PRId64 "\n", timer->index, ticks);
        ticks = 0;
    } else if (ticks > 0xffff) {
        TIMER_DEBUG("timer %d overrun, value=%" PRId64 "\n", timer->index, ticks);
        ticks = 0xffff;
    }
    return (int16_t) ticks;
}

static void clps7100_timer_update_alarm(clps7100_timer_t* timer)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int32_t ticks_to_zero = timer->base_value;
    int64_t delta_ns = muldiv64(ticks_to_zero, 1000 * 1000, (timer->clk == TIMER_CLK_512KHZ) ? 512 : 2);
    timer_mod_anticipate_ns(&timer->timer, now + delta_ns);
}

static void clps7100_timer_update_settings(clps7100_timer_t *timer, timer_clk_t clk, timer_mode_t mode)
{
    if (timer->clk == clk && timer->mode == mode) {
        /* no change */
        return;
    }
    TIMER_DEBUG("%s: timer=%d clk=%s mode=%s\n", __func__,
            timer->index, clk == TIMER_CLK_512KHZ ? "512KHZ" : "2KHZ", mode == TIMER_MODE_PRESCALE ? "PRESCALE" : "FREE_RUNNING");

    timer->clk = clk;
    timer->mode = mode;
    clps7100_timer_update_alarm(timer);
}

static void clps7100_timer_load(clps7100_timer_t *timer, uint32_t new_val)
{
    timer->interval = new_val;
    timer->base_value = new_val;
    timer->base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    clps7100_timer_update_alarm(timer);
}


static void clps7100_timer_cb(void* opaque)
{
    clps7100_timer_t *timer = (clps7100_timer_t*) opaque;
    Clps7100State *s = CLPS7100(timer->parent);

    if (timer->mode == TIMER_MODE_PRESCALE) {
        timer->base_value = timer->interval;
    } else {
        timer->base_value = 0xffff;
    }
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer->base_ns = now;
    TIMER_DEBUG("%s: timer %d fired, now=%" PRId64 " new base_ns=%" PRId64 " new base_value=%d\n",
            __func__, timer->index, now, timer->base_ns, timer->base_value);
    clps7100_timer_update_alarm(timer);

    s->intsr |= timer->irq_mask;
    clps7100_update_irq(s);
}

static uint64_t clps7100_periph_read(void *opaque, hwaddr offset, unsigned size)
{
    Clps7100State *s = CLPS7100(opaque);
    uint32_t result = 0;
    switch (offset) {
        case A_PADR: {
            result = s->port_in[0] |
                     (s->port_in[1] << 8) |
                     (s->port_in[2] << 16) |
                     (s->port_in[3] << 24);
            break;
        }
        case A_PADDR: {
            result = s->port_dir[0] |
                     (s->port_dir[1] << 8) |
                     (s->port_dir[2] << 16) |
                     (s->port_dir[3] << 24);
            break;
        }
        case A_PEDR: {
            result = s->port_in[4];
            break;
        }
        case A_PEDDR: {
            result = s->port_dir[4];
            break;
        }
        case A_SYSCON: {
            FIELD_DP32(result, SYSCON, KSCAN, s->kscan);
            FIELD_DP32(result, SYSCON, TC1M, s->timers[0].mode == TIMER_MODE_PRESCALE);
            FIELD_DP32(result, SYSCON, TC1S, s->timers[0].clk == TIMER_CLK_512KHZ);
            FIELD_DP32(result, SYSCON, TC2M, s->timers[1].mode == TIMER_MODE_PRESCALE);
            FIELD_DP32(result, SYSCON, TC2S, s->timers[1].clk == TIMER_CLK_512KHZ);
            TIMER_DEBUG("%s: SYSCON: KSCAN=%d TC1M=%d TC1S=%d TC2M=%d TC2S=%d\n",
                    __func__, s->kscan, s->timers[0].mode, s->timers[0].clk, s->timers[1].mode, s->timers[1].clk);
            break;
        }
        case A_SYSFLG: {
            FIELD_DP32(result, SYSFLG, DCDET, 1);

            FIELD_DP32(result, SYSFLG, NBFLG, 0);
            FIELD_DP32(result, SYSFLG, RSTFLG, 0);
            FIELD_DP32(result, SYSFLG, PFFLG, 0);
            FIELD_DP32(result, SYSFLG, CLDFLG, 0);
            FIELD_DP32(result, SYSFLG, RTCDIV, clps7100_get_rtc(s) % 64);
            break;
        }

        case A_MEMCFG1: {
            result = s->memcfg1;
            break;
        }
        case A_MEMCFG2: {
            result = s->memcfg2;
            break;
        }
        case A_DRFPR: {
            result = s->drfpr;
            break;
        }

        case A_INTSR: {
            result = s->intsr;
            break;
        }
        case A_INTMR: {
            result = s->intmr;
            break;
        }
        case A_LCDCON: {
            result = s->lcdcon;
            break;
        }
        case A_TC1D: {
            result = clps7100_get_timer_val(&s->timers[0]);
            break;
        }
        case A_TC2D: {
            result = clps7100_get_timer_val(&s->timers[1]);
            break;
        }
        case A_RTCDR: {
            result = clps7100_get_rtc(s) / 64;
            break;
        }
        case A_RTCMR: {
            result = s->rtcmr;
            break;
        }
        case A_PMPCON: {
            result = s->pmpcon;
            break;
        }

        default: {
            qemu_log("clps7100_periph_read: unhandled addr=%03x\n", (uint32_t) offset);
            break;
        }
    }
    return result;
}

static void clps7100_periph_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    Clps7100State *s = CLPS7100(opaque);
    switch (offset) {
        case A_PADR: {
            s->port_out[0] = value & 0xff;
            qemu_log("clps7100_periph_write: PADR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PBDR: {
            s->port_out[1] = value & 0xff;
            break;
        }
        case A_PCDR: {
            s->port_out[2] = value & 0xff;
            break;
        }
        case A_PDDR: {
            s->port_out[3] = value & 0xff;
            break;
        }
        case A_PADDR: {
            s->port_dir[0] = value & 0xff;
            break;
        }
        case A_PBDDR: {
            s->port_dir[1] = value & 0xff;
            break;
        }
        case A_PCDDR: {
            s->port_dir[2] = value & 0xff;
            break;
        }
        case A_PDDDR: {
            s->port_dir[3] = value & 0xff;
            break;
        }
        case A_PEDR: {
            s->port_out[4] = value & 0xff;
            break;
        }
        case A_PEDDR: {
            s->port_dir[4] = value & 0xff;
            break;
        }
        case A_SYSCON: {
            s->kscan = FIELD_EX32(value, SYSCON, KSCAN);
            timer_clk_t tc1_clk = FIELD_EX32(value, SYSCON, TC1S) ? TIMER_CLK_512KHZ : TIMER_CLK_2KHZ;
            timer_clk_t tc2_clk = FIELD_EX32(value, SYSCON, TC2S) ? TIMER_CLK_512KHZ : TIMER_CLK_2KHZ;
            timer_mode_t tc1_mode = FIELD_EX32(value, SYSCON, TC1M) ? TIMER_MODE_PRESCALE : TIMER_MODE_FREE_RUNNING;
            timer_mode_t tc2_mode = FIELD_EX32(value, SYSCON, TC2M) ? TIMER_MODE_PRESCALE : TIMER_MODE_FREE_RUNNING;
            TIMER_DEBUG("%s: SYSCON: KSCAN=%d TC1M=%d TC1S=%d TC2M=%d TC2S=%d\n",
                    __func__, s->kscan, tc1_mode, tc1_clk, tc2_mode, tc2_clk);
            clps7100_timer_update_settings(&s->timers[0], tc1_clk, tc1_mode);
            clps7100_timer_update_settings(&s->timers[1], tc2_clk, tc2_mode);
            break;
        }
        case A_MEMCFG1: {
            s->memcfg1 = value;
            break;
        }
        case A_MEMCFG2: {
            s->memcfg2 = value;
            break;
        }
        case A_DRFPR: {
            s->drfpr = value;
            break;
        }
        case A_INTMR: {
            s->intmr = value;
            clps7100_update_irq(s);
            break;
        }
        case A_LCDCON: {
            s->lcdcon = value;
            qemu_log("clps7100_periph_write: LCDCON=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_TC1D: {
            TIMER_DEBUG("clps7100_periph_write: TC1D=0x%08x\n", (uint32_t) value);
            clps7100_timer_load(&s->timers[0], value);
            break;
        }
        case A_TC2D: {
            TIMER_DEBUG("clps7100_periph_write: TC2D=0x%08x\n", (uint32_t) value);
            clps7100_timer_load(&s->timers[1], value);
            break;
        }
        case A_TC1EOI: {
            s->intsr &= ~R_INTSR_TC1OI_MASK;
            clps7100_update_irq(s);
            break;
        }
        case A_TC2EOI: {
            s->intsr &= ~R_INTSR_TC2OI_MASK;
            clps7100_update_irq(s);
            break;
        }
        default: {
            qemu_log("clps7100_periph_write: unhandled addr=%03x value=0x%08x\n", (uint32_t) offset, (uint32_t) value);
        }
    }

}

static const MemoryRegionOps clps7100_ops = {
        .read = clps7100_periph_read,
        .write = clps7100_periph_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .impl = {
                .min_access_size = 4,
                .max_access_size = 4,
                .unaligned = false
        },
};

static uint64_t etna_read(void *opaque, hwaddr offset, unsigned size)
{
    qemu_log("etna_read: unhandled addr=%03x\n", (uint32_t) offset);
    return 0;
}

static void etna_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    qemu_log("etna_write: unhandled addr=%03x value=0x%08x\n", (uint32_t) offset, (uint32_t) value);
}


static const MemoryRegionOps etna_ops = {
    .read = etna_read,
    .write = etna_write,
};

static void clps7100_init(Object *obj)
{
    Clps7100State *s = CLPS7100(obj);

    MemoryRegion *address_space_mem = get_system_memory();

    /* 0x0000_0000 */
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom, NULL, "psion.rom", 8 * MiB, &error_abort);
    memory_region_add_subregion(address_space_mem, 0x00000000, rom);

    /* 0x1000_0000 */
    MemoryRegion *rom2 = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom2, NULL, "psion.rom2", 8 * MiB, &error_abort);
    memory_region_add_subregion(address_space_mem, 0x10000000, rom2);

    /* 0x8000_0000 */
    memory_region_init_io(&s->iomem, obj, &clps7100_ops, s,
                          TYPE_CLPS7100, 0x1000);
    memory_region_add_subregion_overlap(address_space_mem, 0x80000000, &s->iomem, 0);

    /* 0xc000_0000 and 0xd000_0000 */
    MemoryRegion *sram_bank_c0 = g_new(MemoryRegion, 1);
    MemoryRegion *sram_bank_d0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(sram_bank_c0, NULL, "psion.sram_c0", 4 * MiB, &error_abort);
    memory_region_init_ram(sram_bank_d0, NULL, "psion.sram_d0", 4 * MiB, &error_abort);

    memory_region_add_subregion(address_space_mem, 0xc0000000, sram_bank_c0);
    memory_region_add_subregion(address_space_mem, 0xd0000000, sram_bank_d0);

    for (int alias_index = 1; alias_index < 4; ++alias_index) {
        char name[32];
        MemoryRegion *sram_c0_alias = g_new(MemoryRegion, 1);
        snprintf(name, sizeof(name), "psion.sram_c0_alias_%d", alias_index);
        memory_region_init_alias(sram_c0_alias, NULL, name, sram_bank_c0, 0, 4 * MiB);
        memory_region_add_subregion(address_space_mem, 0xc0000000 + alias_index * 4 * MiB, sram_c0_alias);

        MemoryRegion *sram_d0_alias = g_new(MemoryRegion, 1);
        snprintf(name, sizeof(name), "psion.sram_d0_alias_%d", alias_index);
        memory_region_init_alias(sram_d0_alias, NULL, name, sram_bank_d0, 0, 4 * MiB);
        memory_region_add_subregion(address_space_mem, 0xd0000000 + alias_index * 4 * MiB, sram_d0_alias);
    }

    /* 0xe000_0000 */
    MemoryRegion *sram_bank_e0 = g_new(MemoryRegion, 1);
    memory_region_init_rom(sram_bank_e0, NULL, "psion.sram_e0", 4 * MiB, &error_abort);
    memory_region_add_subregion(address_space_mem, 0xe0000000, sram_bank_e0);

    /* 0xf000_0000 */
    MemoryRegion *sram_bank_f0 = g_new(MemoryRegion, 1);
    memory_region_init_rom(sram_bank_f0, NULL, "psion.sram_f0", 4 * MiB, &error_abort);
    memory_region_add_subregion(address_space_mem, 0xf0000000, sram_bank_f0);

    timer_init_ns(&s->timers[0].timer, QEMU_CLOCK_VIRTUAL, clps7100_timer_cb, &s->timers[0]);
    timer_init_ns(&s->timers[1].timer, QEMU_CLOCK_VIRTUAL, clps7100_timer_cb, &s->timers[1]);
    s->timers[0].parent = s;
    s->timers[0].index = 0;
    s->timers[0].irq_mask = R_INTSR_TC1OI_MASK;
    s->timers[0].base_value = 0xffff;
    s->timers[1].parent = s;
    s->timers[1].index = 1;
    s->timers[1].irq_mask = R_INTSR_TC2OI_MASK;
    s->timers[1].base_value = 0xffff;
}


static void clps7100_realize(DeviceState *dev, Error **errp)
{
    Clps7100State *s = CLPS7100(dev);
    s->irq = qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ);
    s->fiq = qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ);
}

static void clps7100_reset(DeviceState *dev)
{
    Clps7100State *s = CLPS7100(dev);
    ARMCPU *cpu = s->cpu;

    cpu_reset(CPU(cpu));
    timer_del(&s->timers[0].timer);
    timer_del(&s->timers[1].timer);

    s->timers[0].base_value = 0xffff;
    s->timers[0].interval = 0xffff;
    s->timers[1].base_value = 0xffff;
    s->timers[1].interval = 0xffff;
}

static void clps7100_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = clps7100_reset;
    dc->realize = clps7100_realize;
}


static const TypeInfo clps7100_info = {
    .name = TYPE_CLPS7100,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Clps7100State),
    .instance_init = clps7100_init,
    .class_init = clps7100_class_init
};



static void psion_s5_init(MachineState *machine)
{
    ARMCPU *cpu;

    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    cpu = ARM_CPU(cpu_create(machine->cpu_type));

    Clps7100State *clps7100 = CLPS7100(qdev_new(TYPE_CLPS7100));
    clps7100->cpu = cpu;
    sysbus_realize(SYS_BUS_DEVICE(clps7100), &error_fatal);


    static struct arm_boot_info psion_s5_boot_info = {
            .loader_start = 0x0,
    };
    arm_load_kernel(cpu, machine, &psion_s5_boot_info);

    char *rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, "sysrom_series5.bin");
    if (rom_binary == NULL) {
        error_report("Error: ROM code binary not found");
        exit(1);
    }

    ssize_t size = load_image_targphys(rom_binary, 0, 8 * MiB);
    if (size < 0) {
        error_report("Error: could not load ROM binary '%s'", rom_binary);
        exit(1);
    }
    g_free(rom_binary);
}


static void psion_s5_machine_init(MachineClass *mc)
{
    mc->desc = "Psion Series 5";
    mc->init = psion_s5_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm710a");
    mc->default_ram_size = 8 * MiB;

}

DEFINE_MACHINE("psion_s5", psion_s5_machine_init)


static void psion_register_types(void)
{
    type_register_static(&clps7100_info);
}

type_init(psion_register_types)
