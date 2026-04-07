// PebbleMeds — Timeline pin management
//
// Builds and inserts Timeline pins for the next 48 hours using the
// Rebble app's local Pebble.insertTimelinePin() API (added v1.0.6.8).
// Remote timeline pins are not yet supported on this platform; getAccountToken
// and getWatchToken are not implemented.

'use strict';

var schedule = require('./schedule');

function pushTimelinePins(cfg) {
  if (!cfg || !cfg.meds || !cfg.meds.length) {
    console.log('Timeline: no meds in config, skipping');
    return;
  }

  var now         = Math.floor(Date.now() / 1000);
  var horizon     = now + 48 * 3600;
  var privacyMode = cfg.settings && cfg.settings.privacyMode;
  var pins        = [];

  cfg.meds.forEach(function (med, index) {
    var doseTimes = schedule.getNextDoseTimes(med, now, horizon);
    console.log('Timeline: med ' + index + ' (' + med.name + ') has ' + doseTimes.length + ' doses in 48h');
    doseTimes.forEach(function (ts) {
      pins.push(buildPin(med, index, ts, privacyMode));
    });
  });

  console.log('Timeline: inserting ' + pins.length + ' pin(s)');
  pins.forEach(function (pin) { insertTimelinePin(pin); });
}

function buildPin(med, index, ts, privacyMode) {
  var title = privacyMode ? 'Medication Due' : med.name;
  var body  = privacyMode
    ? med.taker
    : (med.taker + (med.dose ? ' \u2014 ' + med.dose : ''));
  return {
    id: 'pebble-meds-' + index + '-' + ts,
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

module.exports = {
  pushTimelinePins: pushTimelinePins,
  buildPin:         buildPin,
};
