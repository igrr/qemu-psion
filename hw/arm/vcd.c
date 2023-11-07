#include <stdio.h>
#include <stdlib.h>
#include "vcd.h"

typedef struct vcd_signal_t {
    vcd_signal_info_t info;
    char id[3]; // 2 chars + null
    uint64_t last_value;
} vcd_signal_t;

typedef struct vcd_state_t {
    vcd_signal_t *signals;
    int num_signals;
    FILE* fp;
    uint64_t last_timestamp;
    int oneshot_signals_to_clear;
} vcd_state_t;

vcd_state_t s_state;

void vcd_open(const vcd_file_info_t* info)
{
    
    const char* filename = getenv("VCD_FILENAME");
    if (filename == NULL) {
        filename = "/tmp/dump.vcd";
    }
    s_state.fp = fopen(filename, "wb");
    if (s_state.fp == NULL) {
        fprintf(stderr, "Failed to open %s\n", filename);
        exit(1);
    }

    s_state.signals = calloc(info->num_signals, sizeof(vcd_signal_t));
    s_state.num_signals = info->num_signals;

    // Assign signal IDs
    //  ! to ~ (decimal 33 to 126)
    char vcd_signal_id_alphabet[] = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    for (int i = 0; i < info->num_signals; i++) {
        const vcd_signal_info_t *signal_info = &info->signals[i];
        s_state.signals[i].info = *signal_info;
        int letter_1 = i / 94;
        int letter_2 = i % 94;
        s_state.signals[i].id[0] = vcd_signal_id_alphabet[letter_1];
        s_state.signals[i].id[1] = vcd_signal_id_alphabet[letter_2];
        s_state.signals[i].id[2] = 0;
    }

    // Write header with some dummy metadata
    fprintf(s_state.fp, "$date\n");
    fprintf(s_state.fp, "    2020-01-01 00:00:00\n");
    fprintf(s_state.fp, "$end\n");
    fprintf(s_state.fp, "$version\n");
    fprintf(s_state.fp, "    0.1\n");
    fprintf(s_state.fp, "$end\n");
    fprintf(s_state.fp, "$timescale\n");
    fprintf(s_state.fp, "    1%s\n", info->timescale);
    fprintf(s_state.fp, "$end\n");
    fprintf(s_state.fp, "$scope module top $end\n");
    for (int i = 0; i < info->num_signals; i++) {
        const vcd_signal_info_t *signal_info = &info->signals[i];
        fprintf(s_state.fp, "$var wire %d %s %s $end\n", signal_info->bits, s_state.signals[i].id, signal_info->name);
    }
    fprintf(s_state.fp, "$upscope $end\n");
    fprintf(s_state.fp, "$enddefinitions $end\n");
    fprintf(s_state.fp, "$dumpvars\n");

    // Write initial values
    for (int i = 0; i < info->num_signals; i++) {
        fprintf(s_state.fp, "#0\n");
        const vcd_signal_info_t *signal_info = &info->signals[i];
        if (signal_info->bits == 1) {
            fputs("0", s_state.fp);
            fputs(s_state.signals[i].id, s_state.fp);
            fputc('\n', s_state.fp);
        } else {
            fprintf(s_state.fp, "b");
            for (int j = 0; j < signal_info->bits; j++) {
                fprintf(s_state.fp, "0");
            }
            fprintf(s_state.fp, " %s\n", s_state.signals[i].id);
        }
    }
}

void vcd_flush(void)
{
    fflush(s_state.fp);
}

void vcd_write(uint64_t timestamp, int signal_index, uint64_t value)
{
    // find the signal structure with the matching index
    vcd_signal_t *signal = NULL;
    for (int i = 0; i < s_state.num_signals; i++) {
        if (s_state.signals[i].info.index == signal_index) {
            signal = &s_state.signals[i];
            break;
        }
    }
    if (signal == NULL) {
        return;
    }
    // Check if the value has changed since the last write
    if (signal->last_value == value) {
        return;
    }
    // Write the timestamp if it has changed
    if (timestamp != s_state.last_timestamp) {

        if (s_state.oneshot_signals_to_clear > 0) {
            fprintf(s_state.fp, "#%llu\n", s_state.last_timestamp + 1);
            
            for (int i = 0; s_state.oneshot_signals_to_clear > 0 && i < s_state.num_signals; i++) {
                if (s_state.signals[i].info.is_oneshot && s_state.signals[i].last_value == 1) {
                    fputc('0', s_state.fp);
                    fputs(s_state.signals[i].id, s_state.fp);
                    fputc('\n', s_state.fp);
                    s_state.signals[i].last_value = 0;
                    s_state.oneshot_signals_to_clear -= 1;
                }
            }
            s_state.oneshot_signals_to_clear = 0;
        }
        if (timestamp > s_state.last_timestamp + 1) {
            fprintf(s_state.fp, "#%llu\n", timestamp);
        }
        s_state.last_timestamp = timestamp;
    }
    if (signal->info.bits == 1) {
        // single bit signals are written as 1 or 0 followed by the signal ID
        fputc(value ? '1' : '0', s_state.fp);
        fputs(signal->id, s_state.fp);
    } else {
        // multi-bit signals are written as b followed by the binary value, then space and ID
        fputc('b', s_state.fp);
        for (int i = 0; i < signal->info.bits; i++) {
            fputc((value >> (signal->info.bits - i - 1)) & 1 ? '1' : '0', s_state.fp);
        }
        fputc(' ', s_state.fp);
        fputs(signal->id, s_state.fp);
    }
    // Newline
    fputc('\n', s_state.fp);
    // Update the last  value
    signal->last_value = value;
    if (signal->info.is_oneshot) {
        s_state.oneshot_signals_to_clear += 1;
    }
}

