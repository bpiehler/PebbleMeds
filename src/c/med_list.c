#include "med_list.h"
#include <pebble.h>

// Persist keys
#define PERSIST_KEY_MED_COUNT 0
#define PERSIST_KEY_MED_BASE  1   // keys 1..16 hold one MedEntry each

static MedEntry s_meds[MED_MAX];
static uint8_t  s_count = 0;

void med_list_init(void) {
    if (persist_exists(PERSIST_KEY_MED_COUNT)) {
        s_count = (uint8_t)persist_read_int(PERSIST_KEY_MED_COUNT);
        if (s_count > MED_MAX) s_count = MED_MAX;
        for (uint8_t i = 0; i < s_count; i++) {
            persist_read_data(PERSIST_KEY_MED_BASE + i, &s_meds[i], sizeof(MedEntry));
        }
    }
}

void med_list_deinit(void) {
    persist_write_int(PERSIST_KEY_MED_COUNT, s_count);
    for (uint8_t i = 0; i < s_count; i++) {
        persist_write_data(PERSIST_KEY_MED_BASE + i, &s_meds[i], sizeof(MedEntry));
    }
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
