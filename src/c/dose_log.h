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
// True if this dose was marked taken or skipped.
bool         dose_log_is_marked(uint8_t med_index, uint32_t ts);
// True if the log still reaches back to this dose time.
bool         dose_log_covers(uint32_t ts);
