#include "appmessage.h"
#include "med_list.h"
#include "dose_list_window.h"
#include "notifications.h"
#include "jsmn.h"
#include <pebble.h>
#include <string.h>
#include <stdlib.h>

// AppMessage key IDs — must match appinfo.json appKeys.
#define KEY_CONFIG_JSON  0
#define KEY_CHUNK_INDEX  1
#define KEY_CHUNK_TOTAL  2
#define KEY_ACTION       3
#define KEY_MED_INDEX    4
#define KEY_DOSE_TS      5
#define KEY_REQUEST_SYNC 6

// Chunked JSON reassembly. 16 meds * ~200 bytes each + overhead.
#define JSON_BUF_SIZE    3300
// jsmn token budget for a single med object (worst case ~35 tokens; 64 is ample).
#define MED_TOKEN_COUNT  64

static char    s_json_buf[JSON_BUF_SIZE];
static uint8_t s_expected_chunks = 0;
static uint8_t s_received_chunks = 0;

// ---------------------------------------------------------------------------
// jsmn helpers
// ---------------------------------------------------------------------------

static bool jsmn_eq(const char *js, jsmntok_t *tok, const char *s) {
    return tok->type == JSMN_STRING
        && (int)strlen(s) == tok->end - tok->start
        && strncmp(js + tok->start, s, (size_t)(tok->end - tok->start)) == 0;
}

static void jsmn_str(const char *js, jsmntok_t *tok, char *buf, size_t buflen) {
    size_t len = (size_t)(tok->end - tok->start);
    if (len >= buflen) len = buflen - 1;
    strncpy(buf, js + tok->start, len);
    buf[len] = '\0';
}

static int jsmn_toi(const char *js, jsmntok_t *tok) {
    char buf[16];
    jsmn_str(js, tok, buf, sizeof(buf));
    return atoi(buf);
}

// Returns the number of tokens in the subtree rooted at tokens[i].
static int jsmn_subtree_size(jsmntok_t *tokens, int i) {
    int j = i + 1;
    for (int c = 0; c < tokens[i].size; c++) {
        j += jsmn_subtree_size(tokens, j);
    }
    return j - i;
}

// ---------------------------------------------------------------------------
// JSON string helpers: find matching brace/bracket accounting for strings
// ---------------------------------------------------------------------------

static const char *find_matching_brace(const char *start) {
    int depth = 0;
    bool in_str = false;
    for (const char *p = start; *p != '\0'; p++) {
        if (in_str) {
            if (*p == '\\') { p++; continue; }
            if (*p == '"')  in_str = false;
        } else {
            if (*p == '"') { in_str = true; continue; }
            if (*p == '{') depth++;
            if (*p == '}' && --depth == 0) return p;
        }
    }
    return NULL;
}

static const char *find_matching_bracket(const char *start) {
    int depth = 0;
    bool in_str = false;
    for (const char *p = start; *p != '\0'; p++) {
        if (in_str) {
            if (*p == '\\') { p++; continue; }
            if (*p == '"')  in_str = false;
        } else {
            if (*p == '"') { in_str = true; continue; }
            if (*p == '[') depth++;
            if (*p == ']' && --depth == 0) return p;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Color / shape name → value
// ---------------------------------------------------------------------------

static GColor gcolor_from_name(const char *name) {
    if (strcmp(name, "GColorBlack") == 0)                    return GColorBlack;
    if (strcmp(name, "GColorDarkGray") == 0)                 return GColorDarkGray;
    if (strcmp(name, "GColorLightGray") == 0)                return GColorLightGray;
    if (strcmp(name, "GColorRed") == 0)                      return GColorRed;
    if (strcmp(name, "GColorOrange") == 0)                   return GColorOrange;
    if (strcmp(name, "GColorChromeYellow") == 0)             return GColorChromeYellow;
    if (strcmp(name, "GColorYellow") == 0)                   return GColorYellow;
    if (strcmp(name, "GColorGreen") == 0)                    return GColorGreen;
    if (strcmp(name, "GColorMintGreen") == 0)                return GColorMintGreen;
    if (strcmp(name, "GColorTiffanyBlue") == 0)              return GColorTiffanyBlue;
    if (strcmp(name, "GColorCyan") == 0)                     return GColorCyan;
    if (strcmp(name, "GColorBlue") == 0)                     return GColorBlue;
    if (strcmp(name, "GColorLiberty") == 0)                  return GColorLiberty;
    if (strcmp(name, "GColorVividViolet") == 0)              return GColorVividViolet;
    if (strcmp(name, "GColorMagenta") == 0)                  return GColorMagenta;
    if (strcmp(name, "GColorRichBrilliantLavender") == 0)    return GColorRichBrilliantLavender;
    if (strcmp(name, "GColorBrass") == 0)                    return GColorBrass;
    return GColorWhite;
}

static PillShape shape_from_name(const char *name) {
    if (strcmp(name, "oval") == 0)   return SHAPE_OVAL;
    if (strcmp(name, "shield") == 0) return SHAPE_SHIELD;
    if (strcmp(name, "oblong") == 0) return SHAPE_OBLONG;
    if (strcmp(name, "drop") == 0)   return SHAPE_DROP;
    return SHAPE_ROUND;
}

// ---------------------------------------------------------------------------
// Parse one med object substring into a MedEntry
// ---------------------------------------------------------------------------

static void parse_med_object(const char *js, size_t len, MedEntry *med) {
    jsmn_parser parser;
    jsmntok_t tokens[MED_TOKEN_COUNT];
    jsmn_init(&parser);

    int r = jsmn_parse(&parser, js, len, tokens, MED_TOKEN_COUNT);
    if (r < 1 || tokens[0].type != JSMN_OBJECT) return;

    int j = 1;
    for (int k = 0; k < tokens[0].size; k++) {
        jsmntok_t *key = &tokens[j];
        jsmntok_t *val = &tokens[j + 1];

        if (jsmn_eq(js, key, "taker")) {
            jsmn_str(js, val, med->taker, sizeof(med->taker));

        } else if (jsmn_eq(js, key, "name")) {
            jsmn_str(js, val, med->name, sizeof(med->name));

        } else if (jsmn_eq(js, key, "dose")) {
            jsmn_str(js, val, med->dose, sizeof(med->dose));

        } else if (jsmn_eq(js, key, "scheduleType")) {
            char buf[16];
            jsmn_str(js, val, buf, sizeof(buf));
            med->scheduleType = (strcmp(buf, "interval") == 0) ? SCHEDULE_INTERVAL : SCHEDULE_FIXED;

        } else if (jsmn_eq(js, key, "intervalHours")) {
            med->intervalHours = (uint8_t)jsmn_toi(js, val);

        } else if (jsmn_eq(js, key, "startHour")) {
            med->startHour = (uint8_t)jsmn_toi(js, val);

        } else if (jsmn_eq(js, key, "startMinute")) {
            med->startMinute = (uint8_t)jsmn_toi(js, val);

        } else if (jsmn_eq(js, key, "lastTakenTs")) {
            med->lastTakenTs = (uint32_t)jsmn_toi(js, val);

        } else if (jsmn_eq(js, key, "shape")) {
            char buf[16];
            jsmn_str(js, val, buf, sizeof(buf));
            med->shape = shape_from_name(buf);

        } else if (jsmn_eq(js, key, "color")) {
            char buf[48];
            jsmn_str(js, val, buf, sizeof(buf));
            med->color = gcolor_from_name(buf);

        } else if (jsmn_eq(js, key, "times") && val->type == JSMN_ARRAY) {
            uint8_t count = (uint8_t)(val->size > 4 ? 4 : val->size);
            med->timeCount = count;
            int ti = j + 2; // past "times" key + array token
            for (uint8_t t = 0; t < count; t++) {
                int tpairs = tokens[ti].size;
                int tij = ti + 1;
                for (int tp = 0; tp < tpairs; tp++) {
                    if (jsmn_eq(js, &tokens[tij], "h"))
                        med->times[t].h = (uint8_t)jsmn_toi(js, &tokens[tij + 1]);
                    else if (jsmn_eq(js, &tokens[tij], "m"))
                        med->times[t].m = (uint8_t)jsmn_toi(js, &tokens[tij + 1]);
                    tij += jsmn_subtree_size(tokens, tij);
                }
                ti = tij;
            }
        }

        j += jsmn_subtree_size(tokens, j); // advance past key + value subtree
    }
}

// ---------------------------------------------------------------------------
// Parse settings object substring
// ---------------------------------------------------------------------------

static void parse_settings_object(const char *js, size_t len) {
    jsmn_parser parser;
    jsmntok_t tokens[16];
    jsmn_init(&parser);

    int r = jsmn_parse(&parser, js, len, tokens, 16);
    if (r < 1 || tokens[0].type != JSMN_OBJECT) return;

    AppSettings *s = med_list_get_settings();
    int j = 1;
    for (int k = 0; k < tokens[0].size; k++) {
        jsmntok_t *key = &tokens[j];
        jsmntok_t *val = &tokens[j + 1];

        if (jsmn_eq(js, key, "snoozeMins")) {
            s->snoozeMins = (uint16_t)jsmn_toi(js, val);
        } else if (jsmn_eq(js, key, "privacyMode")) {
            char buf[8];
            jsmn_str(js, val, buf, sizeof(buf));
            s->privacyMode = (strcmp(buf, "true") == 0);
        }
        j += jsmn_subtree_size(tokens, j);
    }
    med_list_save_settings();
}

// ---------------------------------------------------------------------------
// Find a root-level JSON key's value start.
// Searches for "key": — safe because we control the format from config.html.
// ---------------------------------------------------------------------------

static const char *find_root_value(const char *json, const char *key) {
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pattern)) != NULL) {
        const char *after = p + strlen(pattern);
        while (*after == ' ' || *after == '\t') after++;
        if (*after == ':') return after + 1; // points to value (may have leading space)
        p += strlen(pattern); // false positive, keep searching
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Main JSON processing — called once all chunks have been assembled
// ---------------------------------------------------------------------------

static void process_config_json(const char *json) {
    uint8_t new_count = 0;

    // ---- Parse meds array ----
    const char *meds_val = find_root_value(json, "meds");
    if (meds_val) {
        while (*meds_val == ' ' || *meds_val == '\t') meds_val++;
        if (*meds_val == '[') {
            const char *arr_end = find_matching_bracket(meds_val);
            const char *p = meds_val + 1;
            while (new_count < MED_MAX && p && p < arr_end) {
                while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',') p++;
                if (*p != '{') break;
                const char *obj_end = find_matching_brace(p);
                if (!obj_end) break;

                MedEntry entry;
                memset(&entry, 0, sizeof(entry));
                entry.color = GColorWhite;
                parse_med_object(p, (size_t)(obj_end - p + 1), &entry);
                med_list_set(new_count, &entry);
                new_count++;
                p = obj_end + 1;
            }
        }
    }
    med_list_set_count(new_count);

    // ---- Parse settings ----
    const char *settings_val = find_root_value(json, "settings");
    if (settings_val) {
        while (*settings_val == ' ' || *settings_val == '\t') settings_val++;
        if (*settings_val == '{') {
            const char *obj_end = find_matching_brace(settings_val);
            if (obj_end) {
                parse_settings_object(settings_val, (size_t)(obj_end - settings_val + 1));
            }
        }
    }

    APP_LOG(APP_LOG_LEVEL_INFO, "Config applied: %d meds", new_count);
    dose_list_window_refresh();
    notifications_schedule_wakeups();
}

// ---------------------------------------------------------------------------
// AppMessage inbox
// ---------------------------------------------------------------------------

static void inbox_received(DictionaryIterator *iter, void *context) {
    Tuple *total_t = dict_find(iter, KEY_CHUNK_TOTAL);
    Tuple *index_t = dict_find(iter, KEY_CHUNK_INDEX);
    Tuple *json_t  = dict_find(iter, KEY_CONFIG_JSON);

    if (total_t && index_t && json_t) {
        uint8_t total = total_t->value->uint8;
        uint8_t index = index_t->value->uint8;
        const char *chunk = json_t->value->cstring;

        if (index == 0) {
            s_json_buf[0]     = '\0';
            s_expected_chunks = total;
            s_received_chunks = 0;
        }

        size_t cur_len   = strlen(s_json_buf);
        size_t chunk_len = strlen(chunk);
        if (cur_len + chunk_len < JSON_BUF_SIZE) {
            strcat(s_json_buf, chunk);
            s_received_chunks++;
        } else {
            APP_LOG(APP_LOG_LEVEL_ERROR, "JSON buffer overflow at chunk %d", index);
        }

        if (s_received_chunks == s_expected_chunks) {
            process_config_json(s_json_buf);
        }
    }

    if (dict_find(iter, KEY_REQUEST_SYNC)) {
        // Phone is requesting watch to ask for a fresh sync (shouldn't normally happen)
        APP_LOG(APP_LOG_LEVEL_INFO, "Sync requested from phone side");
    }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "AppMessage dropped: %d", (int)reason);
}

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
    app_message_open(768, 256);
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
