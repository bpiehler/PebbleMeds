#pragma once
#include <pebble.h>

void notifications_handle_wakeup(WakeupId id, int32_t cookie);
void notifications_handle_timeline_action(uint32_t launch_code);
void notifications_schedule_wakeups(void);
// med_index and dose_time identify which dose was snoozed so the wakeup
// handler can re-show the correct reminder without relying on a timing window.
void notifications_schedule_snooze(uint8_t med_index, time_t dose_time);
// Drop a pending snooze, but only if it belongs to this dose.  Called when the
// dose is confirmed (Taken/Skip) so keep-nagging mode stops.
void notifications_cancel_snooze(uint8_t med_index, time_t dose_time);
