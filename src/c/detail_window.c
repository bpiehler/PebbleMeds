#include "detail_window.h"
#include "med_list.h"
#include "appmessage.h"
#include "notifications.h"
#include <pebble.h>

// ---------------------------------------------------------------------------
// Pill geometry
// ---------------------------------------------------------------------------

#ifdef PBL_ROUND
  #define PILL_R 28
#else
  #define PILL_R 20
#endif
#define PILL_CANVAS_SIZE (PILL_R * 2 + 4)

// GPath shapes: points defined around (0,0), gpath_move_to translates to center
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

static GPoint s_drop_points[] = {
    {0,            -PILL_R},
    {PILL_R*7/10,  -PILL_R*2/5},
    {PILL_R*9/10,   PILL_R/4},
    {PILL_R*6/10,   PILL_R},
    {-PILL_R*6/10,  PILL_R},
    {-PILL_R*9/10,  PILL_R/4},
    {-PILL_R*7/10, -PILL_R*2/5}
};
static const GPathInfo DROP_PATH_INFO = {
    .num_points = 7,
    .points = s_drop_points
};

// ---------------------------------------------------------------------------
// Window state
// ---------------------------------------------------------------------------

static Window           *s_window;
static Layer            *s_canvas_layer;
static TextLayer        *s_name_layer;
static TextLayer        *s_taker_layer;
static TextLayer        *s_dose_layer;
static TextLayer        *s_hint_layer;
#ifdef PBL_RECT
static ActionBarLayer   *s_action_bar;
#endif

static GPath            *s_shield_path;
static GPath            *s_drop_path;
static PropertyAnimation *s_prop_anim;

static uint8_t           s_med_index;
static time_t            s_dose_time;
static bool              s_revealed;
static bool              s_action_taken;

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

static char s_name_buf[36];
static char s_taker_buf[28];
static char s_dose_buf[28];

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

    text_layer_set_text(s_name_layer,  s_name_buf);
    text_layer_set_text(s_taker_layer, s_taker_buf);
    text_layer_set_text(s_dose_layer,  s_dose_buf);
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
    // Monochrome: draw black bg so white pill is visible
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
// Animation
// ---------------------------------------------------------------------------

static void anim_stopped(Animation *anim, bool finished, void *ctx) {
    PropertyAnimation *pa = s_prop_anim;
    s_prop_anim = NULL;
    property_animation_destroy(pa);
    notifications_schedule_wakeups();
    window_stack_pop(false);
}

// ---------------------------------------------------------------------------
// Click handlers
// ---------------------------------------------------------------------------

static void select_click(ClickRecognizerRef rec, void *ctx) {
    AppSettings *settings = med_list_get_settings();

    // In privacy mode, first press reveals; second press confirms Taken
    if (settings->privacyMode && !s_revealed) {
        s_revealed = true;
        update_text();
        layer_mark_dirty(s_canvas_layer);
        return;
    }

    s_action_taken = true;
    appmessage_send_action(s_med_index, "taken", (uint32_t)s_dose_time);
    vibes_short_pulse();

    GRect to = layer_get_frame(s_canvas_layer);
    to.origin.y = 200;  // slide off bottom
    s_prop_anim = property_animation_create_layer_frame(s_canvas_layer, NULL, &to);
    Animation *base = property_animation_get_animation(s_prop_anim);
    animation_set_duration(base, 300);
    animation_set_curve(base, AnimationCurveEaseIn);
    animation_set_handlers(base, (AnimationHandlers){ .stopped = anim_stopped }, NULL);
    animation_schedule(base);
}

static void up_click(ClickRecognizerRef rec, void *ctx) {
    s_action_taken = true;
    appmessage_send_action(s_med_index, "snooze", (uint32_t)s_dose_time);
    notifications_schedule_snooze();
    window_stack_pop(true);
}

static void down_click(ClickRecognizerRef rec, void *ctx) {
    s_action_taken = true;
    appmessage_send_action(s_med_index, "skipped", (uint32_t)s_dose_time);
    notifications_schedule_wakeups();
    window_stack_pop(true);
}

static void click_config_provider(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
    window_single_click_subscribe(BUTTON_ID_UP,     up_click);
    window_single_click_subscribe(BUTTON_ID_DOWN,   down_click);
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
    Layer *root   = window_get_root_layer(window);
    GRect  bounds = layer_get_bounds(root);

    // Create GPath objects for shield and drop shapes
    s_shield_path = gpath_create(&SHIELD_PATH_INFO);
    s_drop_path   = gpath_create(&DROP_PATH_INFO);

    // Action bar (rectangular only)
#ifdef PBL_RECT
    s_action_bar = action_bar_layer_create();
    action_bar_layer_set_click_config_provider(s_action_bar, click_config_provider);
    action_bar_layer_add_to_window(s_action_bar, window);
    int content_w = bounds.size.w - ACTION_BAR_WIDTH;
#else
    window_set_click_config_provider(window, click_config_provider);
    int content_w = bounds.size.w;
#endif

    // Pill canvas layer — centered horizontally, top portion of screen
    int pill_x = (content_w - PILL_CANVAS_SIZE) / 2;
    int pill_y = 10;
    s_canvas_layer = layer_create(GRect(pill_x, pill_y, PILL_CANVAS_SIZE, PILL_CANVAS_SIZE));
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(root, s_canvas_layer);

    // Text layers below pill
    int text_y = pill_y + PILL_CANVAS_SIZE + 8;
    int text_x = 4;
    int text_w = content_w - 8;

    s_name_layer = text_layer_create(GRect(text_x, text_y, text_w, 22));
    text_layer_set_font(s_name_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_name_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_name_layer));

    s_taker_layer = text_layer_create(GRect(text_x, text_y + 24, text_w, 18));
    text_layer_set_font(s_taker_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_taker_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_taker_layer));

    s_dose_layer = text_layer_create(GRect(text_x, text_y + 44, text_w, 18));
    text_layer_set_font(s_dose_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(s_dose_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_dose_layer));

    // Round display: enable text flow AFTER layer_add_child
#ifdef PBL_ROUND
    text_layer_enable_screen_text_flow_and_paging(s_name_layer,  5);
    text_layer_enable_screen_text_flow_and_paging(s_taker_layer, 5);
    text_layer_enable_screen_text_flow_and_paging(s_dose_layer,  5);
#endif

    // Button hint line at the bottom — "^Snooze  Taken  vSkip"
    // ^ = Up button, middle = Select, v = Down
    s_hint_layer = text_layer_create(GRect(0, bounds.size.h - 16, content_w, 16));
    text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
    text_layer_set_text(s_hint_layer, "^Snooze  Taken  vSkip");
    layer_add_child(root, text_layer_get_layer(s_hint_layer));

    update_text();
}

static void window_unload(Window *window) {
    // Clean up any in-progress animation
    if (s_prop_anim) {
        PropertyAnimation *pa = s_prop_anim;
        s_prop_anim = NULL;
        property_animation_destroy(pa);
    }

    // If dismissed without explicit action, treat as snooze
    if (!s_action_taken) {
        notifications_schedule_snooze();
    }

    // Destroy layers
#ifdef PBL_RECT
    action_bar_layer_destroy(s_action_bar);
    s_action_bar = NULL;
#endif
    layer_destroy(s_canvas_layer);
    text_layer_destroy(s_name_layer);
    text_layer_destroy(s_taker_layer);
    text_layer_destroy(s_dose_layer);
    text_layer_destroy(s_hint_layer);

    // Destroy GPath objects
    if (s_shield_path) { gpath_destroy(s_shield_path); s_shield_path = NULL; }
    if (s_drop_path)   { gpath_destroy(s_drop_path);   s_drop_path   = NULL; }

    window_destroy(window);
    s_window = NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void detail_window_push(uint8_t med_index, time_t dose_time) {
    s_med_index   = med_index;
    s_dose_time   = dose_time;
    s_revealed    = false;
    s_action_taken = false;
    s_prop_anim   = NULL;

    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);
}
