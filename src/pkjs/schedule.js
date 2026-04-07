// PebbleMeds — dose schedule calculations
//
// Wrapped in an IIFE so that if Pebble's bundler concatenates all pkjs files
// into a flat scope, these function names don't collide with index.js.
// In Jest (Node.js), module.exports is set inside the IIFE and require() works normally.

'use strict';

(function () {
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

  // Export for Jest (Node.js). In the Pebble runtime these functions are
  // inlined directly in index.js; this file is only included for the tests.
  if (typeof module !== 'undefined') {
    module.exports = {
      getNextDoseTimes: getNextDoseTimes,
      getFixedTimes:    getFixedTimes,
      getIntervalTimes: getIntervalTimes
    };
  }
}());
