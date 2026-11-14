#pragma once
#include <pebble.h>

void notifications_handle_wakeup(WakeupId id, int32_t cookie);
void notifications_handle_timeline_action(uint32_t launch_code);
void notifications_schedule_wakeups(void);
// med_index and dose_time identify which dose was snoozed so the wakeup
// handler can re-show the correct reminder without relying on a timing window.
void notifications_schedule_snooze(uint8_t med_index, time_t dose_time);
// Snooze for a specific number of minutes instead of the configured default.
void notifications_schedule_snooze_for(uint8_t med_index, time_t dose_time, uint16_t snooze_mins);
uint16_t notifications_default_snooze_mins(void);
