// PebbleMeds — dose schedule calculations
//
// Pure functions with no Pebble runtime dependencies so they can be
// required both by index.js (on-device) and by the Jest test suite.

'use strict';

function getNextDoseTimes(med, fromTs, toTs) {
  if (med.scheduleType === 'fixed') {
    return getFixedTimes(med, fromTs, toTs);
  } else {
    return getIntervalTimes(med, fromTs, toTs);
  }
}

function getFixedTimes(med, fromTs, toTs) {
  var results = [];
  var date = new Date(fromTs * 1000);
  // Check today + next 2 days to cover any 48h window
  for (var dayOffset = 0; dayOffset <= 2; dayOffset++) {
    var d = new Date(date.getFullYear(), date.getMonth(), date.getDate() + dayOffset);
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

// Use property-style exports — Pebble's JS runtime does not support
// replacing module.exports with a new object (only exports.foo = bar works).
exports.getNextDoseTimes = getNextDoseTimes;
exports.getFixedTimes    = getFixedTimes;
exports.getIntervalTimes = getIntervalTimes;
