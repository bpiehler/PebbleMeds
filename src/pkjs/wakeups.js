// PebbleMeds — wakeup slot planner
//
// Pure JS mirror of collect_dose_events() + notifications_schedule_wakeups()
// in src/c/notifications.c.  No Pebble SDK dependencies — imported by the
// test suite directly.
//
// NOTE: Keep this file in sync with notifications.c.  Any change to
// slot-filling, snooze-reservation, or per-med occurrence cap logic there
// must be reflected here, and tests/wakeups.test.js updated to match.
//
// The Pebble platform limits apps to 8 simultaneous wakeup slots.
// This module plans which slots to fill given the current med list and any
// pending snooze, matching the logic in the C implementation exactly.

'use strict';

var schedule = require('./schedule');

// Cookie values — must match WAKEUP_COOKIE_* in notifications.c
var COOKIE_DOSE   = 0;
var COOKIE_SNOOZE = 1;

// Collect dose events up to this many per medication.
// Matches the occ < 4 cap in collect_dose_events() in notifications.c.
var MAX_OCC_PER_MED = 4;

// Platform constants
var HORIZON_SECS   = 48 * 3600;  // only schedule within 48h
var RANGE_MIN_SECS = 60;          // wakeup_schedule rejects times < 60s from now

// ---------------------------------------------------------------------------
// collectDoseEvents(meds, now) → sorted array of { ts, medIndex }
//
// Mirrors collect_dose_events() in notifications.c.  Collects up to
// MAX_OCC_PER_MED occurrences per med within the 48h horizon, then sorts
// chronologically.  Events within RANGE_MIN_SECS of now are included here
// (they would fail wakeup_schedule with E_RANGE in C) — planWakeups filters
// them out without consuming a slot, matching C behaviour.
// ---------------------------------------------------------------------------
function collectDoseEvents(meds, now) {
  var horizon = now + HORIZON_SECS;
  var events  = [];

  meds.forEach(function (med, medIndex) {
    var times = schedule.getNextDoseTimes(med, now, horizon);
    var count = Math.min(times.length, MAX_OCC_PER_MED);
    for (var i = 0; i < count; i++) {
      events.push({ ts: times[i], medIndex: medIndex });
    }
  });

  events.sort(function (a, b) { return a.ts - b.ts; });
  return events;
}

// ---------------------------------------------------------------------------
// planWakeups(meds, now, pendingSnoozeTs) → array of { ts, type, medIndex? }
//
// Mirrors notifications_schedule_wakeups() in notifications.c.
//
// Returns up to 8 wakeup descriptors:
//   type: 'dose'   — a regular scheduled dose; medIndex is set
//   type: 'snooze' — a pending snooze reminder; medIndex is absent
//
// Key behaviours:
//   - Fills dose slots in chronological order
//   - Deduplicates same-second timestamps (two meds due simultaneously → 1 slot)
//   - Skips events within RANGE_MIN_SECS of now without consuming a slot
//   - When a snooze is pending, caps dose slots at 7 to guarantee the snooze
//     a slot (fixes the eviction bug where a full 8-slot dose schedule would
//     silently drop the snooze when another med fired mid-snooze)
//
// pendingSnoozeTs: pass 0 or null when no snooze is pending.
// ---------------------------------------------------------------------------
function planWakeups(meds, now, pendingSnoozeTs) {
  var hasSnooze = !!(pendingSnoozeTs && pendingSnoozeTs > now);
  var maxDose   = hasSnooze ? 7 : 8;

  var events  = collectDoseEvents(meds, now);
  var wakeups = [];
  var lastTs  = 0;

  for (var i = 0; i < events.length && wakeups.length < maxDose; i++) {
    var e = events[i];
    if (e.ts === lastTs)               continue;  // deduplicate same-second slots
    if (e.ts <= now + RANGE_MIN_SECS) continue;  // too close / in the past
    wakeups.push({ ts: e.ts, type: 'dose', medIndex: e.medIndex });
    lastTs = e.ts;
  }

  if (hasSnooze) {
    wakeups.push({ ts: pendingSnoozeTs, type: 'snooze' });
  }

  return wakeups;
}

module.exports = {
  planWakeups:       planWakeups,
  collectDoseEvents: collectDoseEvents,
  COOKIE_DOSE:       COOKIE_DOSE,
  COOKIE_SNOOZE:     COOKIE_SNOOZE,
  RANGE_MIN_SECS:    RANGE_MIN_SECS,
  MAX_OCC_PER_MED:   MAX_OCC_PER_MED,
  HORIZON_SECS:      HORIZON_SECS,
};
