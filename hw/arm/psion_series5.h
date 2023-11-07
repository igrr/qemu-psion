#pragma once

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/registerfields.h"

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
REG32(UNK_B00, 0xB00)
REG32(UNK_B04, 0xB04)
REG32(UNK_B08, 0xB08)

#define ETNA_INT_STATUS		6		/* Interrupt Status */
#define ETNA_INT_MASK		7		/* Interrupt Mask */
#define ETNA_INT_CLEAR		8		/* Interrupt Clear */
#define ETNA_CONTROL		11		/* CF control */
#define ETNA_ACTIVE		13		/* CF active */
#define ETNA_WAKE_1		12		/* CF wake up 1 */
#define ETNA_WAKE_2		15		/* CF wake up 2 */

#define HW_OPEN_BIT		0x40	/* Port B. Indicates that the case is open */

#define ADC_DIG_X	ADC1213X_CHAN0	/* Digitiser Y */
#define	ADC_DIG_Y	ADC1213X_CHAN1	/* Digitiser X */
#define ADC_VBATT	ADC1213X_CHAN2	/* Primary Battery Voltage */
#define ADC_VBACKUP	ADC1213X_CHAN3	/* Back-up Battery Voltage */
#define	ADC_VREF	ADC1213X_CHAN4	/* Reference Voltage */
#define	ADC_VPC		ADC1213X_CHAN5	/* Vpc/2 - CF power rail Voltage */
#define	ADC_VDC		ADC1213X_CHAN6	/* DC in (/2.5) */
#define	ADC_VPP		ADC1213X_CHAN7	/* VPP (/4) */v

