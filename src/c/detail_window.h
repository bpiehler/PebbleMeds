#pragma once
#include <pebble.h>

typedef enum {
    DETAIL_MODE_ALERT,   // wakeup context: Taken / Snooze / Skip + implicit snooze on dismiss
    DETAIL_MODE_BROWSE,  // list context:   Taken / Skip only; dismiss with no action is harmless
} DetailWindowMode;

void detail_window_push(uint8_t med_index, time_t dose_time, DetailWindowMode mode);
