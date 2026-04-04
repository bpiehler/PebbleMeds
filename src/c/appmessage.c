#include "appmessage.h"
#include <pebble.h>

// AppMessage key IDs — must match appinfo.json appKeys.
#define KEY_CONFIG_JSON  0
#define KEY_CHUNK_INDEX  1
#define KEY_CHUNK_TOTAL  2
#define KEY_ACTION       3
#define KEY_MED_INDEX    4
#define KEY_DOSE_TS      5
#define KEY_REQUEST_SYNC 6

// Reassembly buffer for chunked JSON from the phone.
#define JSON_BUF_SIZE 3200   // 16 meds * ~200 bytes each

static char     s_json_buf[JSON_BUF_SIZE];
static uint8_t  s_expected_chunks = 0;
static uint8_t  s_received_chunks = 0;

// Forward declaration — implemented in Phase 2.
static void process_config_json(const char *json);

// ---------------------------------------------------------------------------
// Inbox
// ---------------------------------------------------------------------------
static void inbox_received(DictionaryIterator *iter, void *context) {
    Tuple *chunk_total = dict_find(iter, KEY_CHUNK_TOTAL);
    Tuple *chunk_index = dict_find(iter, KEY_CHUNK_INDEX);
    Tuple *json_chunk  = dict_find(iter, KEY_CONFIG_JSON);

    if (chunk_total && chunk_index && json_chunk) {
        uint8_t total = (uint8_t)chunk_total->value->uint8;
        uint8_t index = (uint8_t)chunk_index->value->uint8;
        const char *chunk = json_chunk->value->cstring;

        if (index == 0) {
            // First chunk — reset buffer
            s_json_buf[0]    = '\0';
            s_expected_chunks = total;
            s_received_chunks = 0;
        }

        // Append chunk to buffer if it fits
        size_t current_len = strlen(s_json_buf);
        size_t chunk_len   = strlen(chunk);
        if (current_len + chunk_len < JSON_BUF_SIZE) {
            strcat(s_json_buf, chunk);
            s_received_chunks++;
        } else {
            APP_LOG(APP_LOG_LEVEL_ERROR, "JSON buffer overflow");
        }

        if (s_received_chunks == s_expected_chunks) {
            process_config_json(s_json_buf);
        }
    }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "AppMessage dropped: %d", (int)reason);
}

// ---------------------------------------------------------------------------
// Outbox
// ---------------------------------------------------------------------------
static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "AppMessage send failed: %d", (int)reason);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void appmessage_init(void) {
    app_message_register_inbox_received(inbox_received);
    app_message_register_inbox_dropped(inbox_dropped);
    app_message_register_outbox_failed(outbox_failed);
    app_message_open(512, 256);
}

void appmessage_deinit(void) {
    app_message_deregister_callbacks();
}

void appmessage_send_action(uint8_t med_index, const char *action, uint32_t dose_ts) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
    dict_write_cstring(iter, KEY_ACTION,    action);
    dict_write_uint8(iter,   KEY_MED_INDEX, med_index);
    dict_write_uint32(iter,  KEY_DOSE_TS,   dose_ts);
    app_message_outbox_send();
}

void appmessage_request_sync(void) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
    dict_write_uint8(iter, KEY_REQUEST_SYNC, 1);
    app_message_outbox_send();
}

// ---------------------------------------------------------------------------
// JSON processing — stub for Phase 2
// ---------------------------------------------------------------------------
static void process_config_json(const char *json) {
    // Full JSON parsing implemented in Phase 2.
    APP_LOG(APP_LOG_LEVEL_INFO, "Config received (%d bytes)", (int)strlen(json));
}
