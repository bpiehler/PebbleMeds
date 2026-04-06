#include <pebble.h>
#include "med_list.h"
#include "dose_list_window.h"
#include "appmessage.h"
#include "notifications.h"
#include "dose_log.h"

// ---------------------------------------------------------------------------
// Launch reason handling
// ---------------------------------------------------------------------------

static void handle_wakeup_launch(WakeupId id, int32_t cookie) {
  // Called when app is launched by a wakeup event.
  // notifications.c will check which doses are due and fire alerts.
  notifications_handle_wakeup(id, cookie);
}

static void handle_timeline_launch(uint32_t launch_code) {
  // Called when app is launched by a Timeline pin action.
  // launch_code 1 = Taken, 2 = Snooze
  notifications_handle_timeline_action(launch_code);
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

static void init(void) {
  // Load cached medication list from persistent storage.
  med_list_init();
  dose_log_init();

  // Open AppMessage channel so we can receive config updates from the phone.
  appmessage_init();

  // Determine why the app launched and respond appropriately.
  AppLaunchReason reason = launch_reason();
  if (reason == APP_LAUNCH_WAKEUP) {
    WakeupId wakeup_id;
    int32_t wakeup_cookie;
    wakeup_get_launch_event(&wakeup_id, &wakeup_cookie);
    handle_wakeup_launch(wakeup_id, wakeup_cookie);
  } else if (reason == APP_LAUNCH_TIMELINE_ACTION) {
    uint32_t launch_code = launch_get_args();
    handle_timeline_launch(launch_code);
  } else {
    // Normal launch: show the dose list.
    dose_list_window_push();
  }

  // Schedule the next batch of wakeups (up to 8).
  notifications_schedule_wakeups();
}

static void deinit(void) {
  // Persist current medication list and state before exit.
  dose_log_deinit();
  med_list_deinit();
  appmessage_deinit();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
