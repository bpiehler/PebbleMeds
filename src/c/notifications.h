#pragma once
#include <pebble.h>

void notifications_handle_wakeup(WakeupId id, int32_t cookie);
void notifications_handle_timeline_action(uint32_t launch_code);
void notifications_schedule_wakeups(void);
void notifications_schedule_snooze(void);
