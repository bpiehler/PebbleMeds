// PebbleMeds — dose schedule calculations
//
// Pure functions with no Pebble runtime dependencies so they can be
// required both by app.js (on-device) and by the Jest test suite.
//
// NOTE: This module mirrors med_list_next_dose_time() in src/c/med_list.c.
// Keep the two in sync — any change to scheduling logic there must be
// reflected here, and tests/schedule.test.js updated to match.

'use strict';

function getNextDoseTimes(med, fromTs, toTs) {
  if (med.scheduleType === 'fixed') {
    return getFixedTimes(med, fromTs, toTs);
  } else if (med.scheduleType === 'weekly') {
    return getWeeklyTimes(med, fromTs, toTs);
  } else {
    return getIntervalTimes(med, fromTs, toTs);
  }
}

function getFixedTimes(med, fromTs, toTs) {
  var results = [];
  var fromDate = new Date(fromTs * 1000);
  var toDate   = new Date(toTs * 1000);
  // Normalize to start of day for each date.
  var startDay = new Date(fromDate.getFullYear(), fromDate.getMonth(), fromDate.getDate());
  var endDay   = new Date(toDate.getFullYear(), toDate.getMonth(), toDate.getDate());
  var days     = Math.floor((endDay.getTime() - startDay.getTime()) / 86400000) + 1;
  for (var dayOffset = 0; dayOffset < days; dayOffset++) {
    var d = new Date(startDay.getTime() + dayOffset * 86400000);
    med.times.forEach(function (t) {
      var ts = Math.floor(
        new Date(d.getFullYear(), d.getMonth(), d.getDate(), t.h, t.m, 0).getTime() / 1000
      );
      if (ts >= fromTs && ts <= toTs) results.push(ts);
    });
  }
  return results;
}

function getIntervalTimes(med, fromTs, toTs) {
  var results = [];
  var intervalSecs = med.intervalHours * 3600;
  var base;

  if (med.lastTakenTs && med.lastTakenTs > 0) {
    base = med.lastTakenTs + intervalSecs;
  } else {
    // First run: anchor to startHour:startMinute today; advance if already past
    var now = new Date(fromTs * 1000);
    base = Math.floor(
      new Date(now.getFullYear(), now.getMonth(), now.getDate(),
               med.startHour, med.startMinute, 0).getTime() / 1000
    );
    if (base < fromTs) base += intervalSecs;
  }

  var ts = base;
  while (ts <= toTs) {
    if (ts >= fromTs) results.push(ts);
    ts += intervalSecs;
  }
  return results;
}

function getWeeklyTimes(med, fromTs, toTs) {
  var results = [];
  if (!med.weekMask) return results;

  var timeOfDay = med.times && med.times[0] ? med.times[0] : { h: 9, m: 0 };
  var fromDate  = new Date(fromTs * 1000);

  // Build the first candidate at time-of-day on the from date.
  var candidate = new Date(
    fromDate.getFullYear(), fromDate.getMonth(), fromDate.getDate(),
    timeOfDay.h, timeOfDay.m, 0
  );

  // Scan up to 7 days (covers one full week including wrap-around).
  for (var d = 0; d < 7; d++) {
    var dow = candidate.getDay();  // 0=Sunday
    var ts  = Math.floor(candidate.getTime() / 1000);

    if (ts >= fromTs && ts <= toTs && (med.weekMask & (1 << dow))) {
      results.push(ts);
    }

    // Advance to same time next day.
    candidate.setDate(candidate.getDate() + 1);
  }

  return results;
}

module.exports = {
  getNextDoseTimes: getNextDoseTimes,
  getFixedTimes:    getFixedTimes,
  getIntervalTimes: getIntervalTimes,
  getWeeklyTimes:   getWeeklyTimes,
};
