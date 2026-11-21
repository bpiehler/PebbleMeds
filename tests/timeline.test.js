'use strict';

var MED_A_ID = 'abc-123-uuid';
var MED_B_ID = 'xyz-789-uuid';
var FIXED_TS  = 1700000000;

var storedItems = {};

global.Pebble = {
  insertTimelinePin: jest.fn(function (pinJson, success) { success(); }),
  deleteTimelinePin: jest.fn(),
};

global.localStorage = {
  getItem: jest.fn(function(key) { return storedItems[key] || null; }),
  setItem: jest.fn(function(key, value) { storedItems[key] = value; }),
};

jest.mock('../src/pkjs/schedule', function () {
  return { getNextDoseTimes: jest.fn() };
});

var timeline        = require('../src/pkjs/timeline');
var schedule        = require('../src/pkjs/schedule');
var buildPin        = timeline.buildPin;
var pushTimelinePins = timeline.pushTimelinePins;

// ---------------------------------------------------------------------------
// buildPin — medId-based stable pin IDs
// ---------------------------------------------------------------------------
describe('buildPin', function () {

  test('pin id uses medId, not array index', function () {
    var med = { medId: MED_A_ID, name: 'Aspirin', taker: 'Alice', dose: '100mg' };
    var pin = buildPin(med, FIXED_TS, false);
    expect(pin.id).toBe('pebble-meds-' + MED_A_ID + '-' + FIXED_TS);
  });

  test('two calls with same medId + ts produce identical pin ids', function () {
    var med = { medId: MED_B_ID, name: 'Ibuprofen', taker: 'Bob' };
    var pin1 = buildPin(med, FIXED_TS, false);
    var pin2 = buildPin(med, FIXED_TS, false);
    expect(pin1.id).toBe(pin2.id);
  });

  test('different medId produces different pin id', function () {
    var med1 = { medId: 'id-one', name: 'MedA', taker: 'Alice' };
    var med2 = { medId: 'id-two', name: 'MedA', taker: 'Alice' };
    expect(buildPin(med1, FIXED_TS, false).id).not.toBe(buildPin(med2, FIXED_TS, false).id);
  });

  test('uses med name as title when privacy is off', function () {
    var med = { medId: MED_A_ID, name: 'Aspirin', taker: 'Self', dose: '100mg' };
    expect(buildPin(med, FIXED_TS, false).layout.title).toBe('Aspirin');
  });

  test('uses "Medication Due" as title when privacy is on', function () {
    var med = { medId: MED_A_ID, name: 'Aspirin', taker: 'Self', dose: '100mg' };
    expect(buildPin(med, FIXED_TS, true).layout.title).toBe('Medication Due');
  });

  test('body includes taker and dose when privacy is off', function () {
    var med = { medId: MED_A_ID, name: 'Aspirin', taker: 'Self', dose: '100mg' };
    var body = buildPin(med, FIXED_TS, false).layout.body;
    expect(body).toContain('Self');
    expect(body).toContain('100mg');
  });

  test('body is taker only when privacy is on', function () {
    var med = { medId: MED_A_ID, name: 'Aspirin', taker: 'Self', dose: '100mg' };
    var body = buildPin(med, FIXED_TS, true).layout.body;
    expect(body).toBe('Self');
    expect(body).not.toContain('Aspirin');
    expect(body).not.toContain('100mg');
  });

  test('body omits dose separator when dose is empty', function () {
    var med = { medId: MED_A_ID, name: 'Aspirin', taker: 'Self', dose: '' };
    expect(buildPin(med, FIXED_TS, false).layout.body).toBe('Self');
  });

  test('body omits dose separator when dose is absent', function () {
    var med = { medId: MED_A_ID, name: 'Aspirin', taker: 'Self' };
    expect(buildPin(med, FIXED_TS, false).layout.body).toBe('Self');
  });

  test('time is a valid ISO 8601 string matching the timestamp', function () {
    var med = { medId: MED_A_ID, name: 'A', taker: 'Self' };
    expect(new Date(buildPin(med, FIXED_TS, false).time).getTime() / 1000).toBe(FIXED_TS);
  });

  test('layout type is genericPin', function () {
    var med = { medId: MED_A_ID, name: 'A', taker: 'Self' };
    expect(buildPin(med, FIXED_TS, false).layout.type).toBe('genericPin');
  });

  test('has one Open in Pebble Meds action that names the dose', function () {
    var med = { medId: MED_A_ID, name: 'A', taker: 'Self' };
    var actions = buildPin(med, FIXED_TS, false, 3).actions;
    expect(actions).toHaveLength(1);
    var minutes = Math.floor((FIXED_TS - 1672531200) / 60);
    expect(actions[0].title).toBe('Open in Pebble Meds');
    expect(actions[0].type).toBe('openWatchApp');
    expect(actions[0].launchCode).toBe(minutes * 64 + 3 * 4 + 1);
  });

});

// ---------------------------------------------------------------------------
// pushTimelinePins — medId-keyed pin map, migration, deletion
// ---------------------------------------------------------------------------
describe('pushTimelinePins', function () {

  beforeEach(function () {
    storedItems = {};
    schedule.getNextDoseTimes.mockReturnValue([]);
    Pebble.insertTimelinePin.mockClear();
    Pebble.deleteTimelinePin.mockClear();
    global.localStorage.setItem.mockClear();
  });

  test('deletes old pins when cfg is null', function () {
    pushTimelinePins(null);
    expect(Pebble.insertTimelinePin).not.toHaveBeenCalled();
  });

  test('deletes old pins when meds array is empty', function () {
    pushTimelinePins({ meds: [], settings: {} });
    expect(Pebble.insertTimelinePin).not.toHaveBeenCalled();
  });

  test('stores pin id map keyed by medId, not array index', function () {
    var cfg = {
      meds: [
        { medId: MED_A_ID, name: 'Aspirin', taker: 'Alice', scheduleType: 'fixed', times: [{ h: 8, m: 0 }] },
        { medId: MED_B_ID, name: 'Ibuprofen', taker: 'Bob', scheduleType: 'fixed', times: [{ h: 9, m: 0 }] },
      ],
      settings: {},
    };
    schedule.getNextDoseTimes
      .mockReturnValueOnce([FIXED_TS + 100])
      .mockReturnValueOnce([FIXED_TS + 200]);

    pushTimelinePins(cfg);

    var raw = storedItems['pebble_meds_timeline_pin_ids'];
    var pinMap = JSON.parse(raw);
    expect(pinMap[MED_A_ID]).toBeDefined();
    expect(pinMap[MED_B_ID]).toBeDefined();
    expect(pinMap['0']).toBeUndefined();
    expect(pinMap['1']).toBeUndefined();
    expect(pinMap[MED_A_ID][0]).toContain('pebble-meds-' + MED_A_ID + '-');
    expect(pinMap[MED_B_ID][0]).toContain('pebble-meds-' + MED_B_ID + '-');
  });

  test('medId migration: assigns medId to med without one and persists to localStorage', function () {
    var cfg = {
      meds: [
        { name: 'Tylenol', taker: 'Carol', scheduleType: 'fixed', times: [{ h: 12, m: 0 }] },
      ],
      settings: {},
    };
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS + 300]);

    pushTimelinePins(cfg);

    expect(cfg.meds[0].medId).toBeDefined();
    expect(typeof cfg.meds[0].medId).toBe('string');
    expect(cfg.meds[0].medId.length).toBeGreaterThan(0);

    var savedConfig = JSON.parse(storedItems['pebble_meds_config']);
    expect(savedConfig.meds[0].medId).toBe(cfg.meds[0].medId);
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

    expect(cfg.meds[0].medId).toBe('existing-1');
  });

  test('second push with same medId produces identical pin ids', function () {
    var cfg = {
      meds: [
        { medId: 'stable-1', name: 'Aspirin', taker: 'Alice', scheduleType: 'fixed', times: [{ h: 8, m: 0 }] },
      ],
      settings: {},
    };
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS + 400]);

    pushTimelinePins(cfg);
    var map1 = JSON.parse(storedItems['pebble_meds_timeline_pin_ids']);

    pushTimelinePins(cfg);
    var map2 = JSON.parse(storedItems['pebble_meds_timeline_pin_ids']);

    expect(map1['stable-1'][0]).toBe(map2['stable-1'][0]);
  });

  test('deletion: deleting middle med leaves no orphaned pins', function () {
    var cfg = {
      meds: [
        { medId: MED_A_ID, name: 'Aspirin',   taker: 'Alice', scheduleType: 'fixed', times: [{ h: 8, m: 0 }] },
        { medId: MED_B_ID, name: 'Ibuprofen', taker: 'Bob',   scheduleType: 'fixed', times: [{ h: 9, m: 0 }] },
        { medId: 'med-tylenol', name: 'Tylenol', taker: 'Carol', scheduleType: 'fixed', times: [{ h: 10, m: 0 }] },
      ],
      settings: {},
    };
    schedule.getNextDoseTimes
      .mockReturnValueOnce([FIXED_TS + 10])
      .mockReturnValueOnce([FIXED_TS + 20])
      .mockReturnValueOnce([FIXED_TS + 30]);

    pushTimelinePins(cfg);

    var map3 = JSON.parse(storedItems['pebble_meds_timeline_pin_ids']);
    expect(Object.keys(map3)).toHaveLength(3);
    expect(map3[MED_A_ID]).toHaveLength(1);
    expect(map3[MED_B_ID]).toHaveLength(1);
    expect(map3['med-tylenol']).toHaveLength(1);

    cfg.meds = [
      { medId: MED_A_ID, name: 'Aspirin', taker: 'Alice', scheduleType: 'fixed', times: [{ h: 8, m: 0 }] },
      { medId: 'med-tylenol', name: 'Tylenol', taker: 'Carol', scheduleType: 'fixed', times: [{ h: 10, m: 0 }] },
    ];
    schedule.getNextDoseTimes
      .mockReturnValueOnce([FIXED_TS + 10])
      .mockReturnValueOnce([FIXED_TS + 30]);

    pushTimelinePins(cfg);

    var map4 = JSON.parse(storedItems['pebble_meds_timeline_pin_ids']);
    expect(Object.keys(map4)).toHaveLength(2);
    expect(map4[MED_A_ID]).toBeDefined();
    expect(map4[MED_B_ID]).toBeUndefined();
    expect(map4['med-tylenol']).toBeDefined();
  });

  test('deletes old pins via deleteTimelinePin before inserting new ones', function () {
    var cfg = {
      meds: [
        { medId: MED_A_ID, name: 'Aspirin', taker: 'Alice', scheduleType: 'fixed', times: [{ h: 8, m: 0 }] },
      ],
      settings: {},
    };
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS + 10]);

    pushTimelinePins(cfg);
    var firstPinId = JSON.parse(storedItems['pebble_meds_timeline_pin_ids'])[MED_A_ID][0];

    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS + 20]);
    pushTimelinePins(cfg);

    expect(Pebble.deleteTimelinePin).toHaveBeenCalledWith(firstPinId);
  });

  test('passes privacy mode through to pin titles', function () {
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS]);
    var cfg = {
      meds: [{ medId: MED_A_ID, name: 'SecretMed', taker: 'Self', dose: '' }],
      settings: { privacyMode: true },
    };
    pushTimelinePins(cfg);
    var pinArg = JSON.parse(Pebble.insertTimelinePin.mock.calls[0][0]);
    expect(pinArg.layout.title).toBe('Medication Due');
  });

  test('handles missing settings gracefully (no crash)', function () {
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS]);
    var cfg = { meds: [{ medId: MED_A_ID, name: 'A', taker: 'Self', dose: '' }] };
    expect(function () { pushTimelinePins(cfg); }).not.toThrow();
  });

});

// ---------------------------------------------------------------------------
// launchCode: action, med index and dose minute packed for the watch
// ---------------------------------------------------------------------------

describe('launchCode', function () {
  var launchCode = timeline.launchCode;

  test('packs the fields so the watch can unpack them', function () {
    var ts = 1794700800;  // 2026-11-15 00:00 UTC
    var code = launchCode(2, 15, ts);
    expect(code & 3).toBe(2);
    expect((code >>> 2) & 0xF).toBe(15);
    expect(1672531200 + (code >>> 6) * 60).toBe(ts);
  });

  test('stays below 2^31 for decades', function () {
    var ts = Date.UTC(2080, 0, 1) / 1000;
    expect(launchCode(2, 15, ts)).toBeLessThan(Math.pow(2, 31));
  });

  test('is never confused with the old fixed codes 1 and 2', function () {
    expect(launchCode(1, 0, FIXED_TS)).toBeGreaterThan(3);
  });
});

describe('pushTimelinePins answered doses', function () {
  test('uses the dose log to show answered doses', function () {
    storedItems = {
      pebble_meds_log: JSON.stringify([
        { medIndex: 0, ts: FIXED_TS, status: 'skipped' },
        { medIndex: 0, ts: FIXED_TS + 3600, status: 'snooze' },
      ]),
    };
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS, FIXED_TS + 3600]);
    Pebble.insertTimelinePin.mockClear();
    pushTimelinePins({ meds: [{ medId: MED_A_ID, name: 'A', taker: 'Self' }], settings: {} });
    var pins = Pebble.insertTimelinePin.mock.calls.map(function (c) { return JSON.parse(c[0]); });
    expect(pins[0].layout.subtitle).toBe('Skipped');
    expect(pins[1].layout.subtitle).toBeUndefined();
  });
});

describe('buildPin answered', function () {
  var med = { medId: MED_A_ID, name: 'A', taker: 'Self' };

  test('a taken dose shows Taken and keeps its action', function () {
    var pin = buildPin(med, FIXED_TS, false, 0, 'taken');
    expect(pin.layout.subtitle).toBe('Taken');
    expect(pin.actions).toHaveLength(1);
  });

  test('a skipped dose shows Skipped', function () {
    expect(buildPin(med, FIXED_TS, false, 0, 'skipped').layout.subtitle).toBe('Skipped');
  });
});
