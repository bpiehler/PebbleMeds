#include "notifications.h"
#include "med_list.h"
#include "detail_window.h"
#include "dose_list_window.h"
#include <pebble.h>

#define WAKEUP_COOKIE_DOSE      0
#define WAKEUP_COOKIE_SNOOZE    1
#define WAKEUP_COOKIE_HEARTBEAT 2
#define DUE_WINDOW_SECS         300   // 5-minute grouping window for regular dose wakeups
#define HEARTBEAT_INTERVAL_SECS (6 * 3600)  // safety-net recovery window
// Keys 18, 20 and 21 held the single snooze of older versions.
#define PERSIST_KEY_OLD_SNOOZE      18
#define PERSIST_KEY_OLD_SNOOZE_MED  20
#define PERSIST_KEY_OLD_SNOOZE_DOSE 21
#define PERSIST_KEY_SNOOZES         22

// ---------------------------------------------------------------------------
// Vibration helper
// ---------------------------------------------------------------------------

static void fire_vibe(uint8_t vibe_pattern) {
    switch (vibe_pattern) {
        case MED_VIBE_LONG:   vibes_long_pulse();   break;
        case MED_VIBE_DOUBLE: vibes_double_pulse(); break;
        default:          vibes_short_pulse();  break;
    }
}

// ---------------------------------------------------------------------------
// Snoozed doses: one entry per dose, each with the time it comes back
// ---------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint32_t until;
    uint32_t dose_ts;
    uint8_t  med_index;
} Snooze;

static uint8_t snoozes_read(Snooze *out) {
    int size = persist_get_size(PERSIST_KEY_SNOOZES);
    if (size > 0) {
        persist_read_data(PERSIST_KEY_SNOOZES, out, MED_MAX * sizeof(Snooze));
        return (uint8_t)(size / (int)sizeof(Snooze));
    }
    // Snooze saved by an older version
    int32_t until = persist_read_int(PERSIST_KEY_OLD_SNOOZE);
    if (until <= 0) return 0;
    out[0] = (Snooze){
        .until     = (uint32_t)until,
        .dose_ts   = (uint32_t)persist_read_int(PERSIST_KEY_OLD_SNOOZE_DOSE),
        .med_index = (uint8_t)persist_read_int(PERSIST_KEY_OLD_SNOOZE_MED),
    };
    return 1;
}

static void snoozes_write(const Snooze *list, uint8_t count) {
    if (count > 0) {
        persist_write_data(PERSIST_KEY_SNOOZES, list, count * sizeof(Snooze));
    } else {
        persist_delete(PERSIST_KEY_SNOOZES);
    }
    if (persist_exists(PERSIST_KEY_OLD_SNOOZE)) {
        persist_delete(PERSIST_KEY_OLD_SNOOZE);
        persist_delete(PERSIST_KEY_OLD_SNOOZE_MED);
        persist_delete(PERSIST_KEY_OLD_SNOOZE_DOSE);
    }
}

// Earliest snooze time, or 0 if no dose is snoozed.
static time_t snoozes_earliest(void) {
    Snooze list[MED_MAX];
    uint8_t count = snoozes_read(list);
    time_t earliest = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (earliest == 0 || (time_t)list[i].until < earliest) earliest = (time_t)list[i].until;
    }
    return earliest;
}

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
    time_t  horizon   = now + 7 * 24 * 3600;

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
                // FIXED and WEEKLY both advance by re-calling next_dose_time
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
    // a wakeup slot for it.  Without this, a full dose schedule would silently
    // drop the snooze when another med fires mid-snooze.
    // One additional slot is always reserved for the heartbeat safety net.
    // The snooze slot wakes for the earliest snoozed dose. A snooze time that
    // passed while the watch was off comes back in a minute.
    time_t snooze_t   = snoozes_earliest();
    bool   has_snooze = (snooze_t > 0);
    if (has_snooze && snooze_t < now + 60) snooze_t = now + 60;
    uint8_t max_dose  = has_snooze ? 6 : 7;  // 8 total - 1 heartbeat - (1 snooze?)

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

    wakeup_schedule(now + HEARTBEAT_INTERVAL_SECS, WAKEUP_COOKIE_HEARTBEAT, true);
}

// ---------------------------------------------------------------------------
// Public: snooze. Save the dose and when it comes back, then reschedule.
// ---------------------------------------------------------------------------

void notifications_schedule_snooze(uint8_t med_index, time_t dose_time) {
    AppSettings *settings  = med_list_get_settings();
    uint16_t snooze_mins   = (settings->snoozeMins > 0) ? settings->snoozeMins : 15;
    Snooze list[MED_MAX];
    uint8_t count = snoozes_read(list);
    // A dose snoozed again, or a later dose of the same med, takes the med's entry.
    uint8_t slot = count;
    for (uint8_t i = 0; i < count; i++) {
        if (list[i].med_index == med_index) { slot = i; break; }
    }
    if (slot == MED_MAX) slot = 0;
    list[slot] = (Snooze){
        .until     = (uint32_t)(time(NULL) + (time_t)snooze_mins * 60),
        .dose_ts   = (uint32_t)dose_time,
        .med_index = med_index,
    };
    if (slot == count) count++;
    snoozes_write(list, count);
    notifications_schedule_wakeups();  // includes the snooze slot
}

// ---------------------------------------------------------------------------
// Public: wakeup launch handler
// ---------------------------------------------------------------------------

void notifications_handle_wakeup(WakeupId id, int32_t cookie) {
    time_t now = time(NULL);

    if (cookie == WAKEUP_COOKIE_HEARTBEAT) {
        notifications_schedule_wakeups();
        return;
    }

    if (cookie == WAKEUP_COOKIE_SNOOZE) {
        // Show the earliest snoozed dose. Others due now get the next wakeup.
        Snooze list[MED_MAX];
        uint8_t count = snoozes_read(list);
        uint8_t first = count;
        for (uint8_t i = 0; i < count; i++) {
            if (list[i].until <= (uint32_t)(now + 60)
                    && (first == count || list[i].until < list[first].until)) {
                first = i;
            }
        }
        if (first == count) {
            notifications_schedule_wakeups();
            return;
        }
        uint8_t med_index = list[first].med_index;
        time_t  dose_ts   = (time_t)list[first].dose_ts;
        list[first] = list[--count];
        snoozes_write(list, count);
        notifications_schedule_wakeups();
        light_enable_interaction();
        MedEntry *snooze_med = med_list_get(med_index);
        fire_vibe(snooze_med ? snooze_med->vibePattern : MED_VIBE_SHORT);
        if (snooze_med) {
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

    light_enable_interaction();
    fire_vibe(med_list_get(due_indices[0]) ? med_list_get(due_indices[0])->vibePattern : MED_VIBE_SHORT);
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
