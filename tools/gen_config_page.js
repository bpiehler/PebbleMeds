// Generates src/pkjs/config_page.js from src/pkjs/config.html.
//
// The configuration page ships inside the app and is opened as a data: URI
// (the same trick Clay uses), so the med list never travels to a remote host.
// Re-run this after editing config.html:  node tools/gen_config_page.js
// tests/config_page.test.js fails if the generated copy is stale.
//
// The HTML is stored percent-encoded so it embeds in a JS string literal
// without any escaping of quotes, newlines or backslashes.

'use strict';

var fs   = require('fs');
var path = require('path');

var root = path.join(__dirname, '..');
var src  = path.join(root, 'src/pkjs/config.html');
var dst  = path.join(root, 'src/pkjs/config_page.js');

var html    = fs.readFileSync(src, 'utf8');
var encoded = encodeURIComponent(html);

var out =
  '// GENERATED FILE - do not edit. Source: src/pkjs/config.html\n' +
  '// Regenerate with: node tools/gen_config_page.js\n' +
  "'use strict';\n\n" +
  '// Percent-encoded UTF-8 of config.html (decodeURIComponent to get the raw page).\n' +
  'exports.HTML = "' + encoded + '";\n';

fs.writeFileSync(dst, out);
console.log('config_page.js: ' + out.length + ' bytes (from ' + html.length + ' bytes of html)');
