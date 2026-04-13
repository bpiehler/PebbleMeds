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
// AppMessage key IDs (automatically mapped from package.json)
// ---------------------------------------------------------------------------
var keys = Pebble.MessageKeys;
var KEY_CONFIG_JSON  = keys.ConfigJson;
var KEY_CHUNK_INDEX  = keys.ChunkIndex;
var KEY_CHUNK_TOTAL  = keys.ChunkTotal;
var KEY_ACTION       = keys.Action;
var KEY_MED_INDEX    = keys.MedIndex;
var KEY_DOSE_TS      = keys.DoseTs;
var KEY_REQUEST_SYNC = keys.RequestSync;

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
// Timeline pins
// ---------------------------------------------------------------------------
var pushTimelinePins = require('./timeline').pushTimelinePins;

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
