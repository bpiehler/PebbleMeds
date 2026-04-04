#pragma once
#include <stdint.h>

void notifications_handle_wakeup(void);
void notifications_handle_timeline_action(uint32_t launch_code);
void notifications_schedule_wakeups(void);
