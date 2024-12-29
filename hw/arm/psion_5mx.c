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
#include "psion_5mx.h"
#include "psion_timer.h"
#include "hw/qdev-core.h"


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
    uint16_t syncio_request;
    uint16_t syncio_response;

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

static void windermere_handle_syncio_request(WindermereState* s, uint16_t request)
{
    uint8_t adc_config = (request) & 0xff;
    uint8_t bit_length = (request >> 8) & 0x1f;
    uint8_t smcken = (request >> 13) & 0x1;
    uint8_t txfrmen = (request >> 14) & 0x1;

    SYNCIO_DEBUG("windermere_handle_syncio_request: request=0x%04x adc_config=0x%02x bit_length=%d smcken=%d txfrmen=%d\n",
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
            result = s->port_out[0];
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
            result = s->intmr;
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
            result = s->syncio_response;
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

static void windermere_init(Object *obj)
{
    WindermereState *s = WINDERMERE(obj);

    MemoryRegion *address_space_mem = get_system_memory();

    /* 0x0000_0000 */
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom, NULL, "psion.rom", 12 * MiB, &error_abort);
    memory_region_add_subregion(address_space_mem, 0x00000000, rom);

    /* 0x1000_0000 */
    MemoryRegion *rom2 = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom2, NULL, "psion.rom2", 12 * MiB, &error_abort);
    memory_region_add_subregion(address_space_mem, 0x10000000, rom2);

    /* 0x2000_0000 */
    memory_region_init_io(&s->etna.iomem, obj, &etna_ops, s,
                          "etna", 0x1000);
    memory_region_add_subregion_overlap(address_space_mem, 0x20000000, &s->etna.iomem, 0);

    /* 0x8000_0000 */
    memory_region_init_io(&s->iomem, obj, &windermere_ops, s,
                          TYPE_WINDERMERE, 0x1000);
    memory_region_add_subregion_overlap(address_space_mem, 0x80000000, &s->iomem, 0);

    /* 0xc000_0000 and 0xd000_0000 */
    MemoryRegion *sram_bank_c0 = g_new(MemoryRegion, 1);
    MemoryRegion *sram_bank_c1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(sram_bank_c0, NULL, "psion.sram_c0", 8 * MiB, &error_abort);
    memory_region_init_ram(sram_bank_c1, NULL, "psion.sram_c1", 8 * MiB, &error_abort);

    memory_region_add_subregion(address_space_mem, 0xc0000000, sram_bank_c0);
    memory_region_add_subregion(address_space_mem, 0xc1000000, sram_bank_c1);

    for (int alias_index = 1; alias_index <= 4; ++alias_index) {
        char name[32];
        MemoryRegion *sram_c0_alias = g_new(MemoryRegion, 1);
        snprintf(name, sizeof(name), "psion.sram_c0_alias_%d", alias_index);
        memory_region_init_alias(sram_c0_alias, NULL, name, sram_bank_c0, 0, 8 * MiB);
        memory_region_add_subregion(address_space_mem, 0xc0000000 + alias_index * 8 * MiB, sram_c0_alias);

        MemoryRegion *sram_c1_alias = g_new(MemoryRegion, 1);
        snprintf(name, sizeof(name), "psion.sram_c1_alias_%d", alias_index);
        memory_region_init_alias(sram_c1_alias, NULL, name, sram_bank_c1, 0, 8 * MiB);
        memory_region_add_subregion(address_space_mem, 0xc1000000 + alias_index * 8 * MiB, sram_c1_alias);
    }
    MemoryRegion *sram_bank_c4 = g_new(MemoryRegion, 1);
    memory_region_init_rom(sram_bank_c4, NULL, "psion.sram_c4", 0xdc000000 - 0xc4000000, &error_abort);
    memory_region_add_subregion(address_space_mem, 0xc4000000, sram_bank_c4);

    /* 0xe000_0000 */
    MemoryRegion *sram_bank_e0 = g_new(MemoryRegion, 1);
    memory_region_init_rom(sram_bank_e0, NULL, "psion.sram_e0", 4 * MiB, &error_abort);
    memory_region_add_subregion(address_space_mem, 0xe0000000, sram_bank_e0);



    /* 0xf000_0000 */
    MemoryRegion *sram_bank_f0 = g_new(MemoryRegion, 1);
    memory_region_init_rom(sram_bank_f0, NULL, "psion.sram_f0", 4 * MiB, &error_abort);
    memory_region_add_subregion(address_space_mem, 0xf0000000, sram_bank_f0);

    timer_init_ns(&s->rtc_timer, QEMU_CLOCK_VIRTUAL, windermere_rtc_cb, s);

    for (int i = 0; i < 2; ++i) {
        char timer_name[32];
        snprintf(timer_name, sizeof(timer_name), "timer%d", i);
        object_initialize_child(obj, timer_name, &s->timers[i], TYPE_PSION_TIMER);
    }
    qdev_init_gpio_in_named(DEVICE(s), windermere_timer_cb, "timer_irq", 2);
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

    WindermereState *windermere = WINDERMERE(qdev_new(TYPE_WINDERMERE));
    windermere->cpu = cpu;
    sysbus_realize(SYS_BUS_DEVICE(windermere), &error_fatal);


    static struct arm_boot_info psion_s5_boot_info = {
            .loader_start = 0x0,
    };
    arm_load_kernel(cpu, machine, &psion_s5_boot_info);

    char *rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, "sysrom_5mx.bin");
    if (rom_binary == NULL) {
        error_report("Error: ROM code binary not found");
        exit(1);
    }

    ssize_t size = load_image_targphys(rom_binary, 0, 12 * MiB);
    if (size < 0) {
        error_report("Error: could not load ROM binary '%s'", rom_binary);
        exit(1);
    }
    g_free(rom_binary);
}


static void psion_5mx_machine_init(MachineClass *mc)
{
    mc->desc = "Psion 5mx";
    mc->init = psion_s5_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm710a");
    mc->default_ram_size = 16 * MiB;

}

DEFINE_MACHINE("psion_5mx", psion_5mx_machine_init)


static void psion_register_types(void)
{
    type_register_static(&windermere_info);
}

type_init(psion_register_types)
