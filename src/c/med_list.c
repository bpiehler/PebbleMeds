#include "med_list.h"
#include <pebble.h>
#include <string.h>

#define TEST_MODE 0

// Persist key layout:
//   0        = uint8  med count
//   1 .. 16  = MedEntry[0..15]  (one per key, each ~108 bytes < 256 byte limit)
//   17       = AppSettings
#define PERSIST_KEY_MED_COUNT 0
#define PERSIST_KEY_MED_BASE  1
#define PERSIST_KEY_SETTINGS  17

static MedEntry  s_meds[MED_MAX];
static uint8_t   s_count = 0;
static AppSettings s_settings = { .snoozeMins = 15, .privacyMode = false };

// ---------------------------------------------------------------------------
// Test Data
// ---------------------------------------------------------------------------

#if TEST_MODE
static void load_test_data(void) {
    s_count = 3;
    
    // 1. Fixed time (Self)
    strncpy(s_meds[0].taker, "Self", sizeof(s_meds[0].taker));
    strncpy(s_meds[0].name, "Multivitamin", sizeof(s_meds[0].name));
    strncpy(s_meds[0].dose, "1 tablet", sizeof(s_meds[0].dose));
    s_meds[0].scheduleType = SCHEDULE_FIXED;
    s_meds[0].timeCount = 1;
    s_meds[0].times[0] = (TimeEntry){ .h = 8, .m = 0 };
    s_meds[0].shape = SHAPE_ROUND;
    s_meds[0].color = GColorYellow;

    // 2. Interval (Murray the dog)
    strncpy(s_meds[1].taker, "Murray", sizeof(s_meds[1].taker));
    strncpy(s_meds[1].name, "Apoquel", sizeof(s_meds[1].name));
    strncpy(s_meds[1].dose, "16mg", sizeof(s_meds[1].dose));
    s_meds[1].scheduleType = SCHEDULE_INTERVAL;
    s_meds[1].intervalHours = 12;
    s_meds[1].startHour = 9;
    s_meds[1].startMinute = 0;
    s_meds[1].lastTakenTs = 0; // Will use start time
    s_meds[1].shape = SHAPE_OVAL;
    s_meds[1].color = GColorWhite;

    // 3. Multiple times (Self)
    strncpy(s_meds[2].taker, "Self", sizeof(s_meds[2].taker));
    strncpy(s_meds[2].name, "Ibuprofen", sizeof(s_meds[2].name));
    strncpy(s_meds[2].dose, "400mg", sizeof(s_meds[2].dose));
    s_meds[2].scheduleType = SCHEDULE_FIXED;
    s_meds[2].timeCount = 2;
    s_meds[2].times[0] = (TimeEntry){ .h = 10, .m = 0 };
    s_meds[2].times[1] = (TimeEntry){ .h = 22, .m = 0 };
    s_meds[2].shape = SHAPE_OBLONG;
    s_meds[2].color = GColorRed;
}
#endif

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void med_list_init(void) {
    if (persist_exists(PERSIST_KEY_MED_COUNT)) {
        s_count = (uint8_t)persist_read_int(PERSIST_KEY_MED_COUNT);
        if (s_count > MED_MAX) s_count = MED_MAX;
        for (uint8_t i = 0; i < s_count; i++) {
            persist_read_data(PERSIST_KEY_MED_BASE + i, &s_meds[i], sizeof(MedEntry));
        }
    } 
#if TEST_MODE
    else {
        load_test_data();
    }
#endif

    if (persist_exists(PERSIST_KEY_SETTINGS)) {
        persist_read_data(PERSIST_KEY_SETTINGS, &s_settings, sizeof(AppSettings));
    }
}

void med_list_deinit(void) {
    persist_write_int(PERSIST_KEY_MED_COUNT, s_count);
    for (uint8_t i = 0; i < s_count; i++) {
        persist_write_data(PERSIST_KEY_MED_BASE + i, &s_meds[i], sizeof(MedEntry));
    }
    med_list_save_settings();
}

uint8_t med_list_count(void) {
    return s_count;
}

MedEntry *med_list_get(uint8_t index) {
    if (index >= s_count) return NULL;
    return &s_meds[index];
}

void med_list_set_count(uint8_t count) {
    s_count = (count > MED_MAX) ? MED_MAX : count;
}

void med_list_set(uint8_t index, const MedEntry *entry) {
    if (index < MED_MAX) {
        memcpy(&s_meds[index], entry, sizeof(MedEntry));
    }
}

AppSettings *med_list_get_settings(void) {
    return &s_settings;
}

void med_list_save_settings(void) {
    persist_write_data(PERSIST_KEY_SETTINGS, &s_settings, sizeof(AppSettings));
}

// NOTE: This function has a pure-JS counterpart in src/pkjs/schedule.js
// (getNextDoseTimes / getFixedTimes / getIntervalTimes).  Keep the two in
// sync — any change to scheduling logic here must be reflected there, and
// the Jest tests in tests/schedule.test.js updated to match.
time_t med_list_next_dose_time(const MedEntry *med, time_t after) {
    struct tm t_after = *localtime(&after);

    if (med->scheduleType == SCHEDULE_FIXED) {
        time_t best = 0;
        // Check all times for the earliest one today/tomorrow
        for (int i = 0; i < med->timeCount; i++) {
            struct tm t = t_after;
            t.tm_hour = med->times[i].h;
            t.tm_min  = med->times[i].m;
            t.tm_sec  = 0;
            time_t occ = mktime(&t);
            if (occ <= after) occ += 24 * 3600; // Tomorrow
            if (best == 0 || occ < best) best = occ;
        }
        return best;
    } else {
        // Interval: lastTakenTs + hours (fallback to start time if lastTakenTs is 0)
        if (med->lastTakenTs == 0) {
            struct tm t = t_after;
            t.tm_hour = med->startHour;
            t.tm_min  = med->startMinute;
            t.tm_sec  = 0;
            time_t occ = mktime(&t);
            if (occ <= after) {
                // How many intervals have passed since start time today?
                int seconds_since = (int)(after - occ);
                int intervals = (seconds_since / (med->intervalHours * 3600)) + 1;
                occ += intervals * (med->intervalHours * 3600);
            }
            return occ;
        } else {
            // Start from lastTakenTs + one interval, then advance by full
            // intervals until we are strictly past 'after'.  Without this,
            // a stale lastTakenTs (more than 4 intervals in the past) causes
            // collect_dose_events to fill all 4 per-med slots with past
            // occurrences and schedule nothing for this med.
            time_t next = (time_t)med->lastTakenTs + (time_t)med->intervalHours * 3600;
            if (next <= after) {
                int seconds_since = (int)(after - next);
                int intervals = (seconds_since / (med->intervalHours * 3600)) + 1;
                next += intervals * (med->intervalHours * 3600);
            }
            return next;
        }
    }
}
