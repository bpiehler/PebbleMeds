#include "time_fmt.h"

#define SECS_PER_DAY (24 * 60 * 60)

void time_fmt_clock(time_t ts, char *buf, size_t len) {
    struct tm *t = localtime(&ts);
    if (clock_is_24h_style()) {
        snprintf(buf, len, "%02d:%02d", t->tm_hour, t->tm_min);
    } else {
        int h = t->tm_hour % 12;
        if (h == 0) h = 12;
        snprintf(buf, len, "%d:%02d %s", h, t->tm_min, t->tm_hour >= 12 ? "PM" : "AM");
    }
}

// Calendar days from today to ts (0 = today, 1 = tomorrow).
static int days_from_today(time_t ts) {
    struct tm t = *localtime(&ts);
    time_t ts_midnight = ts - (t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
    // Round to absorb DST shifts.
    return (int)((ts_midnight - time_start_of_today() + SECS_PER_DAY / 2) / SECS_PER_DAY);
}

void time_fmt_day_clock(time_t ts, char *buf, size_t len) {
    char clock[12];
    time_fmt_clock(ts, clock, sizeof(clock));
    int days = days_from_today(ts);
    if (days == 0) {
        snprintf(buf, len, "%s", clock);
    } else if (days == 1) {
        snprintf(buf, len, "Tomorrow %s", clock);
    } else if (days == -1) {
        snprintf(buf, len, "Yesterday %s", clock);
    } else {
        // "Wed" within the week, else "Nov 3" (no padded day number).
        struct tm *t = localtime(&ts);
        char day[8];
        strftime(day, sizeof(day), (days > 1 && days < 7) ? "%a" : "%b", t);
        if (days > 1 && days < 7) snprintf(buf, len, "%s %s", day, clock);
        else                      snprintf(buf, len, "%s %d %s", day, t->tm_mday, clock);
    }
}

// "25 min", "2 h 5 min", "14 h"
static void fmt_span(int mins, char *buf, size_t len) {
    int h = mins / 60, m = mins % 60;
    if (h == 0)            snprintf(buf, len, "%d min", m);
    else if (m == 0 || h >= 10) snprintf(buf, len, "%d h", h);
    else                   snprintf(buf, len, "%d h %d min", h, m);
}

void time_fmt_relative(time_t ts, char *buf, size_t len) {
    int diff = (int)(ts - time(NULL));
    if (diff >= SECS_PER_DAY || diff <= -SECS_PER_DAY) {
        time_fmt_day_clock(ts, buf, len);
        return;
    }
    char span[16];
    if (diff > 0) {
        fmt_span((diff + 59) / 60, span, sizeof(span));  // round up, as the system does
        snprintf(buf, len, "In %s", span);
    } else if (diff > -60) {
        snprintf(buf, len, "Now");
    } else {
        fmt_span(-diff / 60, span, sizeof(span));        // round down
        snprintf(buf, len, "%s ago", span);
    }
}
