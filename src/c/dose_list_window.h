#pragma once
#include <pebble.h>

void dose_list_window_push(void);
// Shows the list with the action menu open for one dose, which may be a later
// dose than its row shows.
void dose_list_window_push_menu(uint8_t med_index, time_t dose_time);
void dose_list_window_refresh(void);
// After a Taken/Skip action on a specific dose, advance past that dose for the
// acted-upon med so the row immediately shows the next upcoming occurrence.
void dose_list_window_refresh_after(uint8_t med_index, time_t dose_time);
