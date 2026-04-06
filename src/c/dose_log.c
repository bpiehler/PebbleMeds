#include "dose_log.h"

// Persist key 19: ring-buffer dose log
// Layout: { uint8_t count; uint8_t head; DoseLogEntry entries[32]; }
// Size: 2 + 32 * 8 = 258 bytes — fits in one persist slot (256-byte limit requires
// careful sizing; entries trimmed to 30 max for safety: 2 + 30*8 = 242 bytes).

#define DOSE_LOG_MAX      30
#define PERSIST_KEY_DOSE_LOG 19

typedef struct {
    uint8_t      count;
    uint8_t      head;
    DoseLogEntry entries[DOSE_LOG_MAX];
} DoseLogStore;

static DoseLogStore s_store;

void dose_log_init(void) {
    s_store.count = 0;
    s_store.head  = 0;
    if (persist_exists(PERSIST_KEY_DOSE_LOG)) {
        persist_read_data(PERSIST_KEY_DOSE_LOG, &s_store, sizeof(DoseLogStore));
        if (s_store.count > DOSE_LOG_MAX) s_store.count = DOSE_LOG_MAX;
        if (s_store.head  >= DOSE_LOG_MAX) s_store.head  = 0;
    }
}

void dose_log_deinit(void) {
    persist_write_data(PERSIST_KEY_DOSE_LOG, &s_store, sizeof(DoseLogStore));
}

void dose_log_record(uint8_t med_index, DoseAction action, uint32_t ts) {
    s_store.entries[s_store.head] = (DoseLogEntry){
        .ts        = ts,
        .med_index = med_index,
        .action    = action,
        ._pad      = 0,
    };
    s_store.head = (s_store.head + 1) % DOSE_LOG_MAX;
    if (s_store.count < DOSE_LOG_MAX) s_store.count++;

    // Flush immediately so nothing is lost on crash
    persist_write_data(PERSIST_KEY_DOSE_LOG, &s_store, sizeof(DoseLogStore));
}

uint8_t dose_log_count(void) {
    return s_store.count;
}

DoseLogEntry dose_log_get(uint8_t index) {
    // index 0 = oldest, count-1 = newest
    uint8_t ring_index = (s_store.head - s_store.count + index + DOSE_LOG_MAX) % DOSE_LOG_MAX;
    return s_store.entries[ring_index];
}
