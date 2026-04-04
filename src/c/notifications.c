#include "notifications.h"
#include "dose_list_window.h"
#include <pebble.h>

// Stub — full grouping, snooze, and wakeup logic implemented in Phase 3.

void notifications_handle_wakeup(void) {
    // On wakeup launch, open the dose list so the user can act on due meds.
    dose_list_window_push();
}

void notifications_handle_timeline_action(uint32_t launch_code) {
    // launch_code 1 = Taken, 2 = Snooze (from Timeline pin actions).
    // Full handling in Phase 3.
    APP_LOG(APP_LOG_LEVEL_INFO, "Timeline action: %lu", (unsigned long)launch_code);
    dose_list_window_push();
}

void notifications_schedule_wakeups(void) {
    // Full scheduling logic (next 8 wakeup slots) in Phase 3.
}
