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
    pushTimelinePins(cfg);
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

  function sendChunk(index) {
    if (index >= total) {
      console.log('Config send complete');
      return;
    }
    Pebble.sendAppMessage(
      {
        KEY_CONFIG_JSON: chunks[index],
        KEY_CHUNK_INDEX: index,
        KEY_CHUNK_TOTAL: total
      },
      function () { sendChunk(index + 1); },
      function (err) { console.log('Chunk ' + index + ' failed: ' + JSON.stringify(err)); }
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
// Timeline pins
// ---------------------------------------------------------------------------
function pushTimelinePins(cfg) {
  var now = Math.floor(Date.now() / 1000);
  var horizon = now + 48 * 3600;
  var pins = [];

  cfg.meds.forEach(function (med, index) {
    var doseTimes = getNextDoseTimes(med, now, horizon);
    doseTimes.forEach(function (ts) {
      pins.push(buildPin(med, index, ts, cfg.settings.privacyMode));
    });
  });

  pins.forEach(function (pin) {
    Pebble.timelineSubscribe(
      pin.id, function () {}, function (err) {
        console.log('Timeline subscribe error: ' + err);
      }
    );
    // The Timeline API requires a PUT to the Pebble timeline service.
    // This is handled via the Pebble.getAccountToken() + REST API.
    insertTimelinePin(pin);
  });
}

function buildPin(med, index, ts, privacyMode) {
  var title = privacyMode ? 'Medication Due' : med.name;
  var body  = privacyMode ? med.taker : (med.taker + (med.dose ? ' — ' + med.dose : ''));
  return {
    id: 'pebble-meds-' + index + '-' + ts,
    time: new Date(ts * 1000).toISOString(),
    layout: {
      type: 'genericPin',
      title: title,
      body: body,
      tinyIcon: 'system://images/GENERIC_WARNING'
    },
    actions: [
      { title: 'Taken', type: 'openWatchApp', launchCode: 1 },
      { title: 'Snooze', type: 'openWatchApp', launchCode: 2 }
    ]
  };
}

function insertTimelinePin(pin) {
  Pebble.getAccountToken(function (token) {
    if (!token) { console.log('No account token, skipping Timeline pin'); return; }

    var xhr = new XMLHttpRequest();
    var url = 'https://timeline-api.getpebble.com/v1/user/pins/' + pin.id;
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

// ---------------------------------------------------------------------------
// Dose schedule calculation
// ---------------------------------------------------------------------------
function getNextDoseTimes(med, fromTs, toTs) {
  var times = [];
  if (med.scheduleType === 'fixed') {
    times = getFixedTimes(med, fromTs, toTs);
  } else {
    times = getIntervalTimes(med, fromTs, toTs);
  }
  return times;
}

function getFixedTimes(med, fromTs, toTs) {
  var results = [];
  var date = new Date(fromTs * 1000);
  // Check today + tomorrow to cover the 48h window
  for (var dayOffset = 0; dayOffset <= 2; dayOffset++) {
    var d = new Date(date.getFullYear(), date.getMonth(), date.getDate() + dayOffset);
    med.times.forEach(function (t) {
      var ts = Math.floor(new Date(d.getFullYear(), d.getMonth(), d.getDate(), t.h, t.m, 0).getTime() / 1000);
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
    // First run: use startHour/startMinute today
    var now = new Date(fromTs * 1000);
    base = Math.floor(new Date(now.getFullYear(), now.getMonth(), now.getDate(),
                                med.startHour, med.startMinute, 0).getTime() / 1000);
    if (base < fromTs) base += intervalSecs;
  }
  var ts = base;
  while (ts <= toTs) {
    if (ts >= fromTs) results.push(ts);
    ts += intervalSecs;
  }
  return results;
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
