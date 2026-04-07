// PebbleMeds — Timeline pin management
//
// Responsible for building and pushing Timeline pins for the next 48 hours
// via the Rebble Timeline API.

'use strict';

// Note: no require('./schedule') here — Pebble's JS runtime does not support
// transitive requires (require from within a required module). Instead,
// getNextDoseTimes is injected by index.js as a parameter.

function pushTimelinePins(cfg, getNextDoseTimes) {
  if (!cfg || !cfg.meds || !cfg.meds.length) {
    console.log('Timeline: no meds in config, skipping');
    return;
  }

  var now        = Math.floor(Date.now() / 1000);
  var horizon    = now + 48 * 3600;
  var privacyMode = cfg.settings && cfg.settings.privacyMode;
  var pins       = [];

  cfg.meds.forEach(function (med, index) {
    var doseTimes = getNextDoseTimes(med, now, horizon);
    console.log('Timeline: med ' + index + ' (' + med.name + ') has ' + doseTimes.length + ' doses in 48h');
    doseTimes.forEach(function (ts) {
      pins.push(buildPin(med, index, ts, privacyMode));
    });
  });

  console.log('Timeline: inserting ' + pins.length + ' pin(s)');
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
    if (!token) {
      console.log('Timeline: no account token — make sure Rebble account is connected');
      return;
    }
    console.log('Timeline: pushing pin ' + pin.id + ' at ' + pin.time);

    var xhr = new XMLHttpRequest();
    var url = 'https://timeline-api.rebble.io/v1/user/pins/' + pin.id;
    xhr.open('PUT', url, true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.setRequestHeader('X-User-Token', token);
    xhr.onload = function () {
      if (xhr.status >= 200 && xhr.status < 300) {
        console.log('Timeline: pin inserted OK — ' + pin.id);
      } else {
        console.log('Timeline: pin insert FAILED ' + xhr.status + ' — ' + xhr.responseText);
      }
    };
    xhr.onerror = function () {
      console.log('Timeline: network error inserting pin ' + pin.id);
    };
    xhr.send(JSON.stringify(pin));
  });
}

module.exports = { pushTimelinePins: pushTimelinePins };
