'use strict';

var wakeups = require('../src/pkjs/wakeups');
var planWakeups       = wakeups.planWakeups;
var collectDoseEvents = wakeups.collectDoseEvents;
var RANGE_MIN_SECS    = wakeups.RANGE_MIN_SECS;
var MAX_OCC_PER_MED   = wakeups.MAX_OCC_PER_MED;
var HORIZON_SECS      = wakeups.HORIZON_SECS;

// Build a Unix timestamp from LOCAL date/time components (timezone-agnostic).
function ts(year, month, day, hour, min) {
  return Math.floor(new Date(year, month - 1, day, hour, min, 0).getTime() / 1000);
}

// Reference "now": Jan 15 2025, 10:00am local (same anchor as schedule.test.js)
var NOW = ts(2025, 1, 15, 10, 0);

// ---------------------------------------------------------------------------
// Med factory helpers
// ---------------------------------------------------------------------------

function fixedMed(hours) {
  // hours: single number or array of numbers
  var times = [].concat(hours).map(function (h) { return { h: h, m: 0 }; });
  return { scheduleType: 'fixed', times: times };
}

function intervalMed(intervalHours, startHour, lastTakenTs) {
  return {
    scheduleType:  'interval',
    intervalHours: intervalHours,
    startHour:     startHour,
    startMinute:   0,
    lastTakenTs:   lastTakenTs || 0,
  };
}

// ---------------------------------------------------------------------------
// collectDoseEvents
// ---------------------------------------------------------------------------
describe('collectDoseEvents', function () {

  test('returns empty array for zero meds', function () {
    expect(collectDoseEvents([], NOW)).toEqual([]);
  });

  test('returns events sorted chronologically across meds', function () {
    var meds = [fixedMed(14), fixedMed(12)];
    var events = collectDoseEvents(meds, NOW);
    for (var i = 1; i < events.length; i++) {
      expect(events[i].ts).toBeGreaterThanOrEqual(events[i - 1].ts);
    }
  });

  test('caps at MAX_OCC_PER_MED occurrences per med', function () {
    // 4h interval → ~12 occurrences in 48h; should be capped at 4
    var med = intervalMed(4, 10, 0);
    var events = collectDoseEvents([med], NOW);
    expect(events.length).toBeLessThanOrEqual(MAX_OCC_PER_MED);
  });

  test('includes near-past events (they fail E_RANGE in C, filtered in planWakeups)', function () {
    // Dose at now+30s is within RANGE_MIN_SECS; collectDoseEvents includes it
    var nearTs = NOW + 30;
    // Create a fixed med whose dose lands just 30s from now (minute-level fudge)
    var med = { scheduleType: 'fixed', times: [{ h: 10, m: 0 }] };
    var events = collectDoseEvents([med], NOW - 30);  // treat now as 30s before 10am
    var has30s = events.some(function (e) { return e.ts === NOW; });
    expect(has30s).toBe(true);
  });

  test('excludes events beyond the 48h horizon', function () {
    var med = fixedMed(10);  // 10am every day
    var events = collectDoseEvents([med], NOW);
    events.forEach(function (e) {
      expect(e.ts).toBeLessThanOrEqual(NOW + HORIZON_SECS);
    });
  });

  test('medIndex is correctly recorded for each event', function () {
    var meds = [fixedMed(12), fixedMed(14)];
    var events = collectDoseEvents(meds, NOW);
    var indices = events.map(function (e) { return e.medIndex; });
    expect(indices).toContain(0);
    expect(indices).toContain(1);
  });

});

// ---------------------------------------------------------------------------
// planWakeups — basic scheduling
// ---------------------------------------------------------------------------
describe('planWakeups - basic scheduling', function () {

  test('returns empty array when there are no meds', function () {
    expect(planWakeups([], NOW, 0)).toEqual([]);
  });

  test('single fixed med produces one dose wakeup', function () {
    var meds = [fixedMed(14)];  // 2pm, well in the future
    var result = planWakeups(meds, NOW, 0);
    expect(result.length).toBeGreaterThan(0);
    expect(result[0].type).toBe('dose');
    expect(result[0].ts).toBe(ts(2025, 1, 15, 14, 0));
  });

  test('single interval med produces a dose wakeup after start time', function () {
    var meds = [intervalMed(8, 12, 0)];  // start noon, 8h interval
    var result = planWakeups(meds, NOW, 0);
    expect(result.length).toBeGreaterThan(0);
    expect(result[0].type).toBe('dose');
    expect(result[0].ts).toBe(ts(2025, 1, 15, 12, 0));
  });

  test('wakeups are sorted chronologically', function () {
    var meds = [fixedMed(16), fixedMed(12), fixedMed(14)];
    var result = planWakeups(meds, NOW, 0);
    for (var i = 1; i < result.length; i++) {
      expect(result[i].ts).toBeGreaterThanOrEqual(result[i - 1].ts);
    }
  });

  test('total wakeups never exceed 8', function () {
    // 16 meds × 1 dose each = 16 events → should be capped at 8
    var meds = [];
    for (var h = 11; h <= 26; h++) {  // 16 distinct hours
      meds.push(fixedMed(h % 24));
    }
    var result = planWakeups(meds, NOW, 0);
    expect(result.length).toBeLessThanOrEqual(8);
  });

});

// ---------------------------------------------------------------------------
// planWakeups — slot cap and near-miss filtering
// ---------------------------------------------------------------------------
describe('planWakeups - slot cap and near-miss filtering', function () {

  test('events within 60s of now are excluded and do not consume a dose slot', function () {
    // Med A is due in 30s (too close); Med B is due in 2h.
    // Med A should be skipped without wasting a slot — B should appear.
    var medA = { scheduleType: 'fixed', times: [{ h: 10, m: 0 }] };  // exactly NOW
    var medB = fixedMed(12);  // 2h from now
    // Treat NOW as exactly 10:00:00; medA's dose is at exactly NOW (0s gap)
    var result = planWakeups([medA, medB], NOW, 0);
    var atNow = result.filter(function (w) { return w.ts === NOW; });
    expect(atNow.length).toBe(0);                        // medA's NOW dose excluded
    expect(result.some(function (w) { return w.ts === ts(2025, 1, 15, 12, 0); }))
      .toBe(true);                                        // medB still scheduled
  });

  test('event at exactly now + 61s is included', function () {
    var doseTs = NOW + 61;
    var med = { scheduleType: 'fixed',
                times: [{ h: Math.floor((doseTs % 86400) / 3600),
                          m: Math.floor((doseTs % 3600) / 60) }] };
    // Easier: just check the general filter boundary directly via an interval med
    // where we control the exact next-dose time.
    var takenAt = NOW - 8 * 3600 + 61;  // last taken such that next = NOW + 61
    var medInterval = intervalMed(8, 0, takenAt);
    var result = planWakeups([medInterval], NOW, 0);
    expect(result.length).toBeGreaterThan(0);
    expect(result[0].ts).toBe(NOW + 61);
  });

  test('near-miss dose does not consume a slot — later events still fill 8 slots', function () {
    // One near-miss dose (NOW + 30s) + 8 future doses → all 8 future slots filled
    var meds = [
      { scheduleType: 'fixed', times: [{ h: 10, m: 0 }] },  // near-miss at NOW
    ];
    // Add 8 meds at distinct hours well in the future
    for (var h = 11; h <= 18; h++) meds.push(fixedMed(h));

    var result = planWakeups(meds, NOW, 0);
    var doseWakeups = result.filter(function (w) { return w.type === 'dose'; });
    expect(doseWakeups.length).toBe(8);
  });

  test('more than 8 dose events yields exactly 8 wakeups — earliest ones win', function () {
    var meds = [];
    for (var h = 11; h <= 21; h++) meds.push(fixedMed(h));  // 11 events today
    var result = planWakeups(meds, NOW, 0);
    expect(result.length).toBe(8);
    // The 8 wakeups should be the 8 earliest (11am … 6pm)
    expect(result[0].ts).toBe(ts(2025, 1, 15, 11, 0));
    expect(result[7].ts).toBe(ts(2025, 1, 15, 18, 0));
  });

  test('events beyond 48h horizon are excluded', function () {
    var med = fixedMed(9);  // 9am every day; today's already passed (NOW=10am)
    var result = planWakeups([med], NOW, 0);
    result.forEach(function (w) {
      expect(w.ts).toBeLessThanOrEqual(NOW + HORIZON_SECS);
    });
  });

});

// ---------------------------------------------------------------------------
// planWakeups — deduplication
// ---------------------------------------------------------------------------
describe('planWakeups - deduplication', function () {

  test('two meds scheduled at the same second produce only one dose wakeup', function () {
    var meds = [fixedMed(14), fixedMed(14)];  // both due at 2pm
    var result = planWakeups(meds, NOW, 0);
    var at2pm  = result.filter(function (w) { return w.ts === ts(2025, 1, 15, 14, 0); });
    expect(at2pm.length).toBe(1);
  });

  test('deduplicated slot is filled by the next unique timestamp', function () {
    // Med A and Med B both at 12pm (deduplicated to 1 slot).
    // Med C at 1pm should still appear and fill the freed slot.
    var meds = [fixedMed(12), fixedMed(12), fixedMed(13)];
    var result = planWakeups(meds, NOW, 0);
    var at1pm = result.filter(function (w) { return w.ts === ts(2025, 1, 15, 13, 0); });
    expect(at1pm.length).toBe(1);
  });

  test('deduplication does not affect snooze wakeup', function () {
    // Snooze time happens to coincide with a dose time — both should appear
    // (different types; watch dispatches them separately).
    var snoozeTs = ts(2025, 1, 15, 12, 0);
    var meds = [fixedMed(12)];
    var result = planWakeups(meds, NOW, snoozeTs);
    var atNoon = result.filter(function (w) { return w.ts === snoozeTs; });
    expect(atNoon.length).toBe(2);  // one 'dose' + one 'snooze'
    expect(atNoon.map(function (w) { return w.type; }).sort())
      .toEqual(['dose', 'snooze']);
  });

});

// ---------------------------------------------------------------------------
// planWakeups — snooze slot reservation
// ---------------------------------------------------------------------------
describe('planWakeups - snooze slot reservation', function () {

  test('no pending snooze → up to 8 dose slots available', function () {
    var meds = [];
    for (var h = 11; h <= 19; h++) meds.push(fixedMed(h));  // 9 future doses
    var result = planWakeups(meds, NOW, 0);
    var doseCount = result.filter(function (w) { return w.type === 'dose'; }).length;
    expect(doseCount).toBe(8);
    expect(result.filter(function (w) { return w.type === 'snooze'; }).length).toBe(0);
  });

  test('pending snooze with fewer than 7 dose events → all doses + snooze', function () {
    var meds = [fixedMed(12), fixedMed(15)];  // 2 doses today + 2 tomorrow = 4 total
    var snoozeTs = NOW + 15 * 60;
    var result = planWakeups(meds, NOW, snoozeTs);
    expect(result.filter(function (w) { return w.type === 'dose'; }).length).toBe(4);
    expect(result.filter(function (w) { return w.type === 'snooze'; }).length).toBe(1);
    expect(result[result.length - 1]).toMatchObject({ ts: snoozeTs, type: 'snooze' });
  });

  test('pending snooze + exactly 8 dose events → 7 doses + 1 snooze (eviction fix)', function () {
    // This is the exact scenario that caused the bug: another med fires mid-snooze,
    // notifications_schedule_wakeups is called, and 8 dose events fill all slots,
    // silently dropping the snooze.
    var meds = [];
    for (var h = 11; h <= 18; h++) meds.push(fixedMed(h));  // exactly 8 future doses
    var snoozeTs = NOW + 15 * 60;
    var result = planWakeups(meds, NOW, snoozeTs);

    expect(result.length).toBe(8);
    expect(result.filter(function (w) { return w.type === 'dose'; }).length).toBe(7);
    expect(result.filter(function (w) { return w.type === 'snooze'; }).length).toBe(1);
  });

  test('pending snooze + more than 8 dose events → 7 doses + 1 snooze', function () {
    var meds = [];
    for (var h = 11; h <= 21; h++) meds.push(fixedMed(h));  // 11 future doses
    var snoozeTs = NOW + 15 * 60;
    var result = planWakeups(meds, NOW, snoozeTs);

    expect(result.length).toBe(8);
    expect(result.filter(function (w) { return w.type === 'dose'; }).length).toBe(7);
    expect(result.filter(function (w) { return w.type === 'snooze'; }).length).toBe(1);
  });

  test('expired snooze is not included', function () {
    var expiredSnooze = NOW - 60;  // 1 minute in the past
    var meds = [fixedMed(12)];
    var result = planWakeups(meds, NOW, expiredSnooze);
    expect(result.filter(function (w) { return w.type === 'snooze'; }).length).toBe(0);
  });

  test('snooze is always appended last regardless of its timestamp', function () {
    // Snooze at 10:10am; dose at 11am — snooze ts is earlier but must appear last
    var snoozeTs = NOW + 10 * 60;  // 10:10am
    var meds = [fixedMed(11)];     // 11am
    var result = planWakeups(meds, NOW, snoozeTs);
    expect(result[result.length - 1].type).toBe('snooze');
  });

  test('zero meds + pending snooze → only the snooze wakeup', function () {
    var snoozeTs = NOW + 15 * 60;
    var result = planWakeups([], NOW, snoozeTs);
    expect(result.length).toBe(1);
    expect(result[0]).toMatchObject({ ts: snoozeTs, type: 'snooze' });
  });

});

// ---------------------------------------------------------------------------
// planWakeups — per-med occurrence cap
// ---------------------------------------------------------------------------
describe('planWakeups - per-med occurrence cap', function () {

  test('high-frequency interval med contributes at most MAX_OCC_PER_MED events', function () {
    // 4h interval → up to 12 occurrences in 48h; only 4 should enter the pool
    var med = intervalMed(4, 11, 0);  // start 11am
    var result = planWakeups([med], NOW, 0);
    expect(result.length).toBeLessThanOrEqual(MAX_OCC_PER_MED);
  });

  test('two high-frequency meds each contribute up to 4 events; best 8 selected', function () {
    // Two 4h-interval meds → up to 8 events in pool → up to 8 slots filled
    var meds = [intervalMed(4, 11, 0), intervalMed(4, 13, 0)];
    var result = planWakeups(meds, NOW, 0);
    expect(result.length).toBeLessThanOrEqual(8);
    // All returned wakeups should be dose type and sorted
    for (var i = 1; i < result.length; i++) {
      expect(result[i].ts).toBeGreaterThan(result[i - 1].ts);
    }
  });

  test('fixed med with 4 daily doses contributes at most 4 events in pool', function () {
    // 4 times per day × 2 days = 8 occurrences in 48h; capped at 4
    var med = fixedMed([8, 12, 16, 20]);
    var events = collectDoseEvents([med], NOW);
    expect(events.length).toBeLessThanOrEqual(MAX_OCC_PER_MED);
  });

});

// ---------------------------------------------------------------------------
// planWakeups — real-world user scenarios
// ---------------------------------------------------------------------------
describe('planWakeups - real-world user scenarios', function () {

  test('mid-snooze scenario: rescheduling after another med fires preserves snooze', function () {
    // User snoozed Med A at 10am (snooze expires 10:15am).
    // Med B fires at 10:10am, triggering a full reschedule.
    // After reschedule at 10:10am, snooze at 10:15am must still be present.
    var snoozeTs = NOW + 15 * 60;          // 10:15am
    var rescheduleNow = NOW + 10 * 60;      // 10:10am — when Med B fires

    // Enough meds to fill 8 dose slots
    var meds = [];
    for (var h = 11; h <= 18; h++) meds.push(fixedMed(h));

    var result = planWakeups(meds, rescheduleNow, snoozeTs);
    expect(result.filter(function (w) { return w.type === 'snooze'; }).length).toBe(1);
    expect(result.filter(function (w) { return w.type === 'snooze'; })[0].ts).toBe(snoozeTs);
    expect(result.length).toBeLessThanOrEqual(8);
  });

  test('interval med taken late: next dose is spaced from actual taken time, not start time', function () {
    // 12h interval, start 9am; user takes it at 11am.
    // Next dose should be 11pm (11am + 12h), NOT 9pm (9am + 12h).
    var takenAt  = ts(2025, 1, 15, 11, 0);
    var med      = intervalMed(12, 9, takenAt);
    var result   = planWakeups([med], takenAt + 1, 0);
    expect(result[0].ts).toBe(ts(2025, 1, 15, 23, 0));   // 11pm ✓
    expect(result[0].ts).not.toBe(ts(2025, 1, 15, 21, 0)); // not 9pm ✗
  });

  test('interval med with overdue dose: that dose slot skipped, next interval scheduled', function () {
    // 8h interval; last taken 10 hours ago → next dose was due 2 hours ago.
    // The overdue slot must not consume a wakeup slot; the FOLLOWING interval should be scheduled.
    var takenAt    = NOW - 10 * 3600;   // taken 10h ago
    var overdueTos = takenAt + 8 * 3600; // due 2h ago
    var nextDue    = overdueTos + 8 * 3600; // due 6h from now

    var med    = intervalMed(8, 0, takenAt);
    var result = planWakeups([med], NOW, 0);

    expect(result.some(function (w) { return w.ts === overdueTos; })).toBe(false);
    expect(result.some(function (w) { return w.ts === nextDue; })).toBe(true);
  });

  test('fixed med: when all of today\'s times have passed, tomorrow\'s are scheduled', function () {
    // Med with 8am dose; NOW is 10am so today's dose has passed.
    var med    = fixedMed(8);
    var result = planWakeups([med], NOW, 0);
    var tomorrowDose = ts(2025, 1, 16, 8, 0);
    expect(result.some(function (w) { return w.ts === tomorrowDose; })).toBe(true);
    expect(result.some(function (w) { return w.ts === ts(2025, 1, 15, 8, 0); })).toBe(false);
  });

  test('mixed fixed + interval meds + pending snooze all schedule correctly', function () {
    var meds = [
      fixedMed([8, 20]),          // 2x fixed daily (8am has passed; 8pm and tomorrow 8am in window)
      intervalMed(12, 9, 0),      // 12h interval from 9am
      intervalMed(6, 8, 0),       // 6h interval from 8am
    ];
    var snoozeTs = NOW + 20 * 60;
    var result   = planWakeups(meds, NOW, snoozeTs);

    // Basic sanity checks
    expect(result.length).toBeLessThanOrEqual(8);
    expect(result.filter(function (w) { return w.type === 'snooze'; }).length).toBe(1);
    result.forEach(function (w) {
      if (w.type === 'dose') {
        expect(w.ts).toBeGreaterThan(NOW + RANGE_MIN_SECS);
        expect(w.ts).toBeLessThanOrEqual(NOW + HORIZON_SECS);
      }
    });
    // Dose wakeups should be sorted
    var doses = result.filter(function (w) { return w.type === 'dose'; });
    for (var i = 1; i < doses.length; i++) {
      expect(doses[i].ts).toBeGreaterThan(doses[i - 1].ts);
    }
  });

  test('16 meds (platform maximum): no crash, 8 slots filled, snooze preserved', function () {
    var meds = [];
    for (var i = 0; i < 16; i++) {
      meds.push(fixedMed(11 + (i % 12)));  // spread across hours 11am–10pm
    }
    var snoozeTs = NOW + 15 * 60;
    var result   = planWakeups(meds, NOW, snoozeTs);

    expect(result.length).toBeLessThanOrEqual(8);
    expect(result.filter(function (w) { return w.type === 'snooze'; }).length).toBe(1);
  });

  test('stale lastTakenTs: future doses still scheduled (JS handles correctly; note C limitation)', function () {
    // lastTakenTs 4 days ago with 8h interval.
    // In the C implementation, collect_dose_events fills all 4 per-med slots
    // with past occurrences, scheduling nothing for this med.
    // planWakeups (JS) correctly advances past them and finds future slots.
    var staleTaken = NOW - 4 * 24 * 3600;  // 4 days ago
    var med        = intervalMed(8, 9, staleTaken);
    var result     = planWakeups([med], NOW, 0);

    expect(result.length).toBeGreaterThan(0);
    result.forEach(function (w) {
      expect(w.ts).toBeGreaterThan(NOW + RANGE_MIN_SECS);
    });
  });

  test('snooze fires but dose wakeup also fires within the same 5-min window', function () {
    // Both snooze and a dose are in the plan — both should appear with correct types.
    var snoozeTs = ts(2025, 1, 15, 11, 0);
    var doseMed  = fixedMed(14);
    var result   = planWakeups([doseMed], NOW, snoozeTs);

    expect(result.filter(function (w) { return w.type === 'snooze'; }).length).toBe(1);
    expect(result.filter(function (w) { return w.type === 'dose'; }).length).toBeGreaterThan(0);
  });

  test('snooze set to 15 minutes; med due in 10 minutes also scheduled', function () {
    // Both a near-term dose and a snooze should coexist in the plan.
    var snoozeTs = NOW + 15 * 60;
    var med = { scheduleType: 'fixed',
                times: [{ h: 10, m: 10 }] };  // 10:10am = NOW + 10min
    var result = planWakeups([med], NOW, snoozeTs);

    // The med fires today at 10:10am AND tomorrow at 10:10am — both are in the 48h window.
    // The important assertion is that at least one dose wakeup coexists with the snooze.
    expect(result.filter(function (w) { return w.type === 'dose'; }).length).toBeGreaterThanOrEqual(1);
    expect(result.filter(function (w) { return w.type === 'snooze'; }).length).toBe(1);
  });

});
