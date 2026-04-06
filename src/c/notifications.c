#include "notifications.h"
#include "med_list.h"
#include "detail_window.h"
#include "dose_list_window.h"
#include <pebble.h>

#define WAKEUP_COOKIE_DOSE    0
#define WAKEUP_COOKIE_SNOOZE  1
#define DUE_WINDOW_SECS       300   // 5-minute grouping window
#define PERSIST_KEY_SNOOZE    18    // int32 Unix timestamp; 0 = no pending snooze

// ---------------------------------------------------------------------------
// Dose event collection for wakeup scheduling
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t med_index;
    time_t  dose_ts;
} DoseEvent;

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

    DoseEvent events[32];
    uint8_t   count = collect_dose_events(now, events, 32);

    uint8_t scheduled = 0;
    time_t  last_t    = 0;

    for (uint8_t i = 0; i < count && scheduled < 8; i++) {
        if (events[i].dose_ts == last_t) continue;  // deduplicate same-second slots
        WakeupId id = wakeup_schedule(events[i].dose_ts, WAKEUP_COOKIE_DOSE, false);
        if (id >= 0) {
            last_t = events[i].dose_ts;
            scheduled++;
        }
        // id < 0: E_RANGE (< 60s away) or E_OUT_OF_RESOURCES — skip this slot
    }

    // Re-add any pending snooze that's still in the future
    time_t snooze_t = (time_t)persist_read_int(PERSIST_KEY_SNOOZE);
    if (snooze_t > now && scheduled < 8) {
        wakeup_schedule(snooze_t, WAKEUP_COOKIE_SNOOZE, false);
    }
}

// ---------------------------------------------------------------------------
// Public: snooze — persist the desired wakeup time then reschedule
// ---------------------------------------------------------------------------

void notifications_schedule_snooze(void) {
    AppSettings *settings  = med_list_get_settings();
    uint16_t snooze_mins   = (settings->snoozeMins > 0) ? settings->snoozeMins : 15;
    time_t   snooze_t      = time(NULL) + (time_t)snooze_mins * 60;
    persist_write_int(PERSIST_KEY_SNOOZE, (int32_t)snooze_t);
    notifications_schedule_wakeups();  // includes the snooze slot
}

// ---------------------------------------------------------------------------
// Public: wakeup launch handler
// ---------------------------------------------------------------------------

void notifications_handle_wakeup(WakeupId id, int32_t cookie) {
    time_t now = time(NULL);

    // Clear persisted snooze if it was a snooze wakeup that fired
    if (cookie == WAKEUP_COOKIE_SNOOZE) {
        persist_write_int(PERSIST_KEY_SNOOZE, 0);
    }

    // Find all doses due within DUE_WINDOW_SECS of now
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

    // Push detail window for first due dose.
    // Phase 4: implement per-taker group checklist for multiple simultaneous doses.
    detail_window_push(due_indices[0], due_times[0]);

    // Reschedule remaining wakeups now that one has fired
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
