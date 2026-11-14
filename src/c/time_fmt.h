#pragma once
#include <pebble.h>

// Clock time in the user's 12/24h style: "9:05 AM" or "09:05".
void time_fmt_clock(time_t ts, char *buf, size_t len);

// Clock time with a day when not today: "9:05 AM", "Tomorrow 9:05 AM",
// "Wed 9:05 AM", "Nov 20 9:05 AM".
void time_fmt_day_clock(time_t ts, char *buf, size_t len);

// Compact time until or since ts, in system wording:
// "Now", "In 25 min", "In 2 h 5 min", "In 14 h", "5 min ago", "1 h 10 min ago".
// Beyond 24 hours falls back to time_fmt_day_clock.
void time_fmt_relative(time_t ts, char *buf, size_t len);
