#include "hw/arm/psion_timer.h"
#include "hw/qdev-core.h"
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
#include "psion_series5.h"
#include "psion_timer.h"

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

#define ENABLE_VCD_DEBUG
#ifdef ENABLE_VCD_DEBUG
#define VCD_WRITE(...) vcd_write(__VA_ARGS__)
#define VCD_FLUSH() vcd_flush()
#else
#define VCD_WRITE(...)
#define VCD_FLUSH()
#endif



#define VCD_READ(reg_) (0x10000 + (reg_))
#define VCD_ETNA(reg_) (0x20000 + (reg_))
#define VCD_ETNA_READ(reg_) (0x30000 + (reg_))


/* VCD signal definitions: one 32-bit signal for every register, one single-bit signal for "reading" events */
static const vcd_signal_info_t vcd_signals[] = {
    {.name = "PADR", .bits = 8, .index = A_PADR},
    {.name = "PADR_RD", .bits = 1, .index = VCD_READ(A_PADDR), .is_oneshot = true},
    {.name = "PBDR", .bits = 8, .index = A_PBDR},
    {.name = "PBDR_RD", .bits = 1, .index = VCD_READ(A_PBDR), .is_oneshot = true},
    {.name = "PCDR", .bits = 8, .index = A_PCDR},
    {.name = "PCDR_RD", .bits = 1, .index = VCD_READ(A_PCDR), .is_oneshot = true},
    {.name = "PDDR", .bits = 8, .index = A_PDDR},
    {.name = "PDDR_RD", .bits = 1, .index = VCD_READ(A_PDDR), .is_oneshot = true},
    {.name = "PADDR", .bits = 8, .index = A_PADDR},
    {.name = "PADDR_RD", .bits = 1, .index = VCD_READ(A_PADDR), .is_oneshot = true},
    {.name = "PBDDR", .bits = 8, .index = A_PBDDR},
    {.name = "PBDDR_RD", .bits = 1, .index = VCD_READ(A_PBDDR), .is_oneshot = true},
    {.name = "PCDDR", .bits = 8, .index = A_PCDDR},
    {.name = "PCDDR_RD", .bits = 1, .index = VCD_READ(A_PCDDR), .is_oneshot = true},
    {.name = "PDDDR", .bits = 8, .index = A_PDDDR},
    {.name = "PDDDR_RD", .bits = 1, .index = VCD_READ(A_PDDDR), .is_oneshot = true},
    {.name = "PEDR", .bits = 8, .index = A_PEDR},
    {.name = "PEDR_RD", .bits = 1, .index = VCD_READ(A_PEDR), .is_oneshot = true},
    {.name = "PEDDR", .bits = 8, .index = A_PEDDR},
    {.name = "PEDDR_RD", .bits = 1, .index = VCD_READ(A_PEDDR), .is_oneshot = true},
    {.name = "SYSCON", .bits = 32, .index = A_SYSCON},
    {.name = "SYSCON_RD", .bits = 1, .index = VCD_READ(A_SYSCON), .is_oneshot = true},
    {.name = "SYSFLG", .bits = 32, .index = A_SYSFLG},
    {.name = "SYSFLG_RD", .bits = 1, .index = VCD_READ(A_SYSFLG), .is_oneshot = true},
    {.name = "MEMCFG1", .bits = 32, .index = A_MEMCFG1},
    {.name = "MEMCFG1_RD", .bits = 1, .index = VCD_READ(A_MEMCFG1), .is_oneshot = true},
    {.name = "MEMCFG2", .bits = 32, .index = A_MEMCFG2},
    {.name = "MEMCFG2_RD", .bits = 1, .index = VCD_READ(A_MEMCFG2), .is_oneshot = true},
    {.name = "DRFPR", .bits = 32, .index = A_DRFPR},
    {.name = "DRFPR_RD", .bits = 1, .index = VCD_READ(A_DRFPR), .is_oneshot = true},
    {.name = "INTSR", .bits = 32, .index = A_INTSR},
    {.name = "INTSR_RD", .bits = 1, .index = VCD_READ(A_INTSR), .is_oneshot = true},
    {.name = "INTMR", .bits = 32, .index = A_INTMR},
    {.name = "INTMR_RD", .bits = 1, .index = VCD_READ(A_INTMR), .is_oneshot = true},
    {.name = "LCDCON", .bits = 32, .index = A_LCDCON},
    {.name = "LCDCON_RD", .bits = 1, .index = VCD_READ(A_LCDCON), .is_oneshot = true},
    {.name = "TC1D", .bits = 16, .index = A_TC1D},
    {.name = "TC1D_RD", .bits = 1, .index = VCD_READ(A_TC1D), .is_oneshot = true},
    {.name = "TC2D", .bits = 16, .index = A_TC2D},
    {.name = "TC2D_RD", .bits = 1, .index = VCD_READ(A_TC2D), .is_oneshot = true},
    {.name = "RTCDR", .bits = 32, .index = A_RTCDR},
    {.name = "RTCDR_RD", .bits = 1, .index = VCD_READ(A_RTCDR), .is_oneshot = true},
    {.name = "RTCMR", .bits = 32, .index = A_RTCMR},
    {.name = "RTCMR_RD", .bits = 1, .index = VCD_READ(A_RTCMR), .is_oneshot = true},
    {.name = "PMPCON", .bits = 32, .index = A_PMPCON},
    {.name = "PMPCON_RD", .bits = 1, .index = VCD_READ(A_PMPCON), .is_oneshot = true},
    {.name = "CODR", .bits = 32, .index = A_CODR},
    {.name = "CODR_RD", .bits = 1, .index = VCD_READ(A_CODR), .is_oneshot = true},
    {.name = "UARTDR", .bits = 32, .index = A_UARTDR},
    {.name = "UARTDR_RD", .bits = 1, .index = VCD_READ(A_UARTDR), .is_oneshot = true},
    {.name = "UBRLCR", .bits = 32, .index = A_UBRLCR},
    {.name = "UBRLCR_RD", .bits = 1, .index = VCD_READ(A_UBRLCR), .is_oneshot = true},
    {.name = "SYNCIO", .bits = 16, .index = A_SYNCIO},
    {.name = "SYNCIO_RD", .bits = 1, .index = VCD_READ(A_SYNCIO), .is_oneshot = true},
    {.name = "PALLSW", .bits = 32, .index = A_PALLSW},
    {.name = "PALLSW_RD", .bits = 1, .index = VCD_READ(A_PALLSW), .is_oneshot = true},
    {.name = "PALMSW", .bits = 32, .index = A_PALMSW},
    {.name = "PALMSW_RD", .bits = 1, .index = VCD_READ(A_PALMSW), .is_oneshot = true},
    {.name = "STFCLR", .bits = 1, .index = A_STFCLR, .is_oneshot = true},
    {.name = "STFCLR_RD", .bits = 1, .index = VCD_READ(A_STFCLR), .is_oneshot = true},
    {.name = "BLEOI", .bits = 1, .index = A_BLEOI, .is_oneshot = true},
    {.name = "BLEOI_RD", .bits = 1, .index = VCD_READ(A_BLEOI), .is_oneshot = true},
    {.name = "MCEOI", .bits = 1, .index = A_MCEOI, .is_oneshot = true},
    {.name = "MCEOI_RD", .bits = 1, .index = VCD_READ(A_MCEOI), .is_oneshot = true},
    {.name = "TEOI", .bits = 1, .index = A_TEOI, .is_oneshot = true},
    {.name = "TEOI_RD", .bits = 1, .index = VCD_READ(A_TEOI), .is_oneshot = true},
    {.name = "TC1EOI", .bits = 1, .index = A_TC1EOI, .is_oneshot = true},
    {.name = "TC1EOI_RD", .bits = 1, .index = VCD_READ(A_TC1EOI), .is_oneshot = true},
    {.name = "TC2EOI", .bits = 1, .index = A_TC2EOI, .is_oneshot = true},
    {.name = "TC2EOI_RD", .bits = 1, .index = VCD_READ(A_TC2EOI), .is_oneshot = true},
    {.name = "RTCEOI", .bits = 1, .index = A_RTCEOI, .is_oneshot = true},
    {.name = "RTCEOI_RD", .bits = 1, .index = VCD_READ(A_RTCEOI), .is_oneshot = true},
    {.name = "UMSEOI", .bits = 1, .index = A_UMSEOI, .is_oneshot = true},
    {.name = "UMSEOI_RD", .bits = 1, .index = VCD_READ(A_UMSEOI), .is_oneshot = true},
    {.name = "COEOI", .bits = 1, .index = A_COEOI, .is_oneshot = true},
    {.name = "COEOI_RD", .bits = 1, .index = VCD_READ(A_COEOI), .is_oneshot = true},
    {.name = "HALT", .bits = 1, .index = A_HALT, .is_oneshot = true},
    {.name = "HALT_RD", .bits = 1, .index = VCD_READ(A_HALT), .is_oneshot = true},
    {.name = "STDBY", .bits = 1, .index = A_STDBY, .is_oneshot = true},
    {.name = "STDBY_RD", .bits = 1, .index = VCD_READ(A_STDBY), .is_oneshot = true},
    {.name = "UNK_B00", .bits = 32, .index = A_UNK_B00},
    {.name = "UNK_B00_RD", .bits = 1, .index = VCD_READ(A_UNK_B00), .is_oneshot = true},
    {.name = "UNK_B04", .bits = 32, .index = A_UNK_B04},
    {.name = "UNK_B04_RD", .bits = 1, .index = VCD_READ(A_UNK_B04), .is_oneshot = true},
    {.name = "UNK_B08", .bits = 32, .index = A_UNK_B08},
    {.name = "UNK_B08_RD", .bits = 1, .index = VCD_READ(A_UNK_B08), .is_oneshot = true},
    {.name = "ETNA_08", .bits = 8, .index = VCD_ETNA(0x08)},
    {.name = "ETNA_08_RD", .bits = 1, .index = VCD_ETNA_READ(0x08), .is_oneshot = true},
    {.name = "ETNA_09", .bits = 8, .index = VCD_ETNA(0x09)},
    {.name = "ETNA_09_RD", .bits = 1, .index = VCD_ETNA_READ(0x09), .is_oneshot = true},
    {.name = "ETNA_0b", .bits = 8, .index = VCD_ETNA(0x0b)},
    {.name = "ETNA_0b_RD", .bits = 1, .index = VCD_ETNA_READ(0x0b), .is_oneshot = true},
    {.name = "ETNA_0c", .bits = 8, .index = VCD_ETNA(0x0c)},
    {.name = "ETNA_0c_RD", .bits = 1, .index = VCD_ETNA_READ(0x0c), .is_oneshot = true},
    {.name = "ETNA_0d", .bits = 8, .index = VCD_ETNA(0x0d)},
    {.name = "ETNA_0d_RD", .bits = 1, .index = VCD_ETNA_READ(0x0d), .is_oneshot = true},
    {.name = "ETNA_0f", .bits = 8, .index = VCD_ETNA(0x0f)},
    {.name = "ETNA_0f_RD", .bits = 1, .index = VCD_ETNA_READ(0x0f), .is_oneshot = true},
};
static const vcd_file_info_t vcd_file_info = {
    .signals = vcd_signals,
    .num_signals = sizeof(vcd_signals) / sizeof(vcd_signals[0]),
    .timescale = "us"
};



#define TYPE_CLPS7100 "clps7100"

OBJECT_DECLARE_SIMPLE_TYPE(Clps7100State, CLPS7100)

typedef struct Clps7100State {
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
    bool port_dir_inverted[5];

    /* timers */
    PsionTimerState timers[2];

    QEMUTimer rtc_timer;
    QEMUTimer wd_timer;

    /* syncio */
    uint16_t syncio_request;
    uint16_t syncio_response;

    /* interrupts */
    qemu_irq irq;
    qemu_irq fiq;
    uint32_t irqstatus;      /* not a real register, used to track changes to interrupt status */
    uint32_t fiqstatus;      /* same for fiq */

    /* misc state */
    uint32_t kscan;          /* keyboard scan bits */
    uint32_t kbd_data;       /* keyboard read lines */
    uint32_t sysflg;         /* system status flags register */
    uint32_t memcfg1;        /* expansion and ROM memory configuration register 1 */
    uint32_t memcfg2;        /* expansion and ROM memory configuration register 2 */
    uint32_t drfpr;          /* DRAM refresh period register */
    uint32_t intsr;          /* interrupt status register */
    uint32_t intmr;          /* interrupt mask register */
    uint32_t lcdcon;         /* LCD control register */
    uint64_t rtc_count;      /* rtc tick counter, 64 Hz */
    uint32_t rtcmr;          /* real time clock match register */
    uint32_t pmpcon;         /* DC/DC pump control register */
    uint32_t ublcr;          /* UART baud rate and line control register */
    uint32_t palette[2];       /* LCD palette (low & high) registers */
} Clps7100State;


static void clps7100_update_irq(Clps7100State* s)
{
    uint32_t new_intstatus = s->intsr & s->intmr;
    uint32_t new_irqstatus = new_intstatus & IRQ_INTERRUPTS;
    uint32_t new_fiqstatus = new_intstatus & FIQ_INTERRUPTS;
    if (new_irqstatus != s->irqstatus) {
        s->irqstatus = new_irqstatus;
        // VCD_WRITE(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL), A_INTSR, new_intstatus);
        if (new_irqstatus) {
            IRQ_DEBUG("clps7100_update_irq: raising IRQ, st=0x%04x mask=0x%04x raw=0x%04x\n", new_irqstatus, s->intmr, s->intsr);
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


static void clps7100_timer_cb(void *opaque, int n, int level)
{
    assert (n >= 0 && n < 2);
    Clps7100State *s = CLPS7100(opaque);
    uint32_t irq_mask = (n == 0) ? R_INTSR_TC1OI_MASK : R_INTSR_TC2OI_MASK;
    if (level) {
        s->intsr |= irq_mask;
    } else {
        s->intsr &= ~irq_mask;
    }
    clps7100_update_irq(s);
}

/* RTC clock value, in 64Hz ticks */
static uint64_t clps7100_get_rtc(Clps7100State* s)
{
    return s->rtc_count;
}


static void clps7100_rtc_cb(void* opaque)
{
    Clps7100State *s = CLPS7100(opaque);
    s->rtc_count++;
    RTC_DEBUG("clps7100_rtc_cb: rtc_count=%" PRId64 "\n", s->rtc_count);
    s->intsr |= R_INTSR_TINT_MASK;
    timer_mod_anticipate_ns(&s->rtc_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000 * 1000 * 1000 / 64);
    clps7100_update_irq(s);
    VCD_FLUSH();
}

static void clps7100_handle_syncio_request(Clps7100State* s, uint16_t request)
{
    uint8_t adc_config = (request) & 0xff;
    uint8_t bit_length = (request >> 8) & 0x1f;
    uint8_t smcken = (request >> 13) & 0x1;
    uint8_t txfrmen = (request >> 14) & 0x1;

    SYNCIO_DEBUG("clps7100_handle_syncio_request: request=0x%04x adc_config=0x%02x bit_length=%d smcken=%d txfrmen=%d\n",
            request, adc_config, bit_length, smcken, txfrmen);
    if (adc_config == 0xa1) {
        s->syncio_response = 1000;
    } else if (adc_config == 0x91) {
        s->syncio_response = 3000;
    } else if (adc_config == 0xD1) {
        s->syncio_response = 3000;
    } else {
        s->syncio_response = 0x900;
    }
    // switch (request) {
        // 0x00007091
        // 0x000070a1
        // 0x000070f1
        // 0x00006dce
        // 0x00006d0d

        // 0x00006d08
        // 0x00002000
    // }
    if (s->intmr & R_INTMR_SSEOTI_MASK) {
        s->intsr |= R_INTSR_SSEOTI_MASK;
        clps7100_update_irq(s);
    }
}

static uint64_t clps7100_periph_read(void *opaque, hwaddr offset, unsigned size)
{
    Clps7100State *s = CLPS7100(opaque);
    uint32_t pc = s->cpu->env.regs[15];
    bool log_pc = true;
    uint32_t result = 0;
    switch (offset) {
        case A_PADR: {
            log_pc = false;
            assert(size == 1);
            GPIO_DEBUG("clps7100_periph_read: PADR=0x%08x\n", (uint32_t) s->port_in[0]);
            result = (s->port_in[0] & 0x80) | (s->kbd_data  & 0x7f);
            break;
        }
        case A_PBDR: {
            assert(size == 1);
            GPIO_DEBUG("clps7100_periph_read: PBDR=0x%08x\n", (uint32_t) s->port_in[1]);
            result = s->port_in[1] | HW_OPEN_BIT;
            break;
        }
        case A_PCDR: {
            assert(size == 1);
            GPIO_DEBUG("clps7100_periph_read: PCDR=0x%08x\n", (uint32_t) s->port_in[2]);
            result = s->port_in[2];
            break;
        }
        case A_PDDR: {
            assert(size == 1);
            GPIO_DEBUG("clps7100_periph_read: PDDR=0x%08x\n", (uint32_t) s->port_in[3]);
            result = s->port_in[3];
            break;
        }
        case A_PADDR: {
            assert(size == 1);
            result = s->port_dir[0];
            break;
        }
        case A_PBDDR: {
            assert(size == 1);
            result = s->port_dir[1];
            break;
        }
        case A_PCDDR: {
            assert(size == 1);
            result = s->port_dir[2];
            break;
        }
        case A_PDDDR: {
            assert(size == 1);
            result = s->port_dir[3];
            break;
        }
        case A_PEDR: {
            assert(size == 1);
            GPIO_DEBUG("clps7100_periph_read: PEDR=0x%08x\n", (uint32_t) s->port_in[4]);
            result = s->port_in[4];
            break;
        }
        case A_PEDDR: {
            assert(size == 1);
            result = s->port_dir[4];
            break;
        }
        case A_SYSCON: {
            FIELD_DP32(result, SYSCON, KSCAN, s->kscan);
            FIELD_DP32(result, SYSCON, TC1M, s->timers[0].mode == TIMER_MODE_PRESCALE);
            FIELD_DP32(result, SYSCON, TC1S, s->timers[0].clk == TIMER_CLK_512KHZ);
            FIELD_DP32(result, SYSCON, TC2M, s->timers[1].mode == TIMER_MODE_PRESCALE);
            FIELD_DP32(result, SYSCON, TC2S, s->timers[1].clk == TIMER_CLK_512KHZ);
            break;
        }
        case A_SYSFLG: {
            log_pc = false;
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
            log_pc = false;
            result = s->intsr;
            break;
        }
        case A_INTMR: {
            log_pc = false;
            result = s->intmr;
            break;
        }
        case A_LCDCON: {
            result = s->lcdcon;
            break;
        }
        case A_TC1D: {
            result = psion_timer_get_val(&s->timers[0]);
            break;
        }
        case A_TC2D: {
            log_pc = false;
            result = psion_timer_get_val(&s->timers[1]);
            break;
        }
        case A_RTCDR: {
            log_pc = false;
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
        case A_SYNCIO: {
            SYNCIO_DEBUG("clps7100_periph_read: SYNCIO=0x%04x\n", (uint32_t) s->syncio_response);
            result = s->syncio_response;
            break;
        }
        case A_PALLSW: {
            result = s->palette[0];
            break;
        }
        case A_PALMSW: {
            result = s->palette[1];
            break;
        }
        default: {
            UNHANDLED_REG_DEBUG("clps7100_periph_read: unhandled addr=%03x !!!!!!!!!!!!!!!!!!\n", (uint32_t) offset);
            break;
        }
    }
    if (log_pc)
    {
        REG_DEBUG("clps7100_periph_read: addr=%03x result=0x%08x pc=0x%08x lr=0x%08x\n", (uint32_t) offset, (uint32_t) result, pc, s->cpu->env.regs[14]);
        VCD_WRITE(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL), VCD_READ(offset), 1);
    }
    return result;
}

static void clps7100_periph_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    Clps7100State *s = CLPS7100(opaque);
    uint32_t pc = s->cpu->env.regs[15];
    bool log_pc = true;
    // REG_DEBUG("clps7100_periph_write: addr=%03x value=0x%08x pc=0x%08x\n", (uint32_t) offset, (uint32_t) value, pc);
    switch (offset) {
        case A_PADR: {
            s->port_out[0] = value & 0xff;
            GPIO_DEBUG("clps7100_periph_write: PADR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PBDR: {
            s->port_out[1] = value & 0xff;
            GPIO_DEBUG("clps7100_periph_write: PBDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PCDR: {
            s->port_out[2] = value & 0xff;
            GPIO_DEBUG("clps7100_periph_write: PCDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PDDR: {
            s->port_out[3] = value & 0xff;
            GPIO_DEBUG("clps7100_periph_write: PDDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PADDR: {
            s->port_dir[0] = value & 0xff;
            GPIO_DEBUG("clps7100_periph_write: PADDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PBDDR: {
            s->port_dir[1] = value & 0xff;
            GPIO_DEBUG("clps7100_periph_write: PBDDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PCDDR: {
            s->port_dir[2] = value & 0xff;
            GPIO_DEBUG("clps7100_periph_write: PCDDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PDDDR: {
            s->port_dir[3] = value & 0xff;
            GPIO_DEBUG("clps7100_periph_write: PDDDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PEDR: {
            s->port_out[4] = value & 0xff;
            GPIO_DEBUG("clps7100_periph_write: PEDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_PEDDR: {
            s->port_dir[4] = value & 0xff;
            GPIO_DEBUG("clps7100_periph_write: PEDDR=0x%08x\n", (uint32_t) value);
            break;
        }
        case A_SYSCON: {
            s->kscan = FIELD_EX32(value, SYSCON, KSCAN);
            timer_clk_t tc1_clk = FIELD_EX32(value, SYSCON, TC1S) ? TIMER_CLK_512KHZ : TIMER_CLK_2KHZ;
            timer_clk_t tc2_clk = FIELD_EX32(value, SYSCON, TC2S) ? TIMER_CLK_512KHZ : TIMER_CLK_2KHZ;
            timer_mode_t tc1_mode = FIELD_EX32(value, SYSCON, TC1M) ? TIMER_MODE_PRESCALE : TIMER_MODE_FREE_RUNNING;
            timer_mode_t tc2_mode = FIELD_EX32(value, SYSCON, TC2M) ? TIMER_MODE_PRESCALE : TIMER_MODE_FREE_RUNNING;
            psion_timer_update_settings(&s->timers[0], tc1_clk, tc1_mode);
            psion_timer_update_settings(&s->timers[1], tc2_clk, tc2_mode);

            if (FIELD_EX32(value, SYSCON, LCDEN)) {
                qemu_log("clps7100_periph_write: SYSCON: !!!!!!!! LCD ON !!!!!!!!! \n");
            }
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
            psion_timer_load(&s->timers[0], value);
            break;
        }
        case A_TC2D: {
            psion_timer_load(&s->timers[1], value);
            break;
        }
        case A_RTCDR: {
            RTC_DEBUG("clps7100_periph_write: RTCDR=0x%08x\n", (uint32_t) value);
            s->rtc_count = value * 64;
            break;
        }
        case A_SYNCIO: {
            clps7100_handle_syncio_request(s, value);
            break;
        }
        case A_PALLSW: {
            s->palette[0] = value;
            break;
        }
        case A_PALMSW: {
            s->palette[1] = value;
            break;
        }
        case A_TC1EOI: {
            s->intsr &= ~R_INTSR_TC1OI_MASK;
            clps7100_update_irq(s);
            log_pc = false;
            break;
        }
        case A_TC2EOI: {
            s->intsr &= ~R_INTSR_TC2OI_MASK;
            clps7100_update_irq(s);
            log_pc = false;
            break;
        }
        case A_TEOI: {
            s->intsr &= ~R_INTSR_TINT_MASK;
            clps7100_update_irq(s);
            log_pc = false;
            break;
        }
        case A_HALT: {
            cpu_interrupt(CPU(s->cpu), CPU_INTERRUPT_HALT);
            log_pc = false;
            break;
        }
        case A_UNK_B00:
        case A_UNK_B04:
        case A_UNK_B08: {
            // some unknown registers, ignore them for now
            break;
        }
        default: {
            UNHANDLED_REG_DEBUG("clps7100_periph_write: unhandled addr=%03x value=0x%08x\n", (uint32_t) offset, (uint32_t) value);
            break;
        }
    }
    if (log_pc)
    {
        VCD_WRITE(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL), offset, value);
    }

}

static const MemoryRegionOps clps7100_ops = {
        .read = clps7100_periph_read,
        .write = clps7100_periph_write,
        .endianness = DEVICE_LITTLE_ENDIAN
};

static uint64_t etna_read(void *opaque, hwaddr offset, unsigned size)
{
    Clps7100State *s = CLPS7100(opaque);
    VCD_WRITE(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL), VCD_ETNA_READ(offset), 1);
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
    Clps7100State *s = CLPS7100(opaque);
    VCD_WRITE(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL), VCD_ETNA(offset), 1);
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

    /* 0x2000_0000 */
    memory_region_init_io(&s->etna.iomem, obj, &etna_ops, s,
                          "etna", 0x1000);
    memory_region_add_subregion_overlap(address_space_mem, 0x20000000, &s->etna.iomem, 0);

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

    timer_init_ns(&s->rtc_timer, QEMU_CLOCK_VIRTUAL, clps7100_rtc_cb, s);

    for (int i = 0; i < 2; ++i) {
        char timer_name[32];
        snprintf(timer_name, sizeof(timer_name), "timer%d", i);
        object_initialize_child(obj, timer_name, &s->timers[i], TYPE_PSION_TIMER);
    }
    qdev_init_gpio_in_named(DEVICE(s), clps7100_timer_cb, "timer_irq", 2);
}


static void clps7100_realize(DeviceState *dev, Error **errp)
{
    Clps7100State *s = CLPS7100(dev);
    s->irq = qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ);
    s->fiq = qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ);
    

    
    for (int i = 0; i < 2; ++i) {
        object_property_set_int(OBJECT(&s->timers[i]), "index", i, &error_fatal);
        qdev_connect_gpio_out(DEVICE(&s->timers[i]), 0,
                                qdev_get_gpio_in_named(DEVICE(s), "timer_irq", i));
        qdev_realize(DEVICE(&s->timers[i]), sysbus_get_default(), &error_fatal);
    }

    vcd_open(&vcd_file_info);
}

static void clps7100_reset(DeviceState *dev)
{
    Clps7100State *s = CLPS7100(dev);
    ARMCPU *cpu = s->cpu;

    cpu_reset(CPU(cpu));
    timer_del(&s->timers[0].timer);
    timer_del(&s->timers[1].timer);
    timer_del(&s->rtc_timer);

    s->timers[0].base_value = 0xffff;
    s->timers[0].interval = 0xffff;
    s->timers[1].base_value = 0xffff;
    s->timers[1].interval = 0xffff;
    // set up the 64Hz RTC timer
    timer_mod_anticipate_ns(&s->rtc_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000 * 1000 * 1000 / 64);
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
