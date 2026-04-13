#pragma once
#include <pebble.h>

void dose_list_window_push(void);
void dose_list_window_refresh(void);
// After a Taken/Skip action on a specific dose, advance past that dose for the
// acted-upon med so the row immediately shows the next upcoming occurrence.
void dose_list_window_refresh_after(uint8_t med_index, time_t dose_time);
