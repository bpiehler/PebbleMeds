#include "notifications.h"
#include "med_list.h"
#include "detail_window.h"
#include "dose_list_window.h"
#include <pebble.h>

#define WAKEUP_COOKIE_DOSE    0
#define WAKEUP_COOKIE_SNOOZE  1
#define DUE_WINDOW_SECS       300   // 5-minute grouping window for regular dose wakeups
#define PERSIST_KEY_SNOOZE    18    // int32 Unix timestamp; 0 = no pending snooze
#define PERSIST_KEY_SNOOZE_MED  20  // uint8 med index of the snoozed dose
#define PERSIST_KEY_SNOOZE_DOSE 21  // int32 Unix timestamp of the snoozed dose

// ---------------------------------------------------------------------------
// Dose event collection for wakeup scheduling
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t med_index;
    time_t  dose_ts;
} DoseEvent;

// NOTE: collect_dose_events() and notifications_schedule_wakeups() have a
// pure-JS counterpart in src/pkjs/wakeups.js (collectDoseEvents / planWakeups).
// Keep the two in sync — any change to slot-filling or snooze-reservation
// logic here must be reflected there, and tests/wakeups.test.js updated.
static uint8_t collect_dose_events(time_t now, DoseEvent *out, uint8_t max_out) {
    uint8_t count     = 0;
    uint8_t med_count = med_list_count();
    time_t  horizon   = now + 48 * 3600;

    for (uint8_t i = 0; i < med_count && count < max_out; i++) {
        MedEntry *med = med_list_get(i);
        if (!med) continue;

        time_t t = med_list_next_dose_time(med, now);
        for (int occ = 0; occ < 4 && t > 0 && t <= horizon && count < max_out; occ++) {
            out[count].med_index = i;
            out[count].dose_ts   = t;
            count++;
            // Advance to next occurrence of this med
            if (med->scheduleType == SCHEDULE_INTERVAL) {
                t += (time_t)med->intervalHours * 3600;
            } else {
                t = med_list_next_dose_time(med, t);
            }
        }
    }

    // Insertion sort by dose_ts
    for (uint8_t i = 1; i < count; i++) {
        DoseEvent key = out[i];
        int j = (int)i - 1;
        while (j >= 0 && out[j].dose_ts > key.dose_ts) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }

    return count;
}

// ---------------------------------------------------------------------------
// Public: schedule next <=8 wakeups
// ---------------------------------------------------------------------------

void notifications_schedule_wakeups(void) {
    wakeup_cancel_all();

    if (med_list_count() == 0) return;

    time_t now = time(NULL);

    // Check for a pending snooze before filling dose slots so we can reserve
    // a wakeup slot for it.  Without this, a full 8-slot dose schedule would
    // silently drop the snooze when another med fires mid-snooze.
    time_t snooze_t  = (time_t)persist_read_int(PERSIST_KEY_SNOOZE);
    bool   has_snooze = (snooze_t > now);
    uint8_t max_dose  = has_snooze ? 7 : 8;

    DoseEvent events[32];
    uint8_t   count = collect_dose_events(now, events, 32);

    uint8_t scheduled = 0;
    time_t  last_t    = 0;

    for (uint8_t i = 0; i < count && scheduled < max_dose; i++) {
        if (events[i].dose_ts == last_t) continue;  // deduplicate same-second slots
        WakeupId id = wakeup_schedule(events[i].dose_ts, WAKEUP_COOKIE_DOSE, false);
        if (id >= 0) {
            last_t = events[i].dose_ts;
            scheduled++;
        }
        // id < 0: E_RANGE (< 60s away) or E_OUT_OF_RESOURCES — skip this slot
    }

    if (has_snooze) {
        wakeup_schedule(snooze_t, WAKEUP_COOKIE_SNOOZE, false);
    }
}

// ---------------------------------------------------------------------------
// Public: snooze — persist the desired wakeup time then reschedule
// ---------------------------------------------------------------------------

void notifications_schedule_snooze(uint8_t med_index, time_t dose_time) {
    AppSettings *settings  = med_list_get_settings();
    uint16_t snooze_mins   = (settings->snoozeMins > 0) ? settings->snoozeMins : 15;
    time_t   snooze_t      = time(NULL) + (time_t)snooze_mins * 60;
    // Persist the wakeup time AND which dose was snoozed.  The wakeup handler
    // uses med_index + dose_time directly rather than re-searching by window,
    // so the reminder fires correctly regardless of how far past the dose time
    // the snooze wakeup lands.
    persist_write_int(PERSIST_KEY_SNOOZE,      (int32_t)snooze_t);
    persist_write_int(PERSIST_KEY_SNOOZE_MED,  (int32_t)med_index);
    persist_write_int(PERSIST_KEY_SNOOZE_DOSE, (int32_t)dose_time);
    notifications_schedule_wakeups();  // includes the snooze slot
}

// ---------------------------------------------------------------------------
// Public: wakeup launch handler
// ---------------------------------------------------------------------------

void notifications_handle_wakeup(WakeupId id, int32_t cookie) {
    time_t now = time(NULL);

    if (cookie == WAKEUP_COOKIE_SNOOZE) {
        // Read which dose was snoozed, clear all snooze state, then re-show
        // the reminder directly.  We do NOT re-search by time window here —
        // the snooze fires snoozeMins after the original dose, which is outside
        // DUE_WINDOW_SECS, so a window search would always return due_count==0
        // and fall through to dose_list_window_push() instead of the alert.
        uint8_t med_index = (uint8_t)persist_read_int(PERSIST_KEY_SNOOZE_MED);
        time_t  dose_ts   = (time_t)persist_read_int(PERSIST_KEY_SNOOZE_DOSE);
        persist_write_int(PERSIST_KEY_SNOOZE,      0);
        persist_write_int(PERSIST_KEY_SNOOZE_MED,  0xFF);
        persist_write_int(PERSIST_KEY_SNOOZE_DOSE, 0);
        notifications_schedule_wakeups();
        vibes_short_pulse();
        if (med_list_get(med_index)) {
            detail_window_push(med_index, dose_ts, DETAIL_MODE_ALERT);
        } else {
            dose_list_window_push();  // fallback: med was removed since snooze set
        }
        return;
    }

    // Regular dose wakeup: find all doses due within DUE_WINDOW_SECS of now
    uint8_t due_count = 0;
    uint8_t due_indices[MED_MAX];
    time_t  due_times[MED_MAX];

    uint8_t med_count = med_list_count();
    for (uint8_t i = 0; i < med_count; i++) {
        MedEntry *med = med_list_get(i);
        if (!med) continue;
        // next_dose_time after (now - DUE_WINDOW - 1) gives the dose in the window
        time_t next = med_list_next_dose_time(med, now - DUE_WINDOW_SECS - 1);
        if (next >= now - DUE_WINDOW_SECS && next <= now + 60) {
            due_indices[due_count] = i;
            due_times[due_count]   = next;
            due_count++;
        }
    }

    if (due_count == 0) {
        APP_LOG(APP_LOG_LEVEL_WARNING, "Wakeup fired but no doses found in window");
        notifications_schedule_wakeups();
        dose_list_window_push();
        return;
    }

    vibes_short_pulse();
    detail_window_push(due_indices[0], due_times[0], DETAIL_MODE_ALERT);
    notifications_schedule_wakeups();
}

// ---------------------------------------------------------------------------
// Public: Timeline pin action launch handler
// ---------------------------------------------------------------------------

void notifications_handle_timeline_action(uint32_t launch_code) {
    // launch_code 1 = Taken, 2 = Snooze (from Timeline pin actions, Phase 4)
    APP_LOG(APP_LOG_LEVEL_INFO, "Timeline action: %lu", (unsigned long)launch_code);
    dose_list_window_push();
}
