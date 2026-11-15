#include "detail_window.h"
#include "med_list.h"
#include "appmessage.h"
#include "notifications.h"
#include "dose_log.h"
#include "time_fmt.h"
#include "confirm_dialog.h"
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
// Window state
// ---------------------------------------------------------------------------

static Window            *s_window;
static Layer             *s_canvas_layer;
static TextLayer         *s_name_layer;
static TextLayer         *s_taker_layer;
static TextLayer         *s_dose_layer;
static TextLayer         *s_time_layer;   // "Due: HH:MM [AM/PM]"
static ActionBarLayer    *s_action_bar;
static GBitmap           *s_icon_check;
static GBitmap           *s_icon_x;
static GBitmap           *s_icon_snooze;

static GPath             *s_shield_path;
static GPath             *s_drop_path;
static PropertyAnimation *s_prop_anim;

static uint8_t            s_med_index;
static time_t             s_dose_time;
static DetailWindowMode   s_mode;
static bool               s_revealed;
static bool               s_action_taken;
static bool               s_entry_done;     // prevents entry anim replaying
static GRect              s_canvas_target_frame;

// Forward declarations: avoids circular include with dose_list_window.h
void dose_list_window_refresh_after(uint8_t med_index, time_t dose_time);

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

static bool details_hidden(void) {
    return med_list_get_settings()->privacyMode && !s_revealed;
}

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
    graphics_context_set_fill_color(ctx, fill);
    graphics_context_set_stroke_color(ctx, GColorBlack);
#else
    graphics_context_set_stroke_color(ctx, GColorBlack);
#endif
    graphics_context_set_stroke_width(ctx, 2);

    switch (med->shape) {

        case SHAPE_ROUND:
#ifdef PBL_COLOR
            graphics_fill_circle(ctx, center, r);
#endif
            graphics_draw_circle(ctx, center, r);
            break;

        case SHAPE_OVAL: {
            int hw = r * 7 / 10;
            GRect rect = GRect(center.x - hw, center.y - r, hw * 2, r * 2);
#ifdef PBL_COLOR
            graphics_fill_rect(ctx, rect, hw, GCornersAll);
#endif
            graphics_draw_round_rect(ctx, rect, hw);
            break;
        }

        case SHAPE_OBLONG: {
            int hh = r / 2;
            GRect rect = GRect(center.x - r, center.y - hh, r * 2, hh * 2);
#ifdef PBL_COLOR
            graphics_fill_rect(ctx, rect, hh, GCornersAll);
#endif
            graphics_draw_round_rect(ctx, rect, hh);
            break;
        }

        case SHAPE_SHIELD:
            if (s_shield_path) {
                gpath_move_to(s_shield_path, center);
#ifdef PBL_COLOR
                gpath_draw_filled(ctx, s_shield_path);
#endif
                gpath_draw_outline(ctx, s_shield_path);
            }
            break;

        case SHAPE_DROP:
            if (s_drop_path) {
                gpath_move_to(s_drop_path, center);
#ifdef PBL_COLOR
                gpath_draw_filled(ctx, s_drop_path);
#endif
                gpath_draw_outline(ctx, s_drop_path);
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// Entry animation: pill drops in from above
// ---------------------------------------------------------------------------

// Clears s_prop_anim before unscheduling so the stopped handler exits early.
static void cancel_current_anim(void) {
    if (!s_prop_anim) return;
    PropertyAnimation *pa = s_prop_anim;
    s_prop_anim = NULL;
    animation_unschedule(property_animation_get_animation(pa));
    property_animation_destroy(pa);
}

static void entry_anim_stopped(Animation *anim, bool finished, void *ctx) {
    if (!s_prop_anim) return;
    PropertyAnimation *pa = s_prop_anim;
    s_prop_anim = NULL;
    property_animation_destroy(pa);
    layer_set_frame(s_canvas_layer, s_canvas_target_frame);
}

// ---------------------------------------------------------------------------
// Actions: like the system alarm, close the reminder and confirm the result
// ---------------------------------------------------------------------------

static void finish(const char *confirmation) {
    s_action_taken = true;
    notifications_schedule_wakeups();
    if (s_mode == DETAIL_MODE_BROWSE) {
        dose_list_window_refresh_after(s_med_index, s_dose_time);
    }
    confirm_dialog_push(confirmation);
    window_stack_remove(s_window, false);
}

static void do_taken(void) {
    dose_log_record(s_med_index, DOSE_TAKEN, (uint32_t)s_dose_time);
    appmessage_send_action(s_med_index, "taken", (uint32_t)s_dose_time);

    // Update lastTakenTs on the watch immediately so interval scheduling uses
    // the actual taken time rather than waiting for the phone to sync it back.
    MedEntry *med = med_list_get(s_med_index);
    if (med && med->scheduleType == SCHEDULE_INTERVAL) {
        med->lastTakenTs = (uint32_t)time(NULL);
    }
    finish("Taken");
}

static void do_skip(void) {
    dose_log_record(s_med_index, DOSE_SKIPPED, (uint32_t)s_dose_time);
    appmessage_send_action(s_med_index, "skipped", (uint32_t)s_dose_time);
    finish("Skipped");
}

static void do_snooze(uint16_t mins) {
    dose_log_record(s_med_index, DOSE_SNOOZED, (uint32_t)s_dose_time);
    appmessage_send_action(s_med_index, "snooze", (uint32_t)s_dose_time);
    notifications_schedule_snooze_for(s_med_index, s_dose_time, mins);

    char clock[12], text[40];
    time_fmt_clock(time(NULL) + (time_t)mins * 60, clock, sizeof(clock));
    // Time goes on its own line so it never splits
    snprintf(text, sizeof(text), "Snoozed until\n%s", clock);
    finish(text);
}

// ---------------------------------------------------------------------------
// Buttons: Up = Snooze, Select = Taken, Down = Skip, Back = Snooze (alerts)
// ---------------------------------------------------------------------------

static void up_click(ClickRecognizerRef rec, void *ctx) {
    if (s_action_taken) return;
    do_snooze(notifications_default_snooze_mins());
}

static void select_click(ClickRecognizerRef rec, void *ctx) {
    if (s_action_taken) return;
    // Privacy mode: the first press reveals the medication.
    if (details_hidden()) {
        s_revealed = true;
        update_text();
        layer_mark_dirty(s_canvas_layer);
        return;
    }
    do_taken();
}

static void down_click(ClickRecognizerRef rec, void *ctx) {
    if (s_action_taken) return;
    do_skip();
}

static void back_click(ClickRecognizerRef rec, void *ctx) {
    if (s_action_taken) return;
    do_snooze(notifications_default_snooze_mins());
}

static void click_config_provider(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
    // Snooze only applies to alerts; from the list, Back just closes.
    if (s_mode == DETAIL_MODE_ALERT) {
        window_single_click_subscribe(BUTTON_ID_UP, up_click);
        window_single_click_subscribe(BUTTON_ID_BACK, back_click);
    }
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
    Layer *root   = window_get_root_layer(window);
    GRect  bounds = layer_get_bounds(root);

    s_shield_path = gpath_create(&SHIELD_PATH_INFO);
    s_drop_path   = gpath_create(&DROP_PATH_INFO);

    // System action bar icons, as on the alarm screen.
    s_icon_check  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ACTION_CHECK);
    s_icon_x      = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ACTION_X);
    s_icon_snooze = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ACTION_SNOOZE);
    s_action_bar  = action_bar_layer_create();
    if (s_mode == DETAIL_MODE_ALERT) {
        action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP, s_icon_snooze);
    }
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_SELECT, s_icon_check);
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_DOWN, s_icon_x);
    action_bar_layer_set_click_config_provider(s_action_bar, click_config_provider);
    action_bar_layer_add_to_window(s_action_bar, window);

#ifdef PBL_RECT
    int content_x = 0;
    int content_w = bounds.size.w - ACTION_BAR_WIDTH;
    int text_inset = 4;
#else
    int content_x = 12;
    int content_w = bounds.size.w - ACTION_BAR_WIDTH - content_x;
    int text_inset = 8;
#endif
    int text_x = content_x + text_inset;
    int text_w = content_w - text_inset * 2;

    int pill_x = content_x + (content_w - PILL_CANVAS_SIZE) / 2;
    // Round screens narrow at the bottom, so start higher and pack the lines.
    int pill_y = PBL_IF_ROUND_ELSE(4, 12);
    int text_y = pill_y + PILL_CANVAS_SIZE + 4;

    // Pill canvas starts above screen; entry animation moves it into position.
    s_canvas_target_frame = GRect(pill_x, pill_y, PILL_CANVAS_SIZE, PILL_CANVAS_SIZE);
    GRect init_frame      = s_canvas_target_frame;
    init_frame.origin.y   = -(PILL_CANVAS_SIZE);
    s_canvas_layer        = layer_create(init_frame);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(root, s_canvas_layer);

    struct { TextLayer **layer; const char *font; int y; int h; } rows[] = {
        { &s_name_layer,  FONT_KEY_GOTHIC_24_BOLD, 0,                       30 },
        { &s_taker_layer, FONT_KEY_GOTHIC_18_BOLD, PBL_IF_ROUND_ELSE(28, 30), 24 },
        { &s_dose_layer,  FONT_KEY_GOTHIC_18_BOLD, PBL_IF_ROUND_ELSE(50, 54), 24 },
        { &s_time_layer,  FONT_KEY_GOTHIC_18,      PBL_IF_ROUND_ELSE(72, 80), 24 },
    };
    for (unsigned i = 0; i < ARRAY_LENGTH(rows); i++) {
        TextLayer *tl = text_layer_create(GRect(text_x, text_y + rows[i].y, text_w, rows[i].h));
        text_layer_set_font(tl, fonts_get_system_font(rows[i].font));
        text_layer_set_text_alignment(tl, GTextAlignmentCenter);
        text_layer_set_background_color(tl, GColorClear);
        layer_add_child(root, text_layer_get_layer(tl));
        *rows[i].layer = tl;
    }

    update_text();
}

static void window_appear(Window *window) {
    if (s_entry_done) return;   // only once per push
    s_entry_done = true;

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
    cancel_current_anim();

    // Alert closed without an action (app exit, another app): implicit snooze.
    if (s_mode == DETAIL_MODE_ALERT && !s_action_taken) {
        dose_log_record(s_med_index, DOSE_SNOOZED, (uint32_t)s_dose_time);
        notifications_schedule_snooze(s_med_index, s_dose_time);
    }

    action_bar_layer_destroy(s_action_bar);
    s_action_bar = NULL;
    gbitmap_destroy(s_icon_check);  s_icon_check  = NULL;
    gbitmap_destroy(s_icon_x);      s_icon_x      = NULL;
    gbitmap_destroy(s_icon_snooze); s_icon_snooze = NULL;

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

void detail_window_push(uint8_t med_index, time_t dose_time, DetailWindowMode mode) {
    s_med_index    = med_index;
    s_dose_time    = dose_time;
    s_mode         = mode;
    s_revealed     = false;
    s_action_taken = false;
    s_entry_done   = false;
    s_prop_anim    = NULL;

    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .appear = window_appear,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);
}
