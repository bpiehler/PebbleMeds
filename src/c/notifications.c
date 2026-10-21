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
#define PERSIST_KEY_SNOOZE      18
#define PERSIST_KEY_SNOOZE_MED  20
#define PERSIST_KEY_SNOOZE_DOSE 21

// ---------------------------------------------------------------------------
// Vibration helper
// ---------------------------------------------------------------------------

#define VIBE_MAX_SEGMENTS  12
#define VIBE_REPEAT_GAP_MS 250

static const uint32_t VIBE_BASE_SHORT[]  = { 200 };
static const uint32_t VIBE_BASE_LONG[]   = { 700 };
static const uint32_t VIBE_BASE_DOUBLE[] = { 200, 150, 200 };

static void fire_vibe(uint8_t vibe_pattern, uint8_t vibe_strength) {
    const uint32_t *base;
    uint8_t         base_len;

    switch (vibe_pattern) {
        case MED_VIBE_LONG:   base = VIBE_BASE_LONG;   base_len = 1; break;
        case MED_VIBE_DOUBLE: base = VIBE_BASE_DOUBLE; base_len = 3; break;
        default:              base = VIBE_BASE_SHORT;  base_len = 1; break;
    }

    uint8_t passes = (vibe_strength == MED_VIBE_STRONG)   ? 3
                   : (vibe_strength == MED_VIBE_MEDIUM) ? 2
                                                        : 1;

    // Static buffer: the firmware reads the durations while the vibe plays.
    static uint32_t   segments[VIBE_MAX_SEGMENTS];
    static VibePattern pattern;
    uint8_t           n = 0;

    for (uint8_t pass = 0; pass < passes; pass++) {
        if (pass > 0) {
            if (n + 1 + base_len > VIBE_MAX_SEGMENTS) break;
            segments[n++] = VIBE_REPEAT_GAP_MS;
        }
        for (uint8_t s = 0; s < base_len && n < VIBE_MAX_SEGMENTS; s++) {
            segments[n++] = base[s];
        }
    }

    pattern.durations    = segments;
    pattern.num_segments = n;
    vibes_enqueue_custom_pattern(pattern);
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
    time_t snooze_t  = (time_t)persist_read_int(PERSIST_KEY_SNOOZE);
    bool   has_snooze = (snooze_t > now);
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
// Public: drop a pending snooze for a confirmed dose
// ---------------------------------------------------------------------------

void notifications_cancel_snooze(uint8_t med_index, time_t dose_time) {
    // Identity-checked: a pending snooze belonging to another medication (or
    // to another occurrence of the same one) must survive.
    if ((uint8_t)persist_read_int(PERSIST_KEY_SNOOZE_MED)  != med_index) return;
    if ((time_t)persist_read_int(PERSIST_KEY_SNOOZE_DOSE)  != dose_time) return;
    persist_write_int(PERSIST_KEY_SNOOZE,      0);
    persist_write_int(PERSIST_KEY_SNOOZE_MED,  0xFF);
    persist_write_int(PERSIST_KEY_SNOOZE_DOSE, 0);
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
        // Read which dose was snoozed, clear all snooze state, then re-show
        // the reminder directly.  We do NOT re-search by time window here —
        // the snooze fires snoozeMins after the original dose, which is outside
        // DUE_WINDOW_SECS, so a window search would always return due_count==0
        // and fall through to dose_list_window_push() instead of the alert.
        uint8_t   med_index = (uint8_t)persist_read_int(PERSIST_KEY_SNOOZE_MED);
        time_t    dose_ts   = (time_t)persist_read_int(PERSIST_KEY_SNOOZE_DOSE);
        MedEntry *snooze_med = med_list_get(med_index);
        AppSettings *settings = med_list_get_settings();

        // Keep-nagging mode: re-arm the snooze BEFORE re-alerting, so an alert
        // the user ignores fires again after snoozeMins.  Confirming the dose
        // (Taken/Skip) clears it via notifications_cancel_snooze().
        if (snooze_med && settings->repeatSnooze) {
            uint16_t snooze_mins = (settings->snoozeMins > 0) ? settings->snoozeMins : 15;
            persist_write_int(PERSIST_KEY_SNOOZE, (int32_t)(now + (time_t)snooze_mins * 60));
        } else {
            persist_write_int(PERSIST_KEY_SNOOZE,      0);
            persist_write_int(PERSIST_KEY_SNOOZE_MED,  0xFF);
            persist_write_int(PERSIST_KEY_SNOOZE_DOSE, 0);
        }
        notifications_schedule_wakeups();
        light_enable_interaction();
        fire_vibe(snooze_med ? snooze_med->vibePattern  : MED_VIBE_SHORT,
                  snooze_med ? snooze_med->vibeStrength : MED_VIBE_LIGHT);
        if (!snooze_med) {
            dose_list_window_push();  // fallback: med was removed since snooze set
        } else if (detail_window_alert_is_open(med_index, dose_ts)) {
            // Alert still on screen — it was just re-vibrated above; pushing a
            // second copy would stack windows.
            return;
        } else {
            detail_window_push(med_index, dose_ts, DETAIL_MODE_ALERT);
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
    MedEntry *first_due = med_list_get(due_indices[0]);
    fire_vibe(first_due ? first_due->vibePattern  : MED_VIBE_SHORT,
              first_due ? first_due->vibeStrength : MED_VIBE_LIGHT);
    detail_window_push(due_indices[0], due_times[0], DETAIL_MODE_ALERT);

    // Keep-nagging.  The nag has to be armed when the alert *appears*, not
    // only when the user presses Snooze: someone who ignores the alert
    // (no button at all) must still be re-alerted after snoozeMins.  The
    // snooze wakeup handler re-arms itself while repeatSnooze is set, and
    // Taken/Skip clears it via notifications_cancel_snooze().
    AppSettings *settings = med_list_get_settings();
    if (settings->repeatSnooze) {
        notifications_schedule_snooze(due_indices[0], due_times[0]);
    } else {
        notifications_schedule_wakeups();
    }
}

// ---------------------------------------------------------------------------
// Public: Timeline pin action launch handler
// ---------------------------------------------------------------------------

void notifications_handle_timeline_action(uint32_t launch_code) {
    // launch_code 1 = Taken, 2 = Snooze (from Timeline pin actions, Phase 4)
    APP_LOG(APP_LOG_LEVEL_INFO, "Timeline action: %lu", (unsigned long)launch_code);
    dose_list_window_push();
}
