#include "glance.h"
#include "med_list.h"
#include <pebble.h>

// Aplite has no app glances.
#if !defined(PBL_PLATFORM_APLITE)

#define GLANCE_SLICES 8

// Earliest dose after `after` across all meds; 0 if none.
static time_t next_dose(time_t after, uint8_t *med_out) {
    time_t best = 0;
    for (uint8_t i = 0; i < med_list_count(); i++) {
        MedEntry *med = med_list_get(i);
        if (!med) continue;
        time_t t = med_list_next_dose_time(med, after);
        if (t > after && (best == 0 || t < best)) {
            best = t;
            *med_out = i;
        }
    }
    return best;
}

// "8:00 AM Multivitamin" today, "Sat, 8:00 AM Multivitamin" on later days.
static void format_slice(time_t ts, uint8_t med_index, char *buf, size_t len) {
    struct tm t = *localtime(&ts);
    char clock[12];
    strftime(clock, sizeof(clock), clock_is_24h_style() ? "%H:%M" : "%l:%M %p", &t);
    const char *c = (clock[0] == ' ') ? clock + 1 : clock;

    time_t now = time(NULL);
    struct tm today = *localtime(&now);
    char day[8] = "";
    if (t.tm_yday != today.tm_yday || t.tm_year != today.tm_year) {
        strftime(day, sizeof(day), "%a, ", &t);
    }

    MedEntry *med = med_list_get(med_index);
    bool hide_name = med_list_get_settings()->privacyMode || !med;
    snprintf(buf, len, "%s%s%s%s", day, c, hide_name ? "" : " ", hide_name ? "" : med->name);
}

static void reload_cb(AppGlanceReloadSession *session, size_t limit, void *ctx) {
    // Each slice names one dose and expires at its time, so the glance moves
    // on to the next dose while the app is closed.
    time_t after = time(NULL);
    size_t count = (limit < GLANCE_SLICES) ? limit : GLANCE_SLICES;
    for (size_t i = 0; i < count; i++) {
        uint8_t med_index = 0;
        time_t ts = next_dose(after, &med_index);
        if (ts == 0) break;
        char text[64];
        format_slice(ts, med_index, text, sizeof(text));
        AppGlanceSlice slice = {
            .layout = {
                .icon = APP_GLANCE_SLICE_DEFAULT_ICON,
                .subtitle_template_string = text,
            },
            .expiration_time = ts,
        };
        if (app_glance_add_slice(session, slice) != APP_GLANCE_RESULT_SUCCESS) break;
        after = ts;
    }
}

void glance_update(void) {
    app_glance_reload(reload_cb, NULL);
}

#else

void glance_update(void) {}

#endif
