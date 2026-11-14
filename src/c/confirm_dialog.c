#include "confirm_dialog.h"

#define DIALOG_TIMEOUT_MS 2000  // matches the system alarm dialogs
#define FRAME_MS          33
#define ICON_SIZE         80
#define ICON_PAD          26  // the animation draws up to 26 px past its 80 px box

// The animation is a PebbleOS draw command sequence, which aplite lacks.
#if defined(PBL_PLATFORM_APLITE)
#define HAS_ANIM 0
#else
#define HAS_ANIM 1
#endif

static Window    *s_window;
static Layer     *s_icon_layer;
static TextLayer *s_text_layer;
static AppTimer  *s_timeout_timer;
static char       s_text[48];
#if HAS_ANIM
static GDrawCommandSequence *s_seq;
static AppTimer             *s_frame_timer;
static uint32_t              s_elapsed;
#endif

#if HAS_ANIM
static void frame_cb(void *ctx) {
    s_frame_timer = NULL;
    s_elapsed += FRAME_MS;
    layer_mark_dirty(s_icon_layer);
    if (s_elapsed < gdraw_command_sequence_get_total_duration(s_seq)) {
        s_frame_timer = app_timer_register(FRAME_MS, frame_cb, NULL);
    }
}

static void icon_update_proc(Layer *layer, GContext *ctx) {
    if (!s_seq) return;
    GDrawCommandFrame *frame = gdraw_command_sequence_get_frame_by_elapsed(s_seq, s_elapsed);
    if (frame) gdraw_command_frame_draw(ctx, s_seq, frame, GPoint(ICON_PAD, ICON_PAD));
}
#endif

static void timeout_cb(void *ctx) {
    s_timeout_timer = NULL;
    window_stack_remove(s_window, true);
}

static void any_click(ClickRecognizerRef rec, void *ctx) {
    window_stack_remove(s_window, true);
}

static void click_config_provider(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_UP, any_click);
    window_single_click_subscribe(BUTTON_ID_SELECT, any_click);
    window_single_click_subscribe(BUTTON_ID_DOWN, any_click);
}

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);
    // System dialogs use a larger font on the bigger displays.
#if PBL_DISPLAY_HEIGHT > 180
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    int line_h = 34;
#else
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    int line_h = 30;
#endif

    // Icon and up to two lines of text, centered as a group.
    int text_h = line_h * 2;
    int top = (bounds.size.h - (ICON_SIZE + 4 + text_h)) / 2;

    int icon_box = ICON_SIZE + ICON_PAD * 2;
    s_icon_layer = layer_create(
        GRect((bounds.size.w - icon_box) / 2, top - ICON_PAD, icon_box, icon_box));
#if HAS_ANIM
    s_seq = gdraw_command_sequence_create_with_resource(RESOURCE_ID_CONFIRMATION_ANIM);
    s_elapsed = 0;
    layer_set_update_proc(s_icon_layer, icon_update_proc);
#endif
    layer_add_child(root, s_icon_layer);

    int inset = PBL_IF_ROUND_ELSE(20, 6);
    s_text_layer = text_layer_create(
        GRect(inset, top + ICON_SIZE + 4, bounds.size.w - inset * 2, text_h));
    text_layer_set_background_color(s_text_layer, GColorClear);
    text_layer_set_text_color(s_text_layer, GColorBlack);
    text_layer_set_font(s_text_layer, font);
    text_layer_set_text_alignment(s_text_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_text_layer, GTextOverflowModeWordWrap);
    text_layer_set_text(s_text_layer, s_text);
    layer_add_child(root, text_layer_get_layer(s_text_layer));
}

static void window_appear(Window *window) {
#if HAS_ANIM
    if (s_seq && !s_frame_timer) s_frame_timer = app_timer_register(FRAME_MS, frame_cb, NULL);
#endif
    if (!s_timeout_timer) {
        s_timeout_timer = app_timer_register(DIALOG_TIMEOUT_MS, timeout_cb, NULL);
    }
}

static void window_unload(Window *window) {
    if (s_timeout_timer) { app_timer_cancel(s_timeout_timer); s_timeout_timer = NULL; }
#if HAS_ANIM
    if (s_frame_timer) { app_timer_cancel(s_frame_timer); s_frame_timer = NULL; }
    if (s_seq) { gdraw_command_sequence_destroy(s_seq); s_seq = NULL; }
#endif
    layer_destroy(s_icon_layer);
    text_layer_destroy(s_text_layer);
    window_destroy(window);
    s_window = NULL;
}

void confirm_dialog_push(const char *text) {
    if (s_window) window_stack_remove(s_window, false);
    snprintf(s_text, sizeof(s_text), "%s", text);

    s_window = window_create();
    window_set_background_color(s_window, PBL_IF_COLOR_ELSE(GColorJaegerGreen, GColorWhite));
    window_set_click_config_provider(s_window, click_config_provider);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .appear = window_appear,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);
}
