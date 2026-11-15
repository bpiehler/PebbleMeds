#include "dose_list_window.h"
#include "detail_window.h"
#include "med_list.h"
#include "appmessage.h"
#include "time_fmt.h"
#include <pebble.h>

// ---------------------------------------------------------------------------
// Dose row: one entry in the sorted list
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t med_index;
    time_t  dose_time;
} DoseRow;

static DoseRow  s_rows[MED_MAX];
static uint8_t  s_row_count = 0;
static bool     s_show_taker;  // only when more than one person takes meds
static bool     s_show_clock;     // clock times fit on every row
static bool     s_clock_checked;  // s_show_clock and the columns are current
#define SUBTITLE_LEN 64          // relative time and taker
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// ---------------------------------------------------------------------------
// Build and sort the dose row list
// ---------------------------------------------------------------------------

// skip_med_index / skip_after: when non-zero, the search base for that med is
// advanced to MAX(now, skip_after + 1) so a just-taken/skipped future dose
// rolls forward to the next occurrence rather than re-appearing in the list.
static void build_rows(uint8_t skip_med_index, time_t skip_after) {
    s_row_count = 0;
    time_t now = time(NULL);
    uint8_t count = med_list_count();

    for (uint8_t i = 0; i < count; i++) {
        MedEntry *med = med_list_get(i);
        if (!med) continue;
        time_t base = now;
        if (skip_after > 0 && i == skip_med_index && skip_after >= now) {
            base = skip_after + 1;
        }
        s_rows[s_row_count].med_index = i;
        s_rows[s_row_count].dose_time = med_list_next_dose_time(med, base);
        s_row_count++;
    }

    s_show_taker = false;
    s_clock_checked = false;
    MedEntry *first = (s_row_count > 0) ? med_list_get(s_rows[0].med_index) : NULL;
    for (uint8_t i = 1; first && i < s_row_count; i++) {
        MedEntry *m = med_list_get(s_rows[i].med_index);
        if (m && strcmp(m->taker, first->taker) != 0) { s_show_taker = true; break; }
    }

    // Insertion sort by dose_time (at most 16 elements)
    for (uint8_t i = 1; i < s_row_count; i++) {
        DoseRow key = s_rows[i];
        int j = (int)i - 1;
        while (j >= 0 && s_rows[j].dose_time > key.dose_time) {
            s_rows[j + 1] = s_rows[j];
            j--;
        }
        s_rows[j + 1] = key;
    }
}

// ---------------------------------------------------------------------------
// MenuLayer window state
// ---------------------------------------------------------------------------

static Window    *s_window;
static MenuLayer *s_menu_layer;
static TextLayer *s_empty_layer;

// ---------------------------------------------------------------------------
// MenuLayer callbacks
// ---------------------------------------------------------------------------

static uint16_t get_num_sections(MenuLayer *layer, void *ctx) {
    return 1;
}

static int16_t get_header_height(MenuLayer *layer, uint16_t section, void *ctx) {
#ifdef PBL_ROUND
    return 34;
#else
    return MENU_CELL_BASIC_HEADER_HEIGHT;
#endif
}

static void draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section, void *ctx_data) {
#ifdef PBL_ROUND
    GRect bounds = layer_get_bounds(cell_layer);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "Upcoming Doses", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(0, 0, bounds.size.w, 34), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
#else
    menu_cell_basic_header_draw(ctx, cell_layer, "Upcoming Doses");
#endif
}

static uint16_t get_num_rows(MenuLayer *layer, uint16_t section, void *ctx) {
    return (s_row_count > 0) ? s_row_count : 1;
}

static int16_t get_cell_height(MenuLayer *layer, MenuIndex *index, void *ctx) {
    return 44;
}

static void draw_pill_mini(GContext *ctx, GRect bounds, PillShape shape, GColor color, bool highlighted) {
    GPoint center = grect_center_point(&bounds);
    int r = 8; // Small radius for menu icon

#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, color);
    graphics_context_set_stroke_color(ctx, GColorBlack);
#else
    GColor c = highlighted ? GColorWhite : GColorBlack;
    graphics_context_set_fill_color(ctx, c);
    graphics_context_set_stroke_color(ctx, c);
#endif
    graphics_context_set_stroke_width(ctx, 1);

    switch (shape) {
        case SHAPE_ROUND:
            graphics_fill_circle(ctx, center, r);
            graphics_draw_circle(ctx, center, r);
            break;
        case SHAPE_OVAL: {
            GRect rect = GRect(center.x - 6, center.y - 8, 12, 16);
            graphics_fill_rect(ctx, rect, 6, GCornersAll);
            graphics_draw_round_rect(ctx, rect, 6);
            break;
        }
        case SHAPE_OBLONG: {
            GRect rect = GRect(center.x - 8, center.y - 5, 16, 10);
            graphics_fill_rect(ctx, rect, 5, GCornersAll);
            graphics_draw_round_rect(ctx, rect, 5);
            break;
        }
        case SHAPE_SHIELD: {
            // Simple diamond/shield for mini view
            GPathInfo path_info = {
                .num_points = 4,
                .points = (GPoint[]){{0, -r}, {r-2, 0}, {0, r}, {-(r-2), 0}}
            };
            GPath *path = gpath_create(&path_info);
            gpath_move_to(path, center);
            gpath_draw_filled(ctx, path);
            gpath_draw_outline(ctx, path);
            gpath_destroy(path);
            break;
        }
        case SHAPE_DROP: {
            // Simple triangle/drop for mini view
            GPathInfo path_info = {
                .num_points = 3,
                .points = (GPoint[]){{0, -r}, {r-2, r}, {-(r-2), r}}
            };
            GPath *path = gpath_create(&path_info);
            gpath_move_to(path, center);
            gpath_draw_filled(ctx, path);
            gpath_draw_outline(ctx, path);
            gpath_destroy(path);
            break;
        }
    }
}

// Subtitle columns, as x offsets into the text area, so the dots and the
// colons of the clock times line up from row to row.
static int16_t s_clock_dot_x, s_colon_x, s_taker_dot_x, s_dot_w, s_gap;

static int text_width(const char *text, GFont font) {
    return graphics_text_layout_get_content_size(
        text, font, GRect(0, 0, 1000, 100), GTextOverflowModeFill, GTextAlignmentLeft).w;
}

// Beyond a day the relative text is already a day and time.
static bool row_has_clock(time_t dose_time) {
    int diff = (int)(dose_time - time(NULL));
    return diff < SECONDS_PER_DAY && diff > -SECONDS_PER_DAY;
}

// Width of the hour part of a clock time, before the colon.
static int hour_width(char *clock, GFont font) {
    char *colon = strchr(clock, ':');
    if (!colon) return 0;
    *colon = '\0';
    int w = text_width(clock, font);
    *colon = ':';
    return w;
}

// "In 25 min · Murray", for rows drawn without the clock column.
static void format_subtitle(time_t dose_time, const MedEntry *med, char *buf, size_t len) {
    char when[24];
    time_fmt_relative(dose_time, when, sizeof(when));
    if (s_show_taker) snprintf(buf, len, "%s \xc2\xb7 %s", when, med->taker);
    else              snprintf(buf, len, "%s", when);
}

// Lay out "In 25 min · 10:00 AM · Murray" in columns. Show clock times only
// if every row has room, so the rows stay alike.
static void layout_subtitles(GFont font, int width) {
    s_clock_checked = true;
    s_show_clock = false;
    int when_w = 0, hour_w = 0, rest_w = 0, taker_w = 0;
    bool any = false;
    char when[24], clock[12];
    for (uint8_t i = 0; i < s_row_count; i++) {
        MedEntry *med = med_list_get(s_rows[i].med_index);
        time_t t = s_rows[i].dose_time;
        if (!med || !row_has_clock(t)) continue;
        any = true;
        time_fmt_relative(t, when, sizeof(when));
        when_w = MAX(when_w, text_width(when, font));
        time_fmt_clock(t, clock, sizeof(clock));
        int hw = hour_width(clock, font);
        hour_w = MAX(hour_w, hw);
        rest_w = MAX(rest_w, text_width(clock, font) - hw);
        if (s_show_taker) taker_w = MAX(taker_w, text_width(med->taker, font));
    }
    if (!any) return;

    s_dot_w = text_width("\xc2\xb7", font);
    s_gap = text_width("x x", font) - text_width("xx", font);
    s_clock_dot_x = when_w + s_gap;
    s_colon_x = s_clock_dot_x + s_dot_w + s_gap + hour_w;
    s_taker_dot_x = s_colon_x + rest_w + s_gap;
    int total = s_show_taker ? s_taker_dot_x + s_dot_w + s_gap + taker_w : s_colon_x + rest_w;
    s_show_clock = total <= width;
}

// Width the subtitle may fill. On round screens the rows next to the
// focused one are cut by the screen edge, so fit to the row below it.
static int subtitle_fit_w(GRect box, int cell_h) {
#ifdef PBL_ROUND
    int r = PBL_DISPLAY_WIDTH / 2;
    int d = cell_h / 2 + box.origin.y + box.size.h;  // from screen center
    int half = 0;
    while ((half + 1) * (half + 1) <= r * r - d * d) half++;
    return MIN(box.size.w, r + half - box.origin.x - 4);
#else
    return box.size.w;
#endif
}

// Draws text at x within the subtitle box, w wide.
static void draw_sub(GContext *ctx, const char *text, GFont font, GRect box, int x, int w) {
    graphics_draw_text(ctx, text, font, GRect(box.origin.x + x, box.origin.y, w, box.size.h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void draw_subtitle(GContext *ctx, const DoseRow *row, const MedEntry *med,
                          GFont font, GRect box) {
    int w = box.size.w;
    if (!s_show_clock || !row_has_clock(row->dose_time)) {
        char subtitle[SUBTITLE_LEN];
        format_subtitle(row->dose_time, med, subtitle, sizeof(subtitle));
        draw_sub(ctx, subtitle, font, box, 0, w);
        return;
    }
    char when[24], clock[12];
    time_fmt_relative(row->dose_time, when, sizeof(when));
    time_fmt_clock(row->dose_time, clock, sizeof(clock));
    int clock_x = s_colon_x - hour_width(clock, font);
    draw_sub(ctx, when, font, box, 0, s_clock_dot_x);
    draw_sub(ctx, "\xc2\xb7", font, box, s_clock_dot_x, s_dot_w + s_gap);
    draw_sub(ctx, clock, font, box, clock_x, w - clock_x);
    if (s_show_taker) {
        int taker_x = s_taker_dot_x + s_dot_w + s_gap;
        draw_sub(ctx, "\xc2\xb7", font, box, s_taker_dot_x, s_dot_w + s_gap);
        draw_sub(ctx, med->taker, font, box, taker_x, w - taker_x);
    }
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *index, void *ctx_data) {
    GRect bounds = layer_get_bounds(cell_layer);

    if (s_row_count == 0) {
        menu_cell_basic_draw(ctx, cell_layer, "No Medications", "Set up on phone", NULL);
        return;
    }

    DoseRow  *row = &s_rows[index->row];
    MedEntry *med = med_list_get(row->med_index);
    if (!med) return;

    AppSettings *settings = med_list_get_settings();
    bool highlighted = menu_cell_layer_is_highlighted(cell_layer);

    // Padding optimization for Round screen
    int pill_x = PBL_IF_ROUND_ELSE(18, 6);
    int text_x = pill_x + 26;
    int text_w = bounds.size.w - text_x - PBL_IF_ROUND_ELSE(18, 4);

    // Pill icon area
    GRect pill_bounds = GRect(pill_x, (bounds.size.h - 20) / 2, 20, 20);
    draw_pill_mini(ctx, pill_bounds, med->shape, med->color, highlighted);

    // Title: what. Subtitle: when, and for whom if more than one person.
    const char *title = settings->privacyMode ? "Medication" : med->name;
    GFont sub_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    GRect sub_box = GRect(text_x, 22, text_w, 16);
    if (!s_clock_checked) layout_subtitles(sub_font, subtitle_fit_w(sub_box, bounds.size.h));

    graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
    graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(text_x, 2, text_w, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    draw_subtitle(ctx, row, med, sub_font, sub_box);
}

static void select_click(MenuLayer *layer, MenuIndex *index, void *ctx) {
    if (s_row_count == 0) return;
    detail_window_push(s_rows[index->row].med_index, s_rows[index->row].dose_time, DETAIL_MODE_BROWSE);
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);

    s_menu_layer = menu_layer_create(bounds);
    menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
        .get_num_sections = get_num_sections,
        .get_header_height = get_header_height,
        .draw_header      = draw_header,
        .get_num_rows     = get_num_rows,
        .get_cell_height  = get_cell_height,
        .draw_row         = draw_row,
        .select_click     = select_click,
    });
    menu_layer_set_click_config_onto_window(s_menu_layer, window);

#ifdef PBL_COLOR
    menu_layer_set_normal_colors(s_menu_layer,    GColorWhite,      GColorBlack);
    menu_layer_set_highlight_colors(s_menu_layer, GColorCobaltBlue, GColorWhite);
#endif

    build_rows(0xFF, 0);
    layer_add_child(root, menu_layer_get_layer(s_menu_layer));

    // Empty state
    s_empty_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 30, bounds.size.w, 60));
    text_layer_set_text(s_empty_layer, "No meds scheduled.\nUse the phone app!");
    text_layer_set_font(s_empty_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_empty_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_empty_layer));

    if (s_row_count > 0) {
        layer_set_hidden(text_layer_get_layer(s_empty_layer), true);
    } else {
        layer_set_hidden(menu_layer_get_layer(s_menu_layer), true);
    }
}

static void window_unload(Window *window) {
    menu_layer_destroy(s_menu_layer);
    s_menu_layer = NULL;
    text_layer_destroy(s_empty_layer);
    s_empty_layer = NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

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

static void apply_refresh(uint8_t skip_med_index, time_t skip_after) {
    build_rows(skip_med_index, skip_after);
    if (s_menu_layer) {
        menu_layer_reload_data(s_menu_layer);
        if (s_empty_layer) {
            bool empty = (s_row_count == 0);
            layer_set_hidden(menu_layer_get_layer(s_menu_layer), empty);
            layer_set_hidden(text_layer_get_layer(s_empty_layer), !empty);
        }
    }
}

void dose_list_window_refresh(void) {
    apply_refresh(0xFF, 0);
}

void dose_list_window_refresh_after(uint8_t med_index, time_t dose_time) {
    apply_refresh(med_index, dose_time);
}

// Called every minute: keep "In 25 min" current and drop doses that have passed.
void dose_list_window_tick(void) {
    if (!s_menu_layer) return;
    time_t now = time(NULL);
    for (uint8_t i = 0; i < s_row_count; i++) {
        if (s_rows[i].dose_time < now - 60) {
            dose_list_window_refresh();
            return;
        }
    }
    s_clock_checked = false;  // relative times change length
    layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
}
