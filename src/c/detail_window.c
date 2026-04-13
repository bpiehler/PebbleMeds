#include "detail_window.h"
#include "med_list.h"
#include "appmessage.h"
#include "notifications.h"
#include "dose_log.h"
#include <pebble.h>

// ---------------------------------------------------------------------------
// Pill geometry
// ---------------------------------------------------------------------------

#if PBL_DISPLAY_WIDTH >= 200
  #define PILL_R 32
#elif defined(PBL_ROUND)
  #define PILL_R 28
#else
  #define PILL_R 20
#endif
#define PILL_CANVAS_SIZE (PILL_R * 2 + 4)

static GPoint s_shield_points[] = {
    {0,           -PILL_R},
    {PILL_R*9/10, -PILL_R/3},
    {PILL_R*9/10,  PILL_R},
    {-PILL_R*9/10, PILL_R},
    {-PILL_R*9/10, -PILL_R/3}
};
static const GPathInfo SHIELD_PATH_INFO = {
    .num_points = 5,
    .points = s_shield_points
};

// Drop shape: 10-point teardrop (pointed top, rounded bottom).
// Extra intermediate points and a centre-bottom vertex eliminate the flat
// bottom and angular sides of the old 7-point approximation.
static GPoint s_drop_points[] = {
    {0,              -PILL_R},
    { PILL_R*5/8,   -PILL_R*3/8},
    { PILL_R*9/10,   PILL_R/5},
    { PILL_R*8/10,   PILL_R*6/10},
    { PILL_R*4/10,   PILL_R*9/10},
    {0,              PILL_R},          // centre-bottom vertex rounds the tip
    {-PILL_R*4/10,   PILL_R*9/10},
    {-PILL_R*8/10,   PILL_R*6/10},
    {-PILL_R*9/10,   PILL_R/5},
    {-PILL_R*5/8,   -PILL_R*3/8},
};
static const GPathInfo DROP_PATH_INFO = {
    .num_points = 10,
    .points = s_drop_points
};

// ---------------------------------------------------------------------------
// Snooze wobble
// ---------------------------------------------------------------------------

static const int8_t WOBBLE_OFFSETS[] = {8, -8, 5, -5, 3, -2, 0};
#define WOBBLE_STEPS 7

// ---------------------------------------------------------------------------
// Window state
// ---------------------------------------------------------------------------

static Window            *s_window;
static Layer             *s_canvas_layer;
static TextLayer         *s_name_layer;
static TextLayer         *s_taker_layer;
static TextLayer         *s_dose_layer;
static TextLayer         *s_time_layer;   // "Due: HH:MM [AM/PM]"
#ifdef PBL_RECT
static ActionBarLayer    *s_action_bar;
static GBitmap           *s_icon_taken;
static GBitmap           *s_icon_snooze;
static GBitmap           *s_icon_skip;
#endif
#ifdef PBL_ROUND
static Layer             *s_hints_layer;
#endif

static GPath             *s_shield_path;
static GPath             *s_drop_path;
static PropertyAnimation *s_prop_anim;
static AppTimer          *s_wobble_timer;
static uint8_t            s_wobble_step;

static uint8_t            s_med_index;
static time_t             s_dose_time;
static bool               s_revealed;
static bool               s_action_taken;
static bool               s_entry_done;     // prevents entry anim replaying
static GRect              s_canvas_target_frame;

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

static char s_name_buf[36];
static char s_taker_buf[28];
static char s_dose_buf[28];
static char s_time_buf[24];

static void update_text(void) {
    MedEntry    *med      = med_list_get(s_med_index);
    AppSettings *settings = med_list_get_settings();
    if (!med) return;

    if (settings->privacyMode && !s_revealed) {
        snprintf(s_name_buf,  sizeof(s_name_buf),  "Medication Due");
        snprintf(s_taker_buf, sizeof(s_taker_buf), "%s", med->taker);
        s_dose_buf[0] = '\0';
    } else {
        snprintf(s_name_buf,  sizeof(s_name_buf),  "%s", med->name);
        snprintf(s_taker_buf, sizeof(s_taker_buf), "%s", med->taker);
        if (med->dose[0]) {
            snprintf(s_dose_buf, sizeof(s_dose_buf), "%s", med->dose);
        } else {
            s_dose_buf[0] = '\0';
        }
    }

    // "Due: HH:MM" / "Due: H:MM AM"
    if (s_dose_time > 0) {
        struct tm *t = localtime(&s_dose_time);
        if (clock_is_24h_style()) {
            snprintf(s_time_buf, sizeof(s_time_buf), "Due: %02d:%02d",
                     t->tm_hour, t->tm_min);
        } else {
            int h = t->tm_hour % 12;
            if (!h) h = 12;
            snprintf(s_time_buf, sizeof(s_time_buf), "Due: %d:%02d %s",
                     h, t->tm_min, t->tm_hour >= 12 ? "PM" : "AM");
        }
    } else {
        s_time_buf[0] = '\0';
    }

    text_layer_set_text(s_name_layer,  s_name_buf);
    text_layer_set_text(s_taker_layer, s_taker_buf);
    text_layer_set_text(s_dose_layer,  s_dose_buf);
    text_layer_set_text(s_time_layer,  s_time_buf);
}

// ---------------------------------------------------------------------------
// Pill drawing
// ---------------------------------------------------------------------------

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    MedEntry *med = med_list_get(s_med_index);
    if (!med) return;

    GRect  bounds = layer_get_bounds(layer);
    GPoint center = grect_center_point(&bounds);
    int    r      = PILL_R;

#ifdef PBL_COLOR
    GColor fill = med->color;
#else
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    GColor fill = GColorWhite;
#endif

    graphics_context_set_fill_color(ctx, fill);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);

    switch (med->shape) {

        case SHAPE_ROUND:
            graphics_fill_circle(ctx, center, r);
            graphics_draw_circle(ctx, center, r);
            break;

        case SHAPE_OVAL: {
            int hw = r * 7 / 10;
            GRect rect = GRect(center.x - hw, center.y - r, hw * 2, r * 2);
            graphics_fill_rect(ctx, rect, hw, GCornersAll);
            graphics_draw_round_rect(ctx, rect, hw);
            break;
        }

        case SHAPE_OBLONG: {
            int hh = r / 2;
            GRect rect = GRect(center.x - r, center.y - hh, r * 2, hh * 2);
            graphics_fill_rect(ctx, rect, hh, GCornersAll);
            graphics_draw_round_rect(ctx, rect, hh);
            break;
        }

        case SHAPE_SHIELD:
            if (s_shield_path) {
                gpath_move_to(s_shield_path, center);
                gpath_draw_filled(ctx, s_shield_path);
                gpath_draw_outline(ctx, s_shield_path);
            }
            break;

        case SHAPE_DROP:
            if (s_drop_path) {
                gpath_move_to(s_drop_path, center);
                gpath_draw_filled(ctx, s_drop_path);
                gpath_draw_outline(ctx, s_drop_path);
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// Animation helpers
// ---------------------------------------------------------------------------

// Cancel any in-progress PropertyAnimation.  Clears s_prop_anim BEFORE
// calling animation_unschedule so that any stopped-handler invocation sees
// NULL and exits early without double-freeing.
static void cancel_current_anim(void) {
    if (!s_prop_anim) return;
    PropertyAnimation *pa = s_prop_anim;
    s_prop_anim = NULL;                                         // clear first
    animation_unschedule(property_animation_get_animation(pa)); // handler early-exits
    property_animation_destroy(pa);
}

static void cancel_wobble(void) {
    if (s_wobble_timer) {
        app_timer_cancel(s_wobble_timer);
        s_wobble_timer = NULL;
    }
}

// Entry animation: pill drops in from above screen.
static void entry_anim_stopped(Animation *anim, bool finished, void *ctx) {
    if (!s_prop_anim) return;
    PropertyAnimation *pa = s_prop_anim;
    s_prop_anim = NULL;
    property_animation_destroy(pa);
    layer_set_frame(s_canvas_layer, s_canvas_target_frame);
}

// Taken animation: pill slides off the bottom of the screen.
static void taken_anim_stopped(Animation *anim, bool finished, void *ctx) {
    if (!s_prop_anim) return;
    PropertyAnimation *pa = s_prop_anim;
    s_prop_anim = NULL;
    property_animation_destroy(pa);
    notifications_schedule_wakeups();
    window_stack_pop(false);
}

// Skip animation: pill slides off the top of the screen.
static void skip_anim_stopped(Animation *anim, bool finished, void *ctx) {
    if (!s_prop_anim) return;
    PropertyAnimation *pa = s_prop_anim;
    s_prop_anim = NULL;
    property_animation_destroy(pa);
    notifications_schedule_wakeups();
    window_stack_pop(false);
}

// Snooze wobble: timer-driven horizontal shake, then pop.
static void wobble_step_cb(void *ctx) {
    s_wobble_timer = NULL;
    if (s_wobble_step >= WOBBLE_STEPS) {
        layer_set_frame(s_canvas_layer, s_canvas_target_frame);
        notifications_schedule_snooze();
        window_stack_pop(true);
        return;
    }
    GRect frame = s_canvas_target_frame;
    frame.origin.x += WOBBLE_OFFSETS[s_wobble_step++];
    layer_set_frame(s_canvas_layer, frame);
    s_wobble_timer = app_timer_register(40, wobble_step_cb, NULL);
}

// ---------------------------------------------------------------------------
// Click handlers
// ---------------------------------------------------------------------------

static void select_click(ClickRecognizerRef rec, void *ctx) {
    AppSettings *settings = med_list_get_settings();

    // Privacy mode: first press reveals, second press confirms Taken.
    if (settings->privacyMode && !s_revealed) {
        s_revealed = true;
        update_text();
        layer_mark_dirty(s_canvas_layer);
        return;
    }

    s_action_taken = true;
    dose_log_record(s_med_index, DOSE_TAKEN, (uint32_t)s_dose_time);
    appmessage_send_action(s_med_index, "taken", (uint32_t)s_dose_time);
    vibes_short_pulse();

    cancel_current_anim();
    GRect to = layer_get_frame(s_canvas_layer);
    to.origin.y = layer_get_bounds(window_get_root_layer(s_window)).size.h;
    s_prop_anim = property_animation_create_layer_frame(s_canvas_layer, NULL, &to);
    Animation *base = property_animation_get_animation(s_prop_anim);
    animation_set_duration(base, 200);
    animation_set_curve(base, AnimationCurveEaseIn);
    animation_set_handlers(base, (AnimationHandlers){ .stopped = taken_anim_stopped }, NULL);
    animation_schedule(base);
}

static void up_click(ClickRecognizerRef rec, void *ctx) {
    if (s_wobble_timer != NULL) return;   // wobble already running
    s_action_taken = true;
    dose_log_record(s_med_index, DOSE_SNOOZED, (uint32_t)s_dose_time);
    appmessage_send_action(s_med_index, "snooze", (uint32_t)s_dose_time);

    cancel_current_anim();
    // Snap to normal position so wobble starts from the correct origin.
    layer_set_frame(s_canvas_layer, s_canvas_target_frame);
    s_wobble_step  = 0;
    s_wobble_timer = app_timer_register(0, wobble_step_cb, NULL);
}

static void down_click(ClickRecognizerRef rec, void *ctx) {
    s_action_taken = true;
    dose_log_record(s_med_index, DOSE_SKIPPED, (uint32_t)s_dose_time);
    appmessage_send_action(s_med_index, "skipped", (uint32_t)s_dose_time);

    cancel_current_anim();
    cancel_wobble();
    // Animate pill off the top (inverse of the Taken slide-off-bottom).
    GRect to = layer_get_frame(s_canvas_layer);
    to.origin.y = -(PILL_CANVAS_SIZE + 10);
    s_prop_anim = property_animation_create_layer_frame(s_canvas_layer, NULL, &to);
    Animation *base = property_animation_get_animation(s_prop_anim);
    animation_set_duration(base, 200);
    animation_set_curve(base, AnimationCurveEaseIn);
    animation_set_handlers(base, (AnimationHandlers){ .stopped = skip_anim_stopped }, NULL);
    animation_schedule(base);
}

static void click_config_provider(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
    window_single_click_subscribe(BUTTON_ID_UP,     up_click);
    window_single_click_subscribe(BUTTON_ID_DOWN,   down_click);
}

// ---------------------------------------------------------------------------
// Icon hints (Round devices)
// ---------------------------------------------------------------------------
#ifdef PBL_ROUND
static void hints_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_text_color(ctx, GColorDarkGray);

    // Snooze hint (Up) - "Z"
    graphics_draw_text(ctx, "Z", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(bounds.size.w - 18, bounds.size.h / 2 - 48, 14, 14),
                       GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);

    // Taken hint (Select) - "✓"
    graphics_draw_text(ctx, "\xc2\xbb", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(bounds.size.w - 14, bounds.size.h / 2 - 12, 14, 20),
                       GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);

    // Skip hint (Down) - "X"
    graphics_draw_text(ctx, "X", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(bounds.size.w - 18, bounds.size.h / 2 + 32, 14, 14),
                       GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
}
#endif

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
    Layer *root   = window_get_root_layer(window);
    GRect  bounds = layer_get_bounds(root);

    s_shield_path = gpath_create(&SHIELD_PATH_INFO);
    s_drop_path   = gpath_create(&DROP_PATH_INFO);

    // Action bar (rectangular only) — icons replace the old text hint line.
#ifdef PBL_RECT
    s_action_bar  = action_bar_layer_create();
    s_icon_taken  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_TAKEN);
    s_icon_snooze = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_SNOOZE);
    s_icon_skip   = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_SKIP);
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_SELECT, s_icon_taken);
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP,     s_icon_snooze);
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_DOWN,   s_icon_skip);
    action_bar_layer_set_click_config_provider(s_action_bar, click_config_provider);
    action_bar_layer_add_to_window(s_action_bar, window);
    int content_w = bounds.size.w - ACTION_BAR_WIDTH;
#else
    window_set_click_config_provider(window, click_config_provider);
    int content_w = bounds.size.w;
#endif

#ifdef PBL_ROUND
    Layer *hints_layer = layer_create(bounds);
    layer_set_update_proc(hints_layer, hints_update_proc);
    layer_add_child(root, hints_layer);
#endif

    // Pill canvas — starts above screen; entry animation moves it into position.
    int pill_x = (content_w - PILL_CANVAS_SIZE) / 2;
    int pill_y = 12;
    s_canvas_target_frame = GRect(pill_x, pill_y, PILL_CANVAS_SIZE, PILL_CANVAS_SIZE);

    GRect init_frame      = s_canvas_target_frame;
    init_frame.origin.y   = -(PILL_CANVAS_SIZE);
    s_canvas_layer        = layer_create(init_frame);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(root, s_canvas_layer);

    // Text layers — larger fonts for readability.
    int text_y = pill_y + PILL_CANVAS_SIZE + 4;
    int text_x = PBL_IF_ROUND_ELSE(20, 4);
    int text_w = content_w - PBL_IF_ROUND_ELSE(40, 8);

    s_name_layer = text_layer_create(GRect(text_x, text_y, text_w, 30));
    text_layer_set_font(s_name_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_name_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_name_layer));

    s_taker_layer = text_layer_create(GRect(text_x, text_y + 30, text_w, 24));
    text_layer_set_font(s_taker_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_taker_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_taker_layer));

    s_dose_layer = text_layer_create(GRect(text_x, text_y + 54, text_w, 24));
    text_layer_set_font(s_dose_layer, fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS));
    text_layer_set_text_alignment(s_dose_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_dose_layer));

    s_time_layer = text_layer_create(GRect(text_x, text_y + 80, text_w, 24));
    text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_time_layer));

#ifdef PBL_ROUND
    text_layer_enable_screen_text_flow_and_paging(s_name_layer,  5);
    text_layer_enable_screen_text_flow_and_paging(s_taker_layer, 5);
    text_layer_enable_screen_text_flow_and_paging(s_dose_layer,  5);
    text_layer_enable_screen_text_flow_and_paging(s_time_layer,  5);
#endif

    update_text();
}

static void window_appear(Window *window) {
    if (s_entry_done) return;   // only play once per push
    s_entry_done = true;

    // Entry animation: pill drops in from above with an ease-out bounce feel.
    GRect from_frame    = s_canvas_target_frame;
    from_frame.origin.y = -(PILL_CANVAS_SIZE);
    s_prop_anim = property_animation_create_layer_frame(
        s_canvas_layer, &from_frame, &s_canvas_target_frame);
    Animation *base = property_animation_get_animation(s_prop_anim);
    animation_set_duration(base, 200);
    animation_set_curve(base, AnimationCurveEaseOut);
    animation_set_handlers(base, (AnimationHandlers){ .stopped = entry_anim_stopped }, NULL);
    animation_schedule(base);
}

static void window_unload(Window *window) {
    cancel_wobble();
    cancel_current_anim();

    // Dismissed without an explicit action → implicit snooze.
    if (!s_action_taken) {
        dose_log_record(s_med_index, DOSE_SNOOZED, (uint32_t)s_dose_time);
        notifications_schedule_snooze();
    }

#ifdef PBL_RECT
    if (s_icon_taken)  { gbitmap_destroy(s_icon_taken);  s_icon_taken  = NULL; }
    if (s_icon_snooze) { gbitmap_destroy(s_icon_snooze); s_icon_snooze = NULL; }
    if (s_icon_skip)   { gbitmap_destroy(s_icon_skip);   s_icon_skip   = NULL; }
    action_bar_layer_destroy(s_action_bar);
    s_action_bar = NULL;
#endif
#ifdef PBL_ROUND
    if (s_hints_layer) { layer_destroy(s_hints_layer); s_hints_layer = NULL; }
#endif
    layer_destroy(s_canvas_layer);
    text_layer_destroy(s_name_layer);
    text_layer_destroy(s_taker_layer);
    text_layer_destroy(s_dose_layer);
    text_layer_destroy(s_time_layer);

    if (s_shield_path) { gpath_destroy(s_shield_path); s_shield_path = NULL; }
    if (s_drop_path)   { gpath_destroy(s_drop_path);   s_drop_path   = NULL; }

    window_destroy(window);
    s_window = NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void detail_window_push(uint8_t med_index, time_t dose_time) {
    s_med_index    = med_index;
    s_dose_time    = dose_time;
    s_revealed     = false;
    s_action_taken = false;
    s_entry_done   = false;
    s_prop_anim    = NULL;
    s_wobble_timer = NULL;
    s_wobble_step  = 0;

    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .appear = window_appear,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);
}
