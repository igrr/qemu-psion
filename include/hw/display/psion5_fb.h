#pragma once

#include "exec/hwaddr.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "qom/object.h"
#include "exec/memory.h"



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
};

#define TYPE_PSION5FB "psion5-fb"
OBJECT_DECLARE_SIMPLE_TYPE(Psion5FbState, PSION5FB)

void psion5fb_set_base_addr(Psion5FbState *s, hwaddr base_addr);

