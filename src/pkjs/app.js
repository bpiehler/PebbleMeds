// PebbleMeds — PebbleKit JS entry point
//
// Debug-heavy version to diagnose configuration launch failure.

'use strict';

console.log('--- PEBBLEMEDS JS BOOTING ---');

var CONFIG_URL = 'https://bpiehler.github.io/PebbleMeds/src/pkjs/config.html';

// AppMessage key IDs - extremely defensive
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
    console.log('Keys resolved: ' + KEY_CONFIG_JSON + ', ' + KEY_ACTION);
  } else {
    console.log('Pebble.MessageKey not available, using defaults (10000+)');
  }
} catch (e) {
  console.log('CRITICAL: Key init error: ' + e.message);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function loadConfig() {
  console.log('loadConfig() called');
  try {
    if (typeof localStorage !== 'undefined' && localStorage.getItem) {
      var raw = localStorage.getItem('pebble_meds_config');
      console.log('localStorage raw: ' + (raw ? raw.length + ' bytes' : 'null'));
      if (!raw) return null;
      return JSON.parse(raw);
    }
    console.log('localStorage not available');
  } catch (e) {
    console.log('loadConfig error: ' + e.message);
  }
  return null;
}

function getConfigUrl() {
  console.log('getConfigUrl() called');
  try {
    var cfg = loadConfig();
    if (cfg) {
      var url = CONFIG_URL + '#' + encodeURIComponent(JSON.stringify(cfg));
      console.log('Built URL with config hash (' + url.length + ' chars)');
      return url;
    }
  } catch (e) {
    console.log('getConfigUrl error: ' + e.message);
  }
  console.log('Using base CONFIG_URL');
  return CONFIG_URL;
}

// ---------------------------------------------------------------------------
// Event Listeners
// ---------------------------------------------------------------------------

console.log('Registering event listeners...');

Pebble.addEventListener('ready', function (e) {
  console.log('Pebble.ready event received');
  try {
    // Send a "ping" to the watch so we know JS is alive
    var msg = {};
    msg[KEY_REQUEST_SYNC] = 1;
    Pebble.sendAppMessage(msg, 
      function() { console.log('Ready ping sent to watch'); },
      function(err) { console.log('Ready ping failed: ' + JSON.stringify(err)); }
    );

    var cfg = loadConfig();
    if (cfg) {
      console.log('Found config, pushing to watch/timeline');
      sendConfigToWatch(cfg);
      var pushTimelinePins = require('./timeline').pushTimelinePins;
      pushTimelinePins(cfg);
    } else {
      console.log('No config found on ready');
    }
  } catch (err) {
    console.log('ready handler error: ' + err.message);
  }
});

Pebble.addEventListener('showConfiguration', function (e) {
  console.log('Pebble.showConfiguration event received!');
  try {
    var url = getConfigUrl();
    console.log('Calling Pebble.openURL("' + url + '")');
    Pebble.openURL(url);
  } catch (err) {
    console.log('showConfiguration error: ' + err.message);
  }
});

Pebble.addEventListener('webviewclosed', function (e) {
  console.log('Pebble.webviewclosed event received, response: ' + (e.response ? 'present' : 'empty'));
  if (!e.response || e.response === 'CANCELLED') return;

  try {
    var response = e.response;
    if (typeof response === 'string' && response.indexOf('%') !== -1) {
      response = decodeURIComponent(response);
    }
    var cfg = (typeof response === 'string') ? JSON.parse(response) : response;
    
    if (cfg) {
      console.log('New config received, saving...');
      localStorage.setItem('pebble_meds_config', JSON.stringify(cfg));
      sendConfigToWatch(cfg);
      var pushTimelinePins = require('./timeline').pushTimelinePins;
      pushTimelinePins(cfg);
    }
  } catch (err) {
    console.log('webviewclosed error: ' + err.message);
  }
});

Pebble.addEventListener('appmessage', function (e) {
  console.log('Pebble.appmessage event received');
  try {
    var msg = e.payload;
    if (msg.hasOwnProperty(KEY_ACTION)) {
      console.log('Action message: ' + msg[KEY_ACTION]);
      // ... logDoseAction etc (abbreviated for debug trace)
    }
  } catch (err) {
    console.log('appmessage error: ' + err.message);
  }
});

console.log('--- PEBBLEMEDS JS INITIALIZED ---');

function sendConfigToWatch(cfg) {
  console.log('sendConfigToWatch() called');
  // ... (original implementation)
}
