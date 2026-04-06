#include "med_list.h"
#include <pebble.h>

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

void med_list_init(void) {
    if (persist_exists(PERSIST_KEY_MED_COUNT)) {
        s_count = (uint8_t)persist_read_int(PERSIST_KEY_MED_COUNT);
        if (s_count > MED_MAX) s_count = MED_MAX;
        for (uint8_t i = 0; i < s_count; i++) {
            persist_read_data(PERSIST_KEY_MED_BASE + i, &s_meds[i], sizeof(MedEntry));
        }
    }
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
    if (index >= MED_MAX || !entry) return;
    s_meds[index] = *entry;
}

AppSettings *med_list_get_settings(void) {
    return &s_settings;
}

void med_list_save_settings(void) {
    persist_write_data(PERSIST_KEY_SETTINGS, &s_settings, sizeof(AppSettings));
}

time_t med_list_next_dose_time(const MedEntry *med, time_t after) {
    if (med->scheduleType == SCHEDULE_FIXED) {
        struct tm *t = localtime(&after);
        time_t midnight = after - ((time_t)t->tm_hour * 3600
                                 + (time_t)t->tm_min  * 60
                                 + (time_t)t->tm_sec);
        time_t best = 0;
        for (uint8_t day = 0; day <= 1; day++) {
            for (uint8_t i = 0; i < med->timeCount; i++) {
                time_t ts = midnight
                          + (time_t)day * 86400
                          + (time_t)med->times[i].h * 3600
                          + (time_t)med->times[i].m * 60;
                if (ts > after && (best == 0 || ts < best)) best = ts;
            }
        }
        return best ? best : after + 86400;
    } else {
        if (med->lastTakenTs > 0) {
            time_t next = (time_t)med->lastTakenTs + (time_t)med->intervalHours * 3600;
            while (next <= after) next += (time_t)med->intervalHours * 3600;
            return next;
        } else {
            struct tm *t = localtime(&after);
            time_t midnight = after - ((time_t)t->tm_hour * 3600
                                     + (time_t)t->tm_min  * 60
                                     + (time_t)t->tm_sec);
            time_t start = midnight
                         + (time_t)med->startHour   * 3600
                         + (time_t)med->startMinute * 60;
            while (start <= after) start += (time_t)med->intervalHours * 3600;
            return start;
        }
    }
}
