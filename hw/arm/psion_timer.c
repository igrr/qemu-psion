
#include "psion_timer.h"
#include "hw/irq.h"
#include "hw/qdev-core.h"

// #define ENABLE_TIMER_DEBUG
#ifdef ENABLE_TIMER_DEBUG
#define TIMER_DEBUG(fmt, ...) qemu_log("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define TIMER_DEBUG(fmt, ...)
#endif

int16_t psion_timer_get_val(PsionTimerState* timer)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t delta_us = (now - timer->base_ns) / 1000;
    uint32_t freq_khz = (timer->clk == TIMER_CLK_512KHZ) ? 512 : 2;
    int64_t ticks = timer->base_value - (int64_t) muldiv64(delta_us, freq_khz, 1000);
    if (ticks < 0) {
        TIMER_DEBUG("timer %d underrun, value=%" PRId64 "\n now=%" PRId64 " base_ns=%" PRId64 " delta_us=%" PRId64 "\n",
                timer->index, ticks, now, timer->base_ns, delta_us);
        ticks = 0;
    } else if (ticks > 0xffff) {
        TIMER_DEBUG("timer %d overrun, value=%" PRId64 "\n", timer->index, ticks);
        ticks = 0xffff;
    }
    return (int16_t) ticks;
}

void psion_timer_update_alarm(PsionTimerState* timer)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int32_t ticks_to_zero = timer->base_value;
    int64_t delta_ns = muldiv64(ticks_to_zero, 1000 * 1000, (timer->clk == TIMER_CLK_512KHZ) ? 512 : 2);
    timer_mod_anticipate_ns(&timer->timer, now + delta_ns);
    TIMER_DEBUG("%s: timer %d set %" PRId64 " ms from now, base_value=%d, freq=%d\n", __func__, timer->index, delta_ns/1000000, timer->base_value, (timer->clk == TIMER_CLK_512KHZ) ? 512 : 2);
}


void psion_timer_update_settings(PsionTimerState *timer, timer_clk_t clk, timer_mode_t mode)
{
    if (timer->clk == clk && timer->mode == mode) {
        /* no change */
        return;
    }
    TIMER_DEBUG("%s: timer=%d clk=%s mode=%s\n", __func__,
            timer->index, clk == TIMER_CLK_512KHZ ? "512KHZ" : "2KHZ", mode == TIMER_MODE_PRESCALE ? "PRESCALE" : "FREE_RUNNING");

    timer->clk = clk;
    timer->mode = mode;
    psion_timer_update_alarm(timer);
}

void psion_timer_load(PsionTimerState *timer, uint32_t new_val)
{
    timer->interval = new_val;
    timer->base_value = new_val;
    timer->base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    psion_timer_update_alarm(timer);
}

static void psion_timer_cb(void* opaque)
{
    PsionTimerState *timer = (PsionTimerState*) opaque;

    if (timer->mode == TIMER_MODE_PRESCALE) {
        timer->base_value = timer->interval;
    } else {
        timer->base_value = 0xffff;
    }
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer->base_ns = now;
    TIMER_DEBUG("%s: timer %d fired, now=%" PRId64 " new base_ns=%" PRId64 " new base_value=%d\n",
            __func__, timer->index, now, timer->base_ns, timer->base_value);
    psion_timer_update_alarm(timer);

    qemu_irq_raise(timer->irq);
}

static void psion_timer_init(Object *obj)
{
    PsionTimerState *s = PSION_TIMER(obj);
    qdev_init_gpio_out(DEVICE(obj), &s->irq, 1);
}

static void psion_timer_realize(DeviceState *dev, Error **errp)
{
    PsionTimerState *s = PSION_TIMER(dev);
    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, psion_timer_cb, s);
}

static void psion_timer_reset(DeviceState *dev)
{
    PsionTimerState *s = PSION_TIMER(dev);
    timer_del(&s->timer);
    s->base_value = 0xffff;
    s->interval = 0xffff;
}

static Property psion_timer_properties[] = {
    DEFINE_PROP_INT32("index", struct PsionTimerState, index, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void psion_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = psion_timer_reset;
    dc->realize = psion_timer_realize;
    device_class_set_props(dc, psion_timer_properties);
}

static const TypeInfo psion_timer_info = {
    .name = TYPE_PSION_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = psion_timer_init,
    .instance_size = sizeof(PsionTimerState),
    .class_init = psion_timer_class_init
};

static void psion_timer_register_types(void)
{
    type_register_static(&psion_timer_info);
}

type_init(psion_timer_register_types)
