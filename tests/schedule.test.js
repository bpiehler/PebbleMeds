'use strict';

var schedule = require('../src/pkjs/schedule');
var getFixedTimes    = schedule.getFixedTimes;
var getIntervalTimes = schedule.getIntervalTimes;
var getNextDoseTimes = schedule.getNextDoseTimes;

// Build a Unix timestamp from LOCAL date/time components so tests are
// timezone-agnostic (the scheduling functions also use local time).
function ts(year, month, day, hour, min) {
  return Math.floor(new Date(year, month - 1, day, hour, min, 0).getTime() / 1000);
}

// Reference "now": Jan 15 2025 10:00am local
var NOW = ts(2025, 1, 15, 10, 0);
var WIN = NOW + 48 * 3600;  // 48h window end

// ---------------------------------------------------------------------------
// getFixedTimes
// ---------------------------------------------------------------------------
describe('getFixedTimes', function () {

  test('returns a dose scheduled later today', function () {
    var med = { times: [{ h: 14, m: 0 }] };
    expect(getFixedTimes(med, NOW, WIN)).toContain(ts(2025, 1, 15, 14, 0));
  });

  test('does not return a dose that already passed today', function () {
    var med = { times: [{ h: 8, m: 0 }] };
    expect(getFixedTimes(med, NOW, WIN)).not.toContain(ts(2025, 1, 15, 8, 0));
  });

  test('returns tomorrow\'s dose when today\'s has passed', function () {
    var med = { times: [{ h: 8, m: 0 }] };
    expect(getFixedTimes(med, NOW, WIN)).toContain(ts(2025, 1, 16, 8, 0));
  });

  test('returns all future occurrences in a multi-time-per-day schedule', function () {
    var med = { times: [{ h: 8, m: 0 }, { h: 14, m: 0 }, { h: 22, m: 0 }] };
    var result = getFixedTimes(med, NOW, WIN);
    expect(result).not.toContain(ts(2025, 1, 15, 8, 0));  // already past
    expect(result).toContain(ts(2025, 1, 15, 14, 0));
    expect(result).toContain(ts(2025, 1, 15, 22, 0));
    expect(result).toContain(ts(2025, 1, 16, 8, 0));
    expect(result).toContain(ts(2025, 1, 16, 14, 0));
  });

  test('includes dose exactly at fromTs boundary (>=)', function () {
    var med = { times: [{ h: 10, m: 0 }] };
    expect(getFixedTimes(med, NOW, WIN)).toContain(NOW);
  });

  test('includes dose exactly at toTs boundary (<=)', function () {
    var exactEnd = ts(2025, 1, 17, 10, 0);
    var med = { times: [{ h: 10, m: 0 }] };
    expect(getFixedTimes(med, NOW, exactEnd)).toContain(exactEnd);
  });

  test('excludes dose one second after toTs', function () {
    var narrowTo = NOW + 3600;  // window closes at 11am
    var med = { times: [{ h: 12, m: 0 }] };
    expect(getFixedTimes(med, NOW, narrowTo)).not.toContain(ts(2025, 1, 15, 12, 0));
  });

  test('returns empty array when times list is empty', function () {
    var med = { times: [] };
    expect(getFixedTimes(med, NOW, WIN)).toEqual([]);
  });

  test('handles minute-precision times correctly', function () {
    var med = { times: [{ h: 10, m: 30 }] };
    expect(getFixedTimes(med, NOW, WIN)).toContain(ts(2025, 1, 15, 10, 30));
  });

});

// ---------------------------------------------------------------------------
// getIntervalTimes
// ---------------------------------------------------------------------------
describe('getIntervalTimes', function () {

  test('with lastTakenTs: first dose is lastTakenTs + interval', function () {
    var med = { intervalHours: 8, lastTakenTs: ts(2025, 1, 15, 6, 0), startHour: 8, startMinute: 0 };
    var result = getIntervalTimes(med, NOW, WIN);
    expect(result[0]).toBe(ts(2025, 1, 15, 14, 0));  // 6am + 8h = 2pm
  });

  test('with lastTakenTs: returns multiple evenly-spaced doses in 48h window', function () {
    var med = { intervalHours: 8, lastTakenTs: ts(2025, 1, 15, 6, 0), startHour: 8, startMinute: 0 };
    var result = getIntervalTimes(med, NOW, WIN);
    expect(result.length).toBeGreaterThanOrEqual(5);
    for (var i = 1; i < result.length; i++) {
      expect(result[i] - result[i - 1]).toBe(8 * 3600);
    }
  });

  test('without lastTakenTs: first dose is startHour when it is in the future', function () {
    var med = { intervalHours: 8, lastTakenTs: 0, startHour: 12, startMinute: 0 };
    var result = getIntervalTimes(med, NOW, WIN);
    expect(result[0]).toBe(ts(2025, 1, 15, 12, 0));
  });

  test('without lastTakenTs: advances past startHour if it has already passed', function () {
    // NOW is 10am; startHour 8am has passed → next slot is 8am + 8h = 4pm
    var med = { intervalHours: 8, lastTakenTs: 0, startHour: 8, startMinute: 0 };
    var result = getIntervalTimes(med, NOW, WIN);
    expect(result[0]).toBe(ts(2025, 1, 15, 16, 0));
    expect(result[0]).toBeGreaterThanOrEqual(NOW);
  });

  test('without lastTakenTs: startHour exactly at fromTs is included', function () {
    // startHour 10am = NOW exactly; base === fromTs so should appear
    var med = { intervalHours: 8, lastTakenTs: 0, startHour: 10, startMinute: 0 };
    var result = getIntervalTimes(med, NOW, WIN);
    expect(result[0]).toBe(NOW);
  });

  test('all returned doses fall within [fromTs, toTs]', function () {
    var med = { intervalHours: 6, lastTakenTs: 0, startHour: 8, startMinute: 0 };
    var result = getIntervalTimes(med, NOW, WIN);
    result.forEach(function (t) {
      expect(t).toBeGreaterThanOrEqual(NOW);
      expect(t).toBeLessThanOrEqual(WIN);
    });
  });

  test('24h interval yields at most 2 doses in a 48h window', function () {
    var med = { intervalHours: 24, lastTakenTs: 0, startHour: 12, startMinute: 0 };
    var result = getIntervalTimes(med, NOW, WIN);
    expect(result.length).toBeGreaterThanOrEqual(1);
    expect(result.length).toBeLessThanOrEqual(2);
  });

  test('4h interval yields ~12 doses in a 48h window', function () {
    var med = { intervalHours: 4, lastTakenTs: 0, startHour: 12, startMinute: 0 };
    var result = getIntervalTimes(med, NOW, WIN);
    expect(result.length).toBeGreaterThanOrEqual(10);
  });

  test('handles startMinute correctly', function () {
    var med = { intervalHours: 8, lastTakenTs: 0, startHour: 10, startMinute: 30 };
    var result = getIntervalTimes(med, NOW, WIN);
    expect(result[0]).toBe(ts(2025, 1, 15, 10, 30));
  });

  test('without lastTakenTs: advances multiple intervals if many have passed since startHour', function () {
    // NOW is 10am; startHour 02:00am today. 
    // 4h interval: 02:00, 06:00, 10:00 (NOW), 14:00...
    // result[0] should be 10:00 (NOW)
    var med = { intervalHours: 4, lastTakenTs: 0, startHour: 2, startMinute: 0 };
    var result = getIntervalTimes(med, NOW, WIN);
    expect(result[0]).toBe(NOW);
  });

  test('lastTakenTs far in the past: still yields correct first future dose', function () {
    // Took 3 days ago at 8am; 8h interval → doses every 8h, find next after NOW
    var oldTaken = ts(2025, 1, 12, 8, 0);
    var med = { intervalHours: 8, lastTakenTs: oldTaken, startHour: 8, startMinute: 0 };
    var result = getIntervalTimes(med, NOW, WIN);
    // First result must be >= NOW
    expect(result[0]).toBeGreaterThanOrEqual(NOW);
    // And exactly one interval after the previous
    var prev = result[0] - 8 * 3600;
    expect((result[0] - oldTaken) % (8 * 3600)).toBe(0);
  });

});

// ---------------------------------------------------------------------------
// getNextDoseTimes
// ---------------------------------------------------------------------------
describe('getNextDoseTimes', function () {

  test('routes fixed schedules to getFixedTimes', function () {
    var med = { scheduleType: 'fixed', times: [{ h: 14, m: 0 }] };
    var result = getNextDoseTimes(med, NOW, WIN);
    expect(result).toContain(ts(2025, 1, 15, 14, 0));
  });

  test('routes interval schedules to getIntervalTimes', function () {
    var med = { scheduleType: 'interval', intervalHours: 8, lastTakenTs: 0, startHour: 14, startMinute: 0 };
    var result = getNextDoseTimes(med, NOW, WIN);
    expect(result[0]).toBe(ts(2025, 1, 15, 14, 0));
  });

  test('returns empty array for fixed med with no doses in narrow window', function () {
    var narrowWin = NOW + 1800;  // only 30 min
    var med = { scheduleType: 'fixed', times: [{ h: 14, m: 0 }] };
    expect(getNextDoseTimes(med, NOW, narrowWin)).toEqual([]);
  });

});
