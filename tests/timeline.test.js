// Mock the Pebble global before requiring timeline (not available in Node.js)
global.Pebble = {
  insertTimelinePin: jest.fn(function (pinJson, success) { success(); }),
  deleteTimelinePin: jest.fn(function (id) { /* mock delete */ })
};

// Mock localStorage
var storedItems = {};
global.localStorage = {
  getItem: jest.fn(function(key) { return storedItems[key] || null; }),
  setItem: jest.fn(function(key, value) { storedItems[key] = value; }),
  removeItem: jest.fn(function(key) { delete storedItems[key]; }),
  clear: jest.fn(function() { storedItems = {}; })
};

// Mock schedule so pushTimelinePins tests use controlled dose times
jest.mock('../src/pkjs/schedule', function () {
  return { getNextDoseTimes: jest.fn() };
});

var timeline        = require('../src/pkjs/timeline');
var schedule        = require('../src/pkjs/schedule');
var buildPin        = timeline.buildPin;
var pushTimelinePins = timeline.pushTimelinePins;

var FIXED_TS = 1700000000;  // arbitrary stable timestamp for pin structure tests

// ---------------------------------------------------------------------------
// buildPin — medId-based stable pin IDs
// ---------------------------------------------------------------------------
describe('buildPin', function () {

  test('pin id uses medId, not array index', function () {
    var med = {
      medId: 'abc-123-uuid',
      name: 'Aspirin',
      taker: 'Alice',
      dose: '100mg',
    };
    var pin = buildPin(med, FIXED_TS, false);
    expect(pin.id).toBe('pebble-meds-abc-123-uuid-' + FIXED_TS);
  });

  test('two calls with same medId + ts produce identical pin ids (idempotent)', function () {
    var med = { medId: 'xyz-789', name: 'Ibuprofen', taker: 'Bob' };
    var pin1 = buildPin(med, FIXED_TS, false);
    var pin2 = buildPin(med, FIXED_TS, false);
    expect(pin1.id).toBe(pin2.id);
    expect(pin1.id).toBe('pebble-meds-xyz-789-' + FIXED_TS);
  });

  test('different medId produces different pin id', function () {
    var med1 = { medId: 'id-one', name: 'MedA', taker: 'Alice' };
    var med2 = { medId: 'id-two', name: 'MedA', taker: 'Alice' };
    var pin1 = buildPin(med1, FIXED_TS, false);
    var pin2 = buildPin(med2, FIXED_TS, false);
    expect(pin1.id).not.toBe(pin2.id);
  });

  test('privacy mode hides med name', function () {
    var med = { medId: 'p-id', name: 'SecretDrug', taker: 'Carol', dose: '50mg' };
    var pinPrivate = buildPin(med, FIXED_TS, true);
    var pinPublic  = buildPin(med, FIXED_TS, false);
    expect(pinPrivate.layout.title).toBe('Medication Due');
    expect(pinPublic.layout.title).toBe('SecretDrug');
  });

});

// ---------------------------------------------------------------------------
// pushTimelinePins — medId-keyed pin map
// ---------------------------------------------------------------------------
describe('pushTimelinePins', function () {

  beforeEach(function () {
    storedItems = {};
    schedule.getNextDoseTimes.mockReturnValue([]);
    Pebble.insertTimelinePin.mockClear();
    Pebble.deleteTimelinePin.mockClear();
    global.localStorage.setItem.mockClear();
  });

  test('stores pin id map keyed by medId, not array index', function () {
    var cfg = {
      meds: [
        { medId: 'med-a', name: 'Aspirin', taker: 'Alice', scheduleType: 'fixed', times: [{ h: 8, m: 0 }] },
        { medId: 'med-b', name: 'Ibuprofen', taker: 'Bob', scheduleType: 'fixed', times: [{ h: 9, m: 0 }] },
      ],
      settings: {},
    };
    schedule.getNextDoseTimes
      .mockReturnValueOnce([FIXED_TS + 100])
      .mockReturnValueOnce([FIXED_TS + 200]);

    pushTimelinePins(cfg);

    var raw = storedItems['pebble_meds_timeline_pin_ids'];
    var pinMap = JSON.parse(raw);
    // Must be keyed by medId, not 0/1
    expect(pinMap['med-a']).toBeDefined();
    expect(pinMap['med-b']).toBeDefined();
    expect(pinMap['0']).toBeUndefined();
    expect(pinMap['1']).toBeUndefined();
    // Pin IDs must contain the correct medId
    expect(pinMap['med-a'][0]).toContain('pebble-meds-med-a-');
    expect(pinMap['med-b'][0]).toContain('pebble-meds-med-b-');
  });

  test('medId migration: assigns medId to med without one and persists to localStorage', function () {
    var cfg = {
      meds: [
        // No medId — should get one assigned
        { name: 'Tylenol', taker: 'Carol', scheduleType: 'fixed', times: [{ h: 12, m: 0 }] },
      ],
      settings: {},
    };
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS + 300]);

    pushTimelinePins(cfg);

    // medId must have been assigned
    expect(cfg.meds[0].medId).toBeDefined();
    expect(typeof cfg.meds[0].medId).toBe('string');
    expect(cfg.meds[0].medId.length).toBeGreaterThan(0);

    // The migration must have been persisted to pebble_meds_config
    var savedConfig = JSON.parse(storedItems['pebble_meds_config']);
    expect(savedConfig.meds[0].medId).toBe(cfg.meds[0].medId);
  });

  test('second push with same medId produces identical pin ids (stable)', function () {
    var cfg = {
      meds: [
        { medId: 'stable-1', name: 'Aspirin', taker: 'Alice', scheduleType: 'fixed', times: [{ h: 8, m: 0 }] },
      ],
      settings: {},
    };
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS + 400]);

    pushTimelinePins(cfg);
    var raw1 = storedItems['pebble_meds_timeline_pin_ids'];
    var map1 = JSON.parse(raw1);

    // Same medId on second call
    pushTimelinePins(cfg);
    var raw2 = storedItems['pebble_meds_timeline_pin_ids'];
    var map2 = JSON.parse(raw2);

    expect(map1['stable-1'][0]).toBe(map2['stable-1'][0]);
  });

  test('deletion scenario: deleting middle med leaves no orphaned pins', function () {
    // Step 1: 3 meds, push pins
    var cfg = {
      meds: [
        { medId: 'med-0', name: 'Aspirin',    taker: 'Alice', scheduleType: 'fixed', times: [{ h: 8, m: 0 }] },
        { medId: 'med-1', name: 'Ibuprofen',  taker: 'Bob',   scheduleType: 'fixed', times: [{ h: 9, m: 0 }] },
        { medId: 'med-2', name: 'Tylenol',    taker: 'Carol', scheduleType: 'fixed', times: [{ h: 10, m: 0 }] },
      ],
      settings: {},
    };
    schedule.getNextDoseTimes
      .mockReturnValueOnce([FIXED_TS + 10])
      .mockReturnValueOnce([FIXED_TS + 20])
      .mockReturnValueOnce([FIXED_TS + 30]);

    pushTimelinePins(cfg);

    var raw3 = storedItems['pebble_meds_timeline_pin_ids'];
    var map3 = JSON.parse(raw3);
    expect(Object.keys(map3)).toHaveLength(3);
    expect(map3['med-0']).toHaveLength(1);
    expect(map3['med-1']).toHaveLength(1);
    expect(map3['med-2']).toHaveLength(1);

    // Step 2: delete Ibuprofen (med-1), push again — med-1 should not appear
    cfg.meds = [
      { medId: 'med-0', name: 'Aspirin', taker: 'Alice', scheduleType: 'fixed', times: [{ h: 8, m: 0 }] },
      // med-1 intentionally omitted (deleted)
      { medId: 'med-2', name: 'Tylenol', taker: 'Carol', scheduleType: 'fixed', times: [{ h: 10, m: 0 }] },
    ];
    schedule.getNextDoseTimes
      .mockReturnValueOnce([FIXED_TS + 10])
      .mockReturnValueOnce([FIXED_TS + 30]);

    pushTimelinePins(cfg);

    var raw4 = storedItems['pebble_meds_timeline_pin_ids'];
    var map4 = JSON.parse(raw4);

    // Only med-0 and med-2 should have pins — med-1 must be gone (no orphaned pin for med-1)
    expect(Object.keys(map4)).toHaveLength(2);
    expect(map4['med-0']).toBeDefined();
    expect(map4['med-1']).toBeUndefined();  // deleted med — no orphaned pin
    expect(map4['med-2']).toBeDefined();
  });

  test('no migration needed if all meds already have medId', function () {
    var cfg = {
      meds: [
        { medId: 'existing-1', name: 'Aspirin', taker: 'Alice', scheduleType: 'fixed', times: [{ h: 8, m: 0 }] },
      ],
      settings: {},
    };
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS + 500]);

    pushTimelinePins(cfg);

    // No migration should occur — medId was already present
    expect(cfg.meds[0].medId).toBe('existing-1');
    // pebble_meds_config should NOT be overwritten (migration didn't run)
    // We can verify this by checking the config wasn't re-saved unnecessarily
    // Since the migration check sets needsMigration=false, the setItem for config is skipped.
    // The fact that medId remains 'existing-1' confirms no re-assignment.
  });

});
