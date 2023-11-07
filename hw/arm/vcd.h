#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct vcd_signal_info_t
{
    const char* name;
    int bits;
    int index;
    int is_oneshot;
} vcd_signal_info_t;


typedef struct vcd_file_info_t
{
    const vcd_signal_info_t* signals;
    int num_signals;
    const char* timescale;
} vcd_file_info_t;

void vcd_open(const vcd_file_info_t* info);
void vcd_flush(void);
void vcd_write(uint64_t timestamp, int signal, uint64_t value);


#ifdef __cplusplus
}
#endif
