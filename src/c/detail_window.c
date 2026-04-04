#include "detail_window.h"
#include "med_list.h"
#include "appmessage.h"
#include <pebble.h>

// Stub — full pill drawing, privacy reveal, and Taken/Snooze actions in Phase 3.

static Window    *s_window;
static TextLayer *s_text_layer;
static uint8_t    s_med_index;
static time_t     s_dose_time;

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);

    s_text_layer = text_layer_create(bounds);
    text_layer_set_text_alignment(s_text_layer, GTextAlignmentCenter);
    text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

    MedEntry *med = med_list_get(s_med_index);
    AppSettings *settings = med_list_get_settings();

    static char s_buf[80];
    if (med && !settings->privacyMode) {
        snprintf(s_buf, sizeof(s_buf), "%s\n%s\n%s", med->name, med->taker,
                 med->dose[0] ? med->dose : "");
    } else if (med) {
        snprintf(s_buf, sizeof(s_buf), "Medication Due\n%s\n(Press Select\nto reveal)", med->taker);
    } else {
        snprintf(s_buf, sizeof(s_buf), "Unknown medication");
    }
    text_layer_set_text(s_text_layer, s_buf);
    layer_add_child(root, text_layer_get_layer(s_text_layer));
}

static void window_unload(Window *window) {
    text_layer_destroy(s_text_layer);
}

void detail_window_push(uint8_t med_index, time_t dose_time) {
    s_med_index = med_index;
    s_dose_time = dose_time;

    if (!s_window) {
        s_window = window_create();
        window_set_window_handlers(s_window, (WindowHandlers){
            .load   = window_load,
            .unload = window_unload,
        });
    }
    window_stack_push(s_window, true);
}
