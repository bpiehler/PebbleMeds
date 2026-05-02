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
// AppMessage key IDs — must match the values in build/js/message_keys.json.
// We use extremely defensive lookups to prevent initialization crashes.
// ---------------------------------------------------------------------------
var KEY_CONFIG_JSON  = 10000;
var KEY_CHUNK_INDEX  = 10001;
var KEY_CHUNK_TOTAL  = 10002;
var KEY_ACTION       = 10003;
var KEY_MED_INDEX    = 10004;
var KEY_DOSE_TS      = 10005;
var KEY_REQUEST_SYNC = 10006;

try {
  if (typeof Pebble !== 'undefined' && Pebble.MessageKey) {
    KEY_CONFIG_JSON  = Pebble.MessageKey.ConfigJson  || KEY_CONFIG_JSON;
    KEY_CHUNK_INDEX  = Pebble.MessageKey.ChunkIndex  || KEY_CHUNK_INDEX;
    KEY_CHUNK_TOTAL  = Pebble.MessageKey.ChunkTotal  || KEY_CHUNK_TOTAL;
    KEY_ACTION       = Pebble.MessageKey.Action       || KEY_ACTION;
    KEY_MED_INDEX    = Pebble.MessageKey.MedIndex    || KEY_MED_INDEX;
    KEY_DOSE_TS      = Pebble.MessageKey.DoseTs      || KEY_DOSE_TS;
    KEY_REQUEST_SYNC = Pebble.MessageKey.RequestSync || KEY_REQUEST_SYNC;
  }
} catch (e) {
  console.log('Error initializing message keys: ' + e.message);
}

// Max bytes per AppMessage chunk.
var CHUNK_SIZE = 200;

// ---------------------------------------------------------------------------
// Persistence (localStorage)
// ---------------------------------------------------------------------------
function saveConfig(cfg) {
  try {
    if (typeof localStorage !== 'undefined' && localStorage.setItem) {
      localStorage.setItem('pebble_meds_config', JSON.stringify(cfg));
    }
  } catch (e) {
    console.log('localStorage: saveConfig failed: ' + e.message);
  }
}

function loadConfig() {
  try {
    if (typeof localStorage !== 'undefined' && localStorage.getItem) {
      var raw = localStorage.getItem('pebble_meds_config');
      if (!raw) return null;
      return JSON.parse(raw);
    }
  } catch (e) {
    console.log('localStorage: loadConfig failed: ' + e.message);
  }
  return null;
}

function logDoseAction(action, medIndex, ts) {
  var key = 'pebble_meds_log';
  var log = [];
  try {
    if (typeof localStorage !== 'undefined') {
      var raw = localStorage.getItem(key);
      if (raw) { try { log = JSON.parse(raw); } catch (e) {} }
      log.push({ medIndex: medIndex, ts: ts, status: action });
      if (log.length > 500) log = log.slice(log.length - 500);
      localStorage.setItem(key, JSON.stringify(log));
    }
  } catch (e) {
    console.log('localStorage: logDoseAction failed: ' + e.message);
  }
}

// ---------------------------------------------------------------------------
// Configuration page URL
// ---------------------------------------------------------------------------
var CONFIG_URL = 'https://bpiehler.github.io/PebbleMeds/src/pkjs/config.html';

function getConfigUrl() {
  try {
    var cfg = loadConfig();
    if (cfg) {
      return CONFIG_URL + '#' + encodeURIComponent(JSON.stringify(cfg));
    }
  } catch (e) {
    console.log('Error building config URL: ' + e.message);
  }
  return CONFIG_URL;
}

// ---------------------------------------------------------------------------
// AppMessage: send config to watch (chunked)
// ---------------------------------------------------------------------------
function sendConfigToWatch(cfg) {
  try {
    var json = JSON.stringify(cfg);
    var chunks = [];
    for (var i = 0; i < json.length; i += CHUNK_SIZE) {
      chunks.push(json.slice(i, i + CHUNK_SIZE));
    }

    var total = chunks.length;
    console.log('Sending config in ' + total + ' chunk(s), total ' + json.length + ' bytes');

    var sendChunk = function(idx) {
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
    };

    sendChunk(0);
  } catch (e) {
    console.log('sendConfigToWatch failed: ' + e.message);
  }
}

// ---------------------------------------------------------------------------
// App Events
// ---------------------------------------------------------------------------
Pebble.addEventListener('ready', function () {
  console.log('PebbleMeds JS ready');
  try {
    var cfg = loadConfig();
    if (cfg) {
      sendConfigToWatch(cfg);
      var pushTimelinePins = require('./timeline').pushTimelinePins;
      if (typeof pushTimelinePins === 'function') {
        pushTimelinePins(cfg);
      }
    }
  } catch (e) {
    console.log('Ready handler failed: ' + e.message);
  }
});

Pebble.addEventListener('showConfiguration', function () {
  console.log('showConfiguration called');
  try {
    var url = getConfigUrl();
    console.log('Opening config URL: ' + url);
    Pebble.openURL(url);
  } catch (e) {
    console.log('Failed to open configuration: ' + e.message);
  }
});

Pebble.addEventListener('webviewclosed', function (e) {
  console.log('webviewclosed called');
  if (!e.response || e.response === 'CANCELLED') return;

  var cfg;
  try {
    var response = e.response;
    if (typeof response === 'string' && response.indexOf('%') !== -1) {
      response = decodeURIComponent(response);
    }
    cfg = (typeof response === 'string') ? JSON.parse(response) : response;
  } catch (err) {
    console.log('Failed to parse config response: ' + err);
    return;
  }

  if (cfg) {
    saveConfig(cfg);
    sendConfigToWatch(cfg);
    try {
      var pushTimelinePins = require('./timeline').pushTimelinePins;
      if (typeof pushTimelinePins === 'function') {
        pushTimelinePins(cfg);
      }
    } catch (e) {
      console.log('Timeline: pushTimelinePins failed: ' + e.message);
    }
  }
});

Pebble.addEventListener('appmessage', function (e) {
  try {
    var msg = e.payload;
    if (msg.hasOwnProperty(KEY_REQUEST_SYNC)) {
      var cfg = loadConfig();
      if (cfg) sendConfigToWatch(cfg);
      return;
    }

    if (msg.hasOwnProperty(KEY_ACTION)) {
      var action   = msg[KEY_ACTION];
      var medIndex = msg[KEY_MED_INDEX];
      var doseTs   = msg[KEY_DOSE_TS] || Math.floor(Date.now() / 1000);
      logDoseAction(action, medIndex, doseTs);

      if (action === 'taken') {
        var cfg = loadConfig();
        if (cfg && cfg.meds[medIndex] && cfg.meds[medIndex].scheduleType === 'interval') {
          cfg.meds[medIndex].lastTakenTs = doseTs;
          saveConfig(cfg);
          sendConfigToWatch(cfg);
          var pushTimelinePins = require('./timeline').pushTimelinePins;
          if (typeof pushTimelinePins === 'function') pushTimelinePins(cfg);
        }
      }
    }
  } catch (e) {
    console.log('AppMessage handler failed: ' + e.message);
  }
});
