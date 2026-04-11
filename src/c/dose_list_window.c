#include "dose_list_window.h"
#include "detail_window.h"
#include "med_list.h"
#include "appmessage.h"
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

// ---------------------------------------------------------------------------
// Build and sort the dose row list
// ---------------------------------------------------------------------------

static void build_rows(void) {
    s_row_count = 0;
    time_t now = time(NULL);
    uint8_t count = med_list_count();

    for (uint8_t i = 0; i < count; i++) {
        MedEntry *med = med_list_get(i);
        if (!med) continue;
        s_rows[s_row_count].med_index = i;
        s_rows[s_row_count].dose_time = med_list_next_dose_time(med, now);
        s_row_count++;
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
// Time formatting
// ---------------------------------------------------------------------------

static void format_dose_time(time_t ts, char *buf, size_t buflen) {
    time_t now = time(NULL);
    struct tm t_now = *localtime(&now);
    struct tm t     = *localtime(&ts);

    bool is_today = (t.tm_yday == t_now.tm_yday && t.tm_year == t_now.tm_year);

    if (clock_is_24h_style()) {
        if (is_today) {
            snprintf(buf, buflen, "%02d:%02d", t.tm_hour, t.tm_min);
        } else {
            snprintf(buf, buflen, "Tmrw %02d:%02d", t.tm_hour, t.tm_min);
        }
    } else {
        int h = t.tm_hour % 12;
        if (h == 0) h = 12;
        const char *ampm = (t.tm_hour >= 12) ? "p" : "a";
        if (is_today) {
            snprintf(buf, buflen, "%d:%02d%s", h, t.tm_min, ampm);
        } else {
            snprintf(buf, buflen, "Tmrw %d:%02d%s", h, t.tm_min, ampm);
        }
    }
}

// ---------------------------------------------------------------------------
// MenuLayer window state
// ---------------------------------------------------------------------------

static Window    *s_window;
static MenuLayer *s_menu_layer;

// ---------------------------------------------------------------------------
// MenuLayer callbacks
// ---------------------------------------------------------------------------

static uint16_t get_num_sections(MenuLayer *layer, void *ctx) {
    return 1;
}

static int16_t get_header_height(MenuLayer *layer, uint16_t section, void *ctx) {
    return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section, void *ctx_data) {
    menu_cell_basic_header_draw(ctx, cell_layer, "Upcoming Doses");
}

static uint16_t get_num_rows(MenuLayer *layer, uint16_t section, void *ctx) {
    return (s_row_count > 0) ? s_row_count : 1;
}

static int16_t get_cell_height(MenuLayer *layer, MenuIndex *index, void *ctx) {
    return 44;
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *index, void *ctx_data) {
    if (s_row_count == 0) {
        menu_cell_basic_draw(ctx, cell_layer, "No Medications", "Configure via phone app", NULL);
        return;
    }

    DoseRow  *row = &s_rows[index->row];
    MedEntry *med = med_list_get(row->med_index);
    if (!med) return;

    AppSettings *settings = med_list_get_settings();
    char time_str[16];
    format_dose_time(row->dose_time, time_str, sizeof(time_str));

    // Title: "HH:MM  Med Name" or privacy-mode placeholder
    char title[48];
    if (settings->privacyMode) {
        snprintf(title, sizeof(title), "%s  Medication Due", time_str);
    } else {
        snprintf(title, sizeof(title), "%s  %s", time_str, med->name);
    }

    // Subtitle: "Taker  Dose" (or just "Taker" in privacy mode)
    char subtitle[48];
    if (!settings->privacyMode && med->dose[0] != '\0') {
        snprintf(subtitle, sizeof(subtitle), "%s \xc2\xb7 %s", med->taker, med->dose);
    } else {
        snprintf(subtitle, sizeof(subtitle), "%s", med->taker);
    }

    menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

static void select_click(MenuLayer *layer, MenuIndex *index, void *ctx) {
    if (s_row_count == 0) return;
    detail_window_push(s_rows[index->row].med_index, s_rows[index->row].dose_time);
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
    menu_layer_set_normal_colors(s_menu_layer,    GColorBlack,      GColorWhite);
    menu_layer_set_highlight_colors(s_menu_layer, GColorCobaltBlue, GColorWhite);
#endif

    build_rows();
    layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
    menu_layer_destroy(s_menu_layer);
    s_menu_layer = NULL;
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

void dose_list_window_refresh(void) {
    // Rebuild the sorted row list and tell the MenuLayer to redraw.
    // Safe to call even if the window is not currently visible.
    build_rows();
    if (s_menu_layer) {
        menu_layer_reload_data(s_menu_layer);
    }
}
