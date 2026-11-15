#pragma once
#include <pebble.h>

// Text scale derived from the system "Text Size" setting.
// NORMAL matches the original fixed layout; Emery/Gabbro default to LARGE.
typedef enum {
    UI_SCALE_NORMAL = 0,  // Small / Medium
    UI_SCALE_LARGE,       // Large (PT2 default)
    UI_SCALE_XL,          // Extra Large
    UI_SCALE_COUNT
} UiScale;

typedef enum {
    UI_FONT_LIST_HEADER,   // Gothic 14B / 18B / 24B ("Upcoming Doses")
    UI_FONT_ROW_TITLE,     // Gothic 18B / 24B / 36B (list row name)
    UI_FONT_ROW_SUBTITLE,  // Gothic 14 / 24 / 28 (list row details)
    UI_FONT_TITLE,         // Gothic 24B / 28B / 36B (detail name)
    UI_FONT_BODY_BOLD,     // Gothic 18B / 24B / 28B
    UI_FONT_BODY,          // Gothic 18 / 24 / 28
    UI_FONT_COUNT
} UiFont;

UiScale ui_scale_get(void);
GFont   ui_font(UiFont font, UiScale scale);
int16_t ui_font_line_h(UiFont font, UiScale scale);  // box height for one line
