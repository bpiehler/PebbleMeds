// PebbleMeds — Timeline pin management
//
// Builds and inserts Timeline pins for the next 48 hours using the
// Rebble app's local Pebble.insertTimelinePin() API (added v1.0.6.8).
// Remote timeline pins are not yet supported on this platform; getAccountToken
// and getWatchToken are not implemented.
//
// Old pins are deleted via Pebble.deleteTimelinePin() before inserting
// new ones, preventing stale pins from persisting when meds are removed.

'use strict';

var schedule = require('./schedule');

function uuidv4() {
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
    var r = Math.random() * 16 | 0, v = c === 'x' ? r : (r & 0x3 | 0x8);
    return v.toString(16);
  });
}

var TIMELINE_PIN_IDS_KEY = 'pebble_meds_timeline_pin_ids';

function pushTimelinePins(cfg) {
  if (!cfg || !cfg.meds || !cfg.meds.length) {
    console.log('Timeline: no meds in config, deleting any existing pins');
    deleteOldTimelinePins();
    return;
  }

  var now         = Math.floor(Date.now() / 1000);
  var horizon     = now + 48 * 3600;
  var privacyMode = cfg.settings && cfg.settings.privacyMode;

  var needsMigration = false;
  for (var mi = 0; mi < cfg.meds.length; mi++) {
    if (!cfg.meds[mi].medId) {
      cfg.meds[mi].medId = uuidv4();
      needsMigration = true;
    }
  }
  if (needsMigration) {
    console.log('Timeline: assigned medId to ' + cfg.meds.length + ' med(s)');
    try {
      localStorage.setItem('pebble_meds_config', JSON.stringify(cfg));
    } catch (e) {
      console.log('Timeline: failed to persist medId migration: ' + e.message);
    }
  }

  var pins        = [];
  var newPinIdMap = {};

  cfg.meds.forEach(function (med, index) {
    var doseTimes = schedule.getNextDoseTimes(med, now, horizon);
    console.log('Timeline: med ' + index + ' (' + med.name + ') [' + med.medId + '] has ' + doseTimes.length + ' doses in 48h');
    var medId = med.medId;
    doseTimes.forEach(function (ts) {
      var pin = buildPin(med, ts, privacyMode);
      pins.push(pin);
      if (!newPinIdMap[medId]) newPinIdMap[medId] = [];
      newPinIdMap[medId].push(pin.id);
    });
  });

  console.log('Timeline: deleting old pins');
  deleteOldTimelinePins();

  console.log('Timeline: inserting ' + pins.length + ' pin(s)');
  pins.forEach(function (pin) { insertTimelinePin(pin); });

  storePinIdMap(newPinIdMap);
}

function buildPin(med, ts, privacyMode) {
  var medId = med.medId || uuidv4();
  var title = privacyMode ? 'Medication Due' : med.name;
  var body  = privacyMode
    ? med.taker
    : (med.taker + (med.dose ? ' \u2014 ' + med.dose : ''));
  return {
    id: 'pebble-meds-' + medId + '-' + ts,
    time: new Date(ts * 1000).toISOString(),
    layout: {
      type: 'genericPin',
      title: title,
      body: body,
      tinyIcon: 'system://images/NOTIFICATION_REMINDER',
    },
    actions: [
      { title: 'Taken',  type: 'openWatchApp', launchCode: 1 },
      { title: 'Snooze', type: 'openWatchApp', launchCode: 2 },
    ],
  };
}

function insertTimelinePin(pin) {
  try {
    Pebble.insertTimelinePin(
      JSON.stringify(pin),
      function ()  { console.log('Timeline: pin inserted OK — ' + pin.id); },
      function (e) { console.log('Timeline: pin insert FAILED — ' + pin.id + ': ' + e); }
    );
  } catch (e) {
    console.log('Timeline: Pebble.insertTimelinePin unavailable: ' + e.message);
  }
}

function deleteOldTimelinePins() {
  var oldPinIdMap = getStoredPinIdMap();
  var oldKeys = Object.keys(oldPinIdMap);
  if (oldKeys.length === 0) {
    return;
  }
  var totalPins = 0;
  for (var i = 0; i < oldKeys.length; i++) {
    totalPins += oldPinIdMap[oldKeys[i]].length;
  }
  console.log('Timeline: deleting ' + totalPins + ' old pin(s)');
  if (typeof Pebble.deleteTimelinePin === 'function') {
    for (var j = 0; j < oldKeys.length; j++) {
      var ids = oldPinIdMap[oldKeys[j]];
      ids.forEach(function (id) {
        try {
          Pebble.deleteTimelinePin(id);
          console.log('Timeline: deleted pin ' + id);
        } catch (e) {
          console.log('Timeline: failed to delete pin ' + id + ': ' + e);
        }
      });
    }
  } else {
    console.log('Timeline: Pebble.deleteTimelinePin not available, skipping deletion');
  }
  storePinIdMap({});
}

function getStoredPinIdMap() {
  try {
    var raw = localStorage.getItem(TIMELINE_PIN_IDS_KEY);
    if (!raw) return {};
    return JSON.parse(raw);
  } catch (e) {
    return {};
  }
}

function storePinIdMap(pinIdMap) {
  try {
    localStorage.setItem(TIMELINE_PIN_IDS_KEY, JSON.stringify(pinIdMap));
  } catch (e) {
    console.log('Timeline: failed to store pin id map: ' + e.message);
  }
}

module.exports = {
  pushTimelinePins: pushTimelinePins,
  buildPin:         buildPin,
};
