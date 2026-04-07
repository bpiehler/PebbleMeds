'use strict';

// Mock the Pebble global before requiring timeline (not available in Node.js)
global.Pebble = {
  insertTimelinePin: jest.fn(function (pinJson, success) { success(); }),
};

// Mock schedule so pushTimelinePins tests use controlled dose times
jest.mock('../src/pkjs/schedule', function () {
  return { getNextDoseTimes: jest.fn() };
});

var timeline       = require('../src/pkjs/timeline');
var schedule       = require('../src/pkjs/schedule');
var buildPin       = timeline.buildPin;
var pushTimelinePins = timeline.pushTimelinePins;

var FIXED_TS = 1700000000;  // arbitrary stable timestamp for pin structure tests

// ---------------------------------------------------------------------------
// buildPin
// ---------------------------------------------------------------------------
describe('buildPin', function () {

  var med = { name: 'Aspirin', taker: 'Self', dose: '100mg' };

  test('uses med name as title when privacy is off', function () {
    expect(buildPin(med, 0, FIXED_TS, false).layout.title).toBe('Aspirin');
  });

  test('uses "Medication Due" as title when privacy is on', function () {
    expect(buildPin(med, 0, FIXED_TS, true).layout.title).toBe('Medication Due');
  });

  test('body includes taker and dose when privacy is off', function () {
    var body = buildPin(med, 0, FIXED_TS, false).layout.body;
    expect(body).toContain('Self');
    expect(body).toContain('100mg');
  });

  test('body is taker only when privacy is on', function () {
    var body = buildPin(med, 0, FIXED_TS, true).layout.body;
    expect(body).toBe('Self');
    expect(body).not.toContain('Aspirin');
    expect(body).not.toContain('100mg');
  });

  test('body omits dose separator when dose is empty', function () {
    var noDosMed = { name: 'Aspirin', taker: 'Self', dose: '' };
    var body = buildPin(noDosMed, 0, FIXED_TS, false).layout.body;
    expect(body).toBe('Self');
  });

  test('body omits dose separator when dose is absent', function () {
    var noDosMed = { name: 'Aspirin', taker: 'Self' };
    var body = buildPin(noDosMed, 0, FIXED_TS, false).layout.body;
    expect(body).toBe('Self');
  });

  test('id encodes med index and timestamp', function () {
    expect(buildPin(med, 3, FIXED_TS, false).id).toBe('pebble-meds-3-' + FIXED_TS);
  });

  test('time is a valid ISO 8601 string matching the timestamp', function () {
    var pin = buildPin(med, 0, FIXED_TS, false);
    expect(new Date(pin.time).getTime() / 1000).toBe(FIXED_TS);
  });

  test('layout type is genericPin', function () {
    expect(buildPin(med, 0, FIXED_TS, false).layout.type).toBe('genericPin');
  });

  test('has Taken and Snooze actions with correct launchCodes', function () {
    var actions = buildPin(med, 0, FIXED_TS, false).actions;
    expect(actions).toHaveLength(2);
    var taken  = actions.find(function (a) { return a.title === 'Taken'; });
    var snooze = actions.find(function (a) { return a.title === 'Snooze'; });
    expect(taken.launchCode).toBe(1);
    expect(snooze.launchCode).toBe(2);
    expect(taken.type).toBe('openWatchApp');
    expect(snooze.type).toBe('openWatchApp');
  });

});

// ---------------------------------------------------------------------------
// pushTimelinePins
// ---------------------------------------------------------------------------
describe('pushTimelinePins', function () {

  beforeEach(function () {
    jest.clearAllMocks();
  });

  test('does nothing when cfg is null', function () {
    pushTimelinePins(null);
    expect(Pebble.insertTimelinePin).not.toHaveBeenCalled();
  });

  test('does nothing when meds array is empty', function () {
    pushTimelinePins({ meds: [], settings: {} });
    expect(Pebble.insertTimelinePin).not.toHaveBeenCalled();
  });

  test('inserts one pin per dose time returned by schedule', function () {
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS, FIXED_TS + 3600]);
    var cfg = { meds: [{ name: 'A', taker: 'Self', dose: '' }], settings: {} };
    pushTimelinePins(cfg);
    expect(Pebble.insertTimelinePin).toHaveBeenCalledTimes(2);
  });

  test('inserts pins for all meds combined', function () {
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS]);
    var cfg = {
      meds: [
        { name: 'A', taker: 'Self', dose: '' },
        { name: 'B', taker: 'Self', dose: '' },
      ],
      settings: {},
    };
    pushTimelinePins(cfg);
    expect(Pebble.insertTimelinePin).toHaveBeenCalledTimes(2);
  });

  test('passes privacy mode through to pin titles', function () {
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS]);
    var cfg = {
      meds: [{ name: 'SecretMed', taker: 'Self', dose: '' }],
      settings: { privacyMode: true },
    };
    pushTimelinePins(cfg);
    var pinArg = JSON.parse(Pebble.insertTimelinePin.mock.calls[0][0]);
    expect(pinArg.layout.title).toBe('Medication Due');
  });

  test('handles missing settings gracefully (no crash)', function () {
    schedule.getNextDoseTimes.mockReturnValue([FIXED_TS]);
    var cfg = { meds: [{ name: 'A', taker: 'Self', dose: '' }] };
    expect(function () { pushTimelinePins(cfg); }).not.toThrow();
  });

});
