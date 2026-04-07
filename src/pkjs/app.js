// PebbleMeds — PebbleKit JS entry point
//
// Responsibilities:
//   - Launch the HTML config page when the user opens settings
//   - Receive config from the page and forward it to the watch via AppMessage
//   - Receive action events from the watch (Taken/Skipped/Snooze) and log them
//   - Periodically push Timeline pins for the next 48 hours
//   - Sync medication list to watch on app launch

'use strict';

// ---------------------------------------------------------------------------
// AppMessage key IDs (must match package.json appKeys)
// ---------------------------------------------------------------------------
var KEY_CONFIG_JSON  = 0;
var KEY_CHUNK_INDEX  = 1;
var KEY_CHUNK_TOTAL  = 2;
var KEY_ACTION       = 3;
var KEY_MED_INDEX    = 4;
var KEY_DOSE_TS      = 5;
var KEY_REQUEST_SYNC = 6;

// Max bytes per AppMessage chunk. Pebble's buffer is 8KB but we stay
// conservative so the watch-side reassembly buffer stays small.
var CHUNK_SIZE = 200;

// ---------------------------------------------------------------------------
// App ready
// ---------------------------------------------------------------------------
Pebble.addEventListener('ready', function () {
  console.log('PebbleMeds JS ready');

  // Send any cached config to the watch on launch.
  var cfg = loadConfig();
  if (cfg) {
    sendConfigToWatch(cfg);
    try { pushTimelinePins(cfg); } catch (e) {
      console.log('Timeline: pushTimelinePins failed: ' + e.message);
    }
  }
});

// ---------------------------------------------------------------------------
// Configuration page
// ---------------------------------------------------------------------------

// Build the config page URL. We load config.html from the same directory
// and encode it as a data URI so the app remains fully self-contained.
// The HTML file is kept in src/pkjs/config.html for maintainability;
// the Pebble build system bundles everything under src/pkjs/ together.
var CONFIG_URL = 'https://bpiehler.github.io/PebbleMeds/src/pkjs/config.html';

function getConfigUrl() {
  var cfg = loadConfig();
  if (cfg) {
    return CONFIG_URL + '#' + encodeURIComponent(JSON.stringify(cfg));
  }
  return CONFIG_URL;
}

Pebble.addEventListener('showConfiguration', function () {
  var url = getConfigUrl();
  if (url) {
    Pebble.openURL(url);
  } else {
    console.log('Cannot open config: no URL available');
  }
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response || e.response === 'CANCELLED') return;

  var cfg;
  try {
    cfg = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    console.log('Failed to parse config response: ' + err);
    return;
  }

  saveConfig(cfg);
  sendConfigToWatch(cfg);
  pushTimelinePins(cfg);
});

// ---------------------------------------------------------------------------
// AppMessage: send config to watch (chunked)
// ---------------------------------------------------------------------------
function sendConfigToWatch(cfg) {
  var json = JSON.stringify(cfg);
  var chunks = [];
  for (var i = 0; i < json.length; i += CHUNK_SIZE) {
    chunks.push(json.slice(i, i + CHUNK_SIZE));
  }

  var total = chunks.length;
  console.log('Sending config in ' + total + ' chunk(s), total ' + json.length + ' bytes');

  function sendChunk(idx) {
    if (idx >= total) {
      console.log('Config send complete');
      return;
    }
    var msg = {};
    msg[KEY_CONFIG_JSON] = chunks[idx];
    msg[KEY_CHUNK_INDEX] = idx;
    msg[KEY_CHUNK_TOTAL] = total;
    Pebble.sendAppMessage(
      msg,
      function () { sendChunk(idx + 1); },
      function (err) { console.log('Chunk ' + idx + ' failed: ' + JSON.stringify(err)); }
    );
  }

  sendChunk(0);
}

// ---------------------------------------------------------------------------
// AppMessage: receive events from watch
// ---------------------------------------------------------------------------
Pebble.addEventListener('appmessage', function (e) {
  var msg = e.payload;
  console.log('Message from watch: ' + JSON.stringify(msg));

  if (msg.hasOwnProperty(KEY_REQUEST_SYNC)) {
    // Watch is requesting a fresh config (e.g. after a crash/restart)
    var cfg = loadConfig();
    if (cfg) sendConfigToWatch(cfg);
    return;
  }

  if (msg.hasOwnProperty(KEY_ACTION)) {
    var action   = msg[KEY_ACTION];    // "taken" | "skipped" | "snooze"
    var medIndex = msg[KEY_MED_INDEX];
    var doseTs   = msg[KEY_DOSE_TS] || Math.floor(Date.now() / 1000);

    logDoseAction(action, medIndex, doseTs);

    if (action === 'taken') {
      // Update lastTakenTs for interval medications so the next dose
      // is calculated from this moment.
      var cfg = loadConfig();
      if (cfg && cfg.meds[medIndex] && cfg.meds[medIndex].scheduleType === 'interval') {
        cfg.meds[medIndex].lastTakenTs = doseTs;
        saveConfig(cfg);
        // Resend updated config so watch recalculates next dose time.
        sendConfigToWatch(cfg);
        pushTimelinePins(cfg);
      }
    }
  }
});

// ---------------------------------------------------------------------------
// Schedule calculations (inlined from schedule.js — Pebble's CommonJS
// implementation does not reliably export from required modules at runtime;
// schedule.js is kept as the canonical source for Jest tests only)
// ---------------------------------------------------------------------------

function getFixedTimes(med, fromTs, toTs) {
  var results = [];
  var date = new Date(fromTs * 1000);
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

function getNextDoseTimes(med, fromTs, toTs) {
  return med.scheduleType === 'fixed'
    ? getFixedTimes(med, fromTs, toTs)
    : getIntervalTimes(med, fromTs, toTs);
}

// ---------------------------------------------------------------------------
// Timeline pins (inlined from timeline.js — same reason as above)
// ---------------------------------------------------------------------------

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

function getTokenAndInsertPin(pin, token) {
  if (!token) {
    console.log('Timeline: no token available, skipping pin ' + pin.id);
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
}

function insertTimelinePin(pin) {
  // Try getAccountToken first; fall back to getWatchToken if not available
  // (Rebble Android does not implement getAccountToken)
  try {
    Pebble.getAccountToken(function (token) {
      if (token) {
        console.log('Timeline: using account token');
        getTokenAndInsertPin(pin, token);
      } else {
        console.log('Timeline: getAccountToken returned null, trying getWatchToken');
        try {
          Pebble.getWatchToken(function (wtoken) {
            getTokenAndInsertPin(pin, wtoken);
          });
        } catch (e2) {
          console.log('Timeline: getWatchToken also unavailable: ' + e2.message);
        }
      }
    });
  } catch (e) {
    console.log('Timeline: getAccountToken unavailable (' + e.message + '), trying getWatchToken');
    try {
      Pebble.getWatchToken(function (wtoken) {
        getTokenAndInsertPin(pin, wtoken);
      });
    } catch (e2) {
      console.log('Timeline: getWatchToken also unavailable: ' + e2.message);
    }
  }
}

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
    var doseTimes = getNextDoseTimes(med, now, horizon);
    console.log('Timeline: med ' + index + ' (' + med.name + ') has ' + doseTimes.length + ' doses in 48h');
    doseTimes.forEach(function (ts) {
      pins.push(buildPin(med, index, ts, privacyMode));
    });
  });
  console.log('Timeline: inserting ' + pins.length + ' pin(s)');
  pins.forEach(function (pin) { insertTimelinePin(pin); });
}

// ---------------------------------------------------------------------------
// Persistence (localStorage)
// ---------------------------------------------------------------------------
function saveConfig(cfg) {
  localStorage.setItem('pebble_meds_config', JSON.stringify(cfg));
}

function loadConfig() {
  var raw = localStorage.getItem('pebble_meds_config');
  if (!raw) return null;
  try { return JSON.parse(raw); } catch (e) { return null; }
}

function logDoseAction(action, medIndex, ts) {
  var key = 'pebble_meds_log';
  var log = [];
  var raw = localStorage.getItem(key);
  if (raw) { try { log = JSON.parse(raw); } catch (e) {} }
  log.push({ medIndex: medIndex, ts: ts, status: action });
  // Keep the last 500 log entries to avoid unbounded growth.
  if (log.length > 500) log = log.slice(log.length - 500);
  localStorage.setItem(key, JSON.stringify(log));
}
