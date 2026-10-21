'use strict';

var fs   = require('fs');
var path = require('path');
var page = require('../src/pkjs/config_page');

var ROOT = path.join(__dirname, '..');
var HTML = decodeURIComponent(page.HTML);

describe('bundled config page', function () {
  it('matches src/pkjs/config.html', function () {
    var source = fs.readFileSync(path.join(ROOT, 'src/pkjs/config.html'), 'utf8');
    expect(HTML).toBe(source);
  });

  it('has the keep-nagging and vibration strength controls', function () {
    expect(HTML).toContain('id="repeat-snooze"');
    expect(HTML).toContain('id="f-vibe-strength"');
    expect(HTML).toContain('repeatSnooze');
  });

  it('has no remote references, so no medication data leaves the device', function () {
    expect(HTML).not.toMatch(/https?:\/\//);
  });

  it('never logs the config URL, which carries the medication list', function () {
    var source = fs.readFileSync(path.join(ROOT, 'src/pkjs/app.js'), 'utf8');
    expect(source).not.toContain('Opening config URL');
    expect(source).toMatch(/url\.length/);
  });

  it('reads config injected by app.js before falling back to the URL hash', function () {
    expect(HTML).toContain('window.PEBBLE_MEDS_CONFIG');
    var injected = HTML.indexOf('window.PEBBLE_MEDS_CONFIG');
    var hash     = HTML.indexOf('location.hash');
    expect(injected).toBeGreaterThan(-1);
    expect(injected).toBeLessThan(hash);
  });
});

function buildConfigUrl(config, opts) {
  var handlers = {};
  var urls = [];
  var store = { pebble_meds_config: JSON.stringify(config) };
  var saved = { Pebble: global.Pebble, localStorage: global.localStorage };

  global.Pebble = {
    addEventListener: function (name, fn) { handlers[name] = fn; },
    openURL: function (url) { urls.push(url); },
    sendAppMessage: function () {},
    showSimpleNotificationOnPebble: function () {}
  };
  global.localStorage = {
    getItem: function (k) { return Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null; },
    setItem: function (k, v) { store[k] = String(v); }
  };
  jest.resetModules();
  require('../src/pkjs/app.js');

  if (!handlers.showConfiguration) throw new Error('app.js did not register showConfiguration');
  handlers.showConfiguration();

  global.Pebble = saved.Pebble;
  global.localStorage = saved.localStorage;
  return urls[0];
}

function decodeDataUri(url) {
  var marker = ';base64,';
  var i = url.indexOf(marker);
  if (i !== -1) return Buffer.from(url.slice(i + marker.length), 'base64').toString('utf8');
  return decodeURIComponent(url.replace('data:text/html;charset=utf-8,', ''));
}

describe('config URL construction', function () {
  it('escapes a medication name containing </script> so it cannot break out', function () {
    var name = '</script><img src=x onerror=alert(1)>';
    var page = decodeDataUri(buildConfigUrl({ meds: [{ taker: 'Self', name: name }], settings: {} }));

    expect(page).not.toContain('</script><img');
    expect(page).toContain('\\u003c/script>');

    // the escaping must not corrupt the value: it still parses back to the name
    var m = page.match(/window\.PEBBLE_MEDS_CONFIG = ("(?:[^"\\]|\\.)*");/);
    expect(m).not.toBeNull();
    expect(JSON.parse(JSON.parse(m[1])).meds[0].name).toBe(name);
  });

  it('round-trips non-ASCII med names through the data URI encoding', function () {
    var name = 'Sebastián Müller 药';
    var url = buildConfigUrl({ meds: [{ taker: 'Self', name: name }], settings: {} });
    expect(url).toContain('charset=utf-8,');
    expect(decodeDataUri(url)).toContain(name);
  });
});
