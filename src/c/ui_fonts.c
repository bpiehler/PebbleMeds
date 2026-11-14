#include "ui_fonts.h"

// Gothic point size per font role and scale.
static const uint8_t SIZES[UI_FONT_COUNT][UI_SCALE_COUNT] = {
    [UI_FONT_LIST_HEADER]  = {14, 18, 24},
    [UI_FONT_ROW_TITLE]    = {18, 24, 36},
    [UI_FONT_ROW_SUBTITLE] = {14, 24, 28},
    [UI_FONT_TITLE]        = {24, 28, 36},
    [UI_FONT_BODY_BOLD]    = {18, 24, 28},
    [UI_FONT_BODY]         = {18, 24, 28},
};

// Gothic 36 ships in newer firmware but has no SDK constant; load it by name.
#define KEY_GOTHIC_36      "RESOURCE_ID_GOTHIC_36"
#define KEY_GOTHIC_36_BOLD "RESOURCE_ID_GOTHIC_36_BOLD"
#define KEY_MISSING        "RESOURCE_ID_PEBBLEMEDS_NO_SUCH_FONT"

static bool s_have_36_checked;
static bool s_have_36;

static bool is_bold(UiFont font) {
    return font != UI_FONT_ROW_SUBTITLE && font != UI_FONT_BODY;
}

static UiScale clamp_scale(UiScale scale) {
    return (scale < UI_SCALE_COUNT) ? scale : UI_SCALE_NORMAL;
}

// Unknown keys return the firmware fallback font, so compare against it.
static bool have_gothic_36(void) {
    if (!s_have_36_checked) {
        s_have_36_checked = true;
        GFont fallback = fonts_get_system_font(KEY_MISSING);
        s_have_36 = fonts_get_system_font(KEY_GOTHIC_36_BOLD) != fallback &&
                    fonts_get_system_font(KEY_GOTHIC_36) != fallback;
    }
    return s_have_36;
}

static uint8_t point_size(UiFont font, UiScale scale) {
    uint8_t pt = SIZES[font][clamp_scale(scale)];
    return (pt == 36 && !have_gothic_36()) ? 28 : pt;
}

UiScale ui_scale_get(void) {
    switch (preferred_content_size()) {
        case PreferredContentSizeLarge:      return UI_SCALE_LARGE;
        case PreferredContentSizeExtraLarge: return UI_SCALE_XL;
        default:                             return UI_SCALE_NORMAL;
    }
}

GFont ui_font(UiFont font, UiScale scale) {
    bool b = is_bold(font);
    const char *key;
    switch (point_size(font, scale)) {
        case 14: key = b ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_14; break;
        case 18: key = b ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_18; break;
        case 24: key = b ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_24; break;
        case 28: key = b ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_28; break;
        default: key = b ? KEY_GOTHIC_36_BOLD      : KEY_GOTHIC_36;      break;
    }
    return fonts_get_system_font(key);
}

int16_t ui_font_line_h(UiFont font, UiScale scale) {
    switch (point_size(font, scale)) {
        case 14: return 18;
        case 18: return 22;
        case 24: return 30;
        case 28: return 34;
        default: return 42;
    }
}
