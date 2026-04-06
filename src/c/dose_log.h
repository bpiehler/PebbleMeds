#pragma once
#include <pebble.h>

typedef enum {
    DOSE_TAKEN   = 0,
    DOSE_SKIPPED = 1,
    DOSE_SNOOZED = 2,
} DoseAction;

typedef struct {
    uint32_t   ts;
    uint8_t    med_index;
    DoseAction action;
    uint8_t    _pad;  // keep struct size even
} DoseLogEntry;

void         dose_log_init(void);
void         dose_log_deinit(void);
void         dose_log_record(uint8_t med_index, DoseAction action, uint32_t ts);
uint8_t      dose_log_count(void);
DoseLogEntry dose_log_get(uint8_t index);  // 0 = oldest, count-1 = newest
