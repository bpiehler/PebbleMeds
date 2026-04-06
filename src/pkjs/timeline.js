// PebbleMeds — Timeline pin management
//
// Responsible for building and pushing Timeline pins for the next 48 hours
// via the Rebble Timeline API.

'use strict';

var schedule = require('./schedule');

function pushTimelinePins(cfg) {
  var now     = Math.floor(Date.now() / 1000);
  var horizon = now + 48 * 3600;
  var pins    = [];

  cfg.meds.forEach(function (med, index) {
    var doseTimes = schedule.getNextDoseTimes(med, now, horizon);
    doseTimes.forEach(function (ts) {
      pins.push(buildPin(med, index, ts, cfg.settings.privacyMode));
    });
  });

  pins.forEach(function (pin) {
    insertTimelinePin(pin);
  });
}

function buildPin(med, index, ts, privacyMode) {
  var title = privacyMode ? 'Medication Due' : med.name;
  var body  = privacyMode ? med.taker : (med.taker + (med.dose ? ' \u2014 ' + med.dose : ''));
  return {
    id: 'pebble-meds-' + index + '-' + ts,
    time: new Date(ts * 1000).toISOString(),
    layout: {
      type: 'genericPin',
      title: title,
      body: body,
      tinyIcon: 'system://images/NOTIFICATION_REMINDER'
    },
    actions: [
      { title: 'Taken',  type: 'openWatchApp', launchCode: 1 },
      { title: 'Snooze', type: 'openWatchApp', launchCode: 2 }
    ]
  };
}

function insertTimelinePin(pin) {
  Pebble.getAccountToken(function (token) {
    if (!token) { console.log('No account token, skipping Timeline pin'); return; }

    var xhr = new XMLHttpRequest();
    var url = 'https://timeline-api.rebble.io/v1/user/pins/' + pin.id;
    xhr.open('PUT', url, true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.setRequestHeader('X-User-Token', token);
    xhr.onload = function () {
      if (xhr.status >= 200 && xhr.status < 300) {
        console.log('Pin inserted: ' + pin.id);
      } else {
        console.log('Pin insert failed: ' + xhr.status + ' ' + xhr.responseText);
      }
    };
    xhr.send(JSON.stringify(pin));
  });
}

module.exports = { pushTimelinePins: pushTimelinePins };
