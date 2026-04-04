#include "dose_list_window.h"
#include <pebble.h>

// Stub — full implementation in Phase 2.
// Shows a placeholder window so the app launches without crashing.

static Window *s_window;
static TextLayer *s_text_layer;

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);
    s_text_layer = text_layer_create(bounds);
    text_layer_set_text(s_text_layer, "PebbleMeds\n\nConfigure via\nPebble app.");
    text_layer_set_text_alignment(s_text_layer, GTextAlignmentCenter);
    text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    layer_add_child(root, text_layer_get_layer(s_text_layer));
}

static void window_unload(Window *window) {
    text_layer_destroy(s_text_layer);
}

void dose_list_window_push(void) {
    if (!s_window) {
        s_window = window_create();
        window_set_window_handlers(s_window, (WindowHandlers){
            .load   = window_load,
            .unload = window_unload,
        });
    }
    window_stack_push(s_window, true);
}
