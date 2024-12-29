#pragma once

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/registerfields.h"


REG32(MEMCFG1, 0x0000)
REG32(MEMCFG2, 0x0004)
REG32(DRAM_CFG, 0x0100)

REG32(PWRSR, 0x0400)
    FIELD(PWRSR, RTCDIV, 0, 6)
    FIELD(PWRSR, MCDR, 6, 1)
    FIELD(PWRSR, DCDET, 7, 1)
    FIELD(PWRSR, WUDR, 8, 1)
    FIELD(PWRSR, WUON, 9, 1)
    FIELD(PWRSR, NBFLG, 10, 1)
    FIELD(PWRSR, RSTFLG, 11, 1)
    FIELD(PWRSR, PFFLG, 12, 1)
    FIELD(PWRSR, CLDFLG, 13, 1)
    FIELD(PWRSR, VERID, 14, 2)

REG32(PWRCNT, 0x0404)
    FIELD(PWRCNT, EXCKEN, 0, 1)
    FIELD(PWRCNT, WAKEDIR, 1, 1)
    FIELD(PWRCNT, CLKFLG, 2, 1)
    FIELD(PWRCNT, ADCCLK, 3, 1)

REG32(HALT, 0x0408)
REG32(STBY, 0x040c)

REG32(BLEOI, 0x0410)
REG32(MCEOI, 0x0414)
REG32(TEOI, 0x0418)
REG32(STFCLR, 0x041c)
REG32(E2EOI, 0x0420)

REG32(TC1EOI, 0x0c0c)
REG32(TC2EOI, 0x0c2c)
REG32(RTCEOI, 0x0d10)
REG32(UMSEOI, 0x0714)

REG32(INTSR, 0x0500)
    FIELD(INTSR, EXTFIQ, 0, 1)
    FIELD(INTSR, BLINT, 1, 1)
    FIELD(INTSR, WEINT, 2, 1)
    FIELD(INTSR, MCINT, 3, 1)
    FIELD(INTSR, CSINT, 4, 1)
    FIELD(INTSR, EINT1, 5, 1)
    FIELD(INTSR, EINT2, 6, 1)
    FIELD(INTSR, EINT3, 7, 1)
    FIELD(INTSR, TC1OI, 8, 1)
    FIELD(INTSR, TC2OI, 9, 1)
    FIELD(INTSR, RTCMI, 10, 1)
    FIELD(INTSR, TINT, 11, 1)
    FIELD(INTSR, UTXINT, 12, 1)
    FIELD(INTSR, URXINT1, 13, 1)
    FIELD(INTSR, UMSINT, 14, 1)
    FIELD(INTSR, SSEOTI, 15, 1)

REG32(INTRSR, 0x0504)
REG32(INTENS, 0x0508)
REG32(INTENC, 0x050c)
REG32(INTTEST1, 0x0514)
REG32(INTTEST2, 0x0518)

REG32(TC1LOAD, 0x0c00)
REG32(TC1VAL, 0x0c04)
REG32(TC1CTRL, 0x0c08)
    FIELD(TC1CTRL, TC_CLKSEL, 3, 1)     /* 0: 2kHz, 1: 512kHz */
    FIELD(TC1CTRL, TC_MODE, 6, 1)       /* 0: free running, 1: prescale */
    FIELD(TC1CTRL, TC_ENABLE, 7, 1)     /* 0: disabled, 1: enabled */

REG32(TC2LOAD, 0x0c20)
REG32(TC2VAL, 0x0c24)
REG32(TC2CTRL, 0x0c28)
    FIELD(TC2CTRL, TC_CLKSEL, 3, 1)     /* 0: 2kHz, 1: 512kHz */
    FIELD(TC2CTRL, TC_MODE, 6, 1)       /* 0: free running, 1: prescale */
    FIELD(TC2CTRL, TC_ENABLE, 7, 1)     /* 0: disabled, 1: enabled */

REG32(BZCONT, 0x0c40)
    FIELD(BZCONT, BZ_BZTOG, 0, 1)
    FIELD(BZCONT, BZ_BZMOD, 1, 1)

REG32(RTCDRL, 0x0d00)
REG32(RTCDRU, 0x0d04)
REG32(RTCMRL, 0x0d08)
REG32(RTCMRU, 0x0d0c)

REG32(SSCR0, 0x0b00)
REG32(SSCR1, 0x0b04)
REG32(SSDR, 0x0b0c)
REG32(SSSR, 0x0b14)

REG32(PUMPCON, 0x0900)
#define PUMP_RUN_VAL	0xbbb
#define PUMP_STOP_VAL	0x0

REG32(CODR, 0x0a00)
REG32(CONFG, 0x0a04)
REG32(COLFG, 0x0a08)
REG32(COEOI, 0x0a0c)
REG32(COTEST, 0x0a10)

REG32(PADR, 0x0e00)
REG32(PBDR, 0x0e04)
REG32(PCDR, 0x0e08)
REG32(PDDR, 0x0e0c)
REG32(PADDR, 0x0e10)
REG32(PBDDR, 0x0e14)
REG32(PCDDR, 0x0e18)
REG32(PDDDR, 0x0e1c)
REG32(PEDR, 0x0e20)
REG32(PEDDR, 0x0e24)
REG32(KSCAN, 0x0e28)
REG32(LCDMUX, 0x0e2c)

REG32(LCDCTL, 0x200)
    FIELD(LCDCTL, LCDCTL_EN, 0, 1)
    FIELD(LCDCTL, LCDCTL_BW, 1, 1)
    FIELD(LCDCTL, LCDCTL_DP, 2, 1)
    FIELD(LCDCTL, LCDCTL_DONE, 3, 1)
    FIELD(LCDCTL, LCDCTL_NEXT, 4, 1)
    FIELD(LCDCTL, LCDCTL_ERR, 5, 1)
    FIELD(LCDCTL, LCDCTL_TFT, 7, 1)
    FIELD(LCDCTL, LCDCTL_M8B, 8, 1)

REG32(LCDST, 0x204)
    FIELD(LCDST, LCDST_NEXT, 1, 1)
    FIELD(LCDST, LCDST_BER, 2, 1)
    FIELD(LCDST, LCDST_ABC, 3, 1)
    FIELD(LCDST, LCDST_FUF, 5, 1)

REG32(LCD_DBAR1, 0x210)
REG32(LCDT0, 0x220)
REG32(LCDT1, 0x224)
REG32(LCDT2, 0x228)
    FIELD(LCDT2, LCDT2_IVS, 20, 1)
    FIELD(LCDT2, LCDT2_IHS, 21, 1)
    FIELD(LCDT2, LCDT2_IPC, 22, 1)
    FIELD(LCDT2, LCDT2_IEO, 23, 1)

REG8(UART0_DATA, 0x600)
REG32(UART0_FCR, 0x604)
REG32(UART0_LCR, 0x608)
REG8(UART0_CON, 0x60c)
REG8(UART0_FLG, 0x610)
REG8(UART0_INT, 0x614)
REG8(UART0_INTM, 0x618)
REG8(UART0_INTR, 0x61c)
REG32(UART0_TEST1, 0x620)
REG32(UART0_TEST2, 0x624)
REG32(UART0_TEST3, 0x628)

REG8(UART1_DATA, 0x700)
REG32(UART1_FCR, 0x704)
REG32(UART1_LCR, 0x708)
REG8(UART1_CON, 0x70c)
REG8(UART1_FLG, 0x710)
REG8(UART1_INT, 0x714)
REG8(UART1_INTM, 0x718)
REG8(UART1_INTR, 0x71c)
REG32(UART1_TEST1, 0x720)
REG32(UART1_TEST2, 0x724)
REG32(UART1_TEST3, 0x728)
