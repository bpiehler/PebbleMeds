#include "skip_confirm.h"

static Window              *s_window;
static TextLayer           *s_text_layer;
static ActionBarLayer      *s_action_bar;
static GBitmap             *s_icon_check;
static GBitmap             *s_icon_x;
static SkipConfirmCallback  s_on_confirm;

static void confirm_click(ClickRecognizerRef rec, void *ctx) {
    SkipConfirmCallback cb = s_on_confirm;
    window_stack_remove(s_window, false);
    if (cb) cb();
}

static void cancel_click(ClickRecognizerRef rec, void *ctx) {
    window_stack_remove(s_window, true);
}

static void click_config_provider(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_UP, confirm_click);
    window_single_click_subscribe(BUTTON_ID_DOWN, cancel_click);
    window_single_click_subscribe(BUTTON_ID_BACK, cancel_click);
}

static void window_load(Window *window) {
    Layer *root   = window_get_root_layer(window);
    GRect  bounds = layer_get_bounds(root);
    window_set_background_color(window, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorWhite));

    s_icon_check = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_TAKEN);
    s_icon_x     = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_SKIP);
    s_action_bar = action_bar_layer_create();
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP, s_icon_check);
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_DOWN, s_icon_x);
    action_bar_layer_set_click_config_provider(s_action_bar, click_config_provider);
    action_bar_layer_add_to_window(s_action_bar, window);

    GFont font   = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    int   inset  = PBL_IF_ROUND_ELSE(24, 6);
    int   text_w = bounds.size.w - ACTION_BAR_WIDTH - inset * 2;
    GRect box    = GRect(inset, 0, text_w, bounds.size.h);
    const char *text = "Skip this dose?";
    GSize size = graphics_text_layout_get_content_size(text, font, box,
                     GTextOverflowModeWordWrap, GTextAlignmentCenter);
    box.origin.y = (bounds.size.h - size.h) / 2 - 4;
    box.size.h   = size.h + 8;

    s_text_layer = text_layer_create(box);
    text_layer_set_text(s_text_layer, text);
    text_layer_set_font(s_text_layer, font);
    text_layer_set_text_alignment(s_text_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_text_layer, GColorClear);
    text_layer_set_text_color(s_text_layer, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
    layer_add_child(root, text_layer_get_layer(s_text_layer));
}

static void window_unload(Window *window) {
    text_layer_destroy(s_text_layer);
    action_bar_layer_destroy(s_action_bar);
    gbitmap_destroy(s_icon_check);
    gbitmap_destroy(s_icon_x);
    window_destroy(window);
    s_window = NULL;
}

void skip_confirm_push(SkipConfirmCallback on_confirm) {
    if (s_window) return;
    s_on_confirm = on_confirm;
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);
}
