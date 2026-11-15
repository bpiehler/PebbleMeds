#pragma once
#include <pebble.h>

void notifications_handle_wakeup(WakeupId id, int32_t cookie);
void notifications_handle_timeline_action(uint32_t launch_code);
void notifications_schedule_wakeups(void);
// Buzz and light up for a dose, with the medication's vibe pattern.
void notifications_alert(uint8_t med_index);
// med_index and dose_time identify which dose was snoozed so the wakeup
// handler can re-show the correct reminder without relying on a timing window.
void notifications_schedule_snooze(uint8_t med_index, time_t dose_time);
