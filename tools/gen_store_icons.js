// Generates app store icons for PebbleMeds at 80x80 and 144x144.
//
// Design: circular pill on a deep-teal rounded-square background.
// The pill matches the existing 25x25 app icon — a capsule split into two
// tones by a diagonal dividing line.
//
// Rendering uses 4× supersampling for smooth anti-aliased edges.
//
// Usage:  node tools/gen_store_icons.js
//         Writes resources/images/store_icon_80.png
//                resources/images/store_icon_144.png

'use strict';

var fs  = require('fs');
var PNG = require('pngjs').PNG;

// ---------------------------------------------------------------------------
// Colour palette
// ---------------------------------------------------------------------------
var BG        = { r: 0x00, g: 0x80, b: 0x80, a: 255 };  // deep teal background
var PILL_DARK = { r: 0x00, g: 0xAA, b: 0xA0, a: 255 };  // darker teal pill half
var PILL_LITE = { r: 0x40, g: 0xD0, b: 0xC8, a: 255 };  // lighter teal pill half
var WHITE     = { r: 0xFF, g: 0xFF, b: 0xFF, a: 255 };  // dividing line + rim

// ---------------------------------------------------------------------------
// Rendering helpers (work in supersampled space)
// ---------------------------------------------------------------------------

function dist(x, y, cx, cy) {
  var dx = x - cx, dy = y - cy;
  return Math.sqrt(dx * dx + dy * dy);
}

// Returns a [0,1] alpha for a circle edge (anti-aliasing over ±0.7px).
function circleAlpha(x, y, cx, cy, r) {
  var d = dist(x, y, cx, cy);
  return Math.max(0, Math.min(1, (r + 0.7 - d) / 1.4));
}

// Rounded-square signed-distance (approximate via super-ellipse blend).
function roundedSquareDist(x, y, cx, cy, hw, rr) {
  var ax = Math.abs(x - cx) - (hw - rr);
  var ay = Math.abs(y - cy) - (hw - rr);
  return Math.sqrt(
    Math.pow(Math.max(ax, 0), 2) + Math.pow(Math.max(ay, 0), 2)
  ) + Math.min(Math.max(ax, ay), 0) - rr;
}

function roundedSquareAlpha(x, y, cx, cy, hw, rr) {
  return Math.max(0, Math.min(1, (0.7 - roundedSquareDist(x, y, cx, cy, hw, rr)) / 1.4));
}

function lerpColour(a, b, t) {
  return {
    r: Math.round(a.r + (b.r - a.r) * t),
    g: Math.round(a.g + (b.g - a.g) * t),
    b: Math.round(a.b + (b.b - a.b) * t),
    a: Math.round(a.a + (b.a - a.a) * t),
  };
}

function blendOver(dst, src) {
  // Porter-Duff src-over
  if (src.a === 0) return dst;
  if (src.a === 255) return src;
  var sa = src.a / 255;
  var da = dst.a / 255;
  var oa = sa + da * (1 - sa);
  if (oa === 0) return { r: 0, g: 0, b: 0, a: 0 };
  return {
    r: Math.round((src.r * sa + dst.r * da * (1 - sa)) / oa),
    g: Math.round((src.g * sa + dst.g * da * (1 - sa)) / oa),
    b: Math.round((src.b * sa + dst.b * da * (1 - sa)) / oa),
    a: Math.round(oa * 255),
  };
}

// ---------------------------------------------------------------------------
// Render one pixel (in final output space) by 4× supersampling
// ---------------------------------------------------------------------------
function renderPixel(px, py, size) {
  var SUPER = 4;
  var r = 0, g = 0, b = 0, a = 0;

  for (var sy = 0; sy < SUPER; sy++) {
    for (var sx = 0; sx < SUPER; sx++) {
      var x = px + (sx + 0.5) / SUPER;
      var y = py + (sy + 0.5) / SUPER;

      var cx = size / 2, cy = size / 2;

      // --- Background: rounded square ---
      var bgHW  = size * 0.5;
      var bgRR  = size * 0.22;
      var bgA   = roundedSquareAlpha(x, y, cx, cy, bgHW, bgRR);
      var pixel = { r: 0, g: 0, b: 0, a: 0 };
      pixel = blendOver(pixel, { r: BG.r, g: BG.g, b: BG.b, a: Math.round(bgA * 255) });

      // --- Pill circle ---
      var pillR = size * 0.33;
      var pA    = circleAlpha(x, y, cx, cy, pillR);

      if (pA > 0) {
        // Vertical dividing line through centre: right half = PILL_LITE, left = PILL_DARK
        var onLite = (x > cx);

        var halfColour = onLite ? PILL_LITE : PILL_DARK;

        // Rim: thin white ring near edge of pill
        var rimW  = size * 0.025;
        var rimA  = circleAlpha(x, y, cx, cy, pillR) -
                    circleAlpha(x, y, cx, cy, pillR - rimW);
        rimA = Math.max(0, Math.min(1, rimA * 3));  // sharpen

        // Dividing line: thin vertical white stripe at x = cx
        var lineDist = Math.abs(x - cx);
        var lineW    = size * 0.025;
        var lineA    = Math.max(0, Math.min(1, (lineW - lineDist) / lineW)) * pA;

        var pillPixel = { r: halfColour.r, g: halfColour.g, b: halfColour.b,
                          a: Math.round(pA * 255) };
        var rimPixel  = { r: WHITE.r, g: WHITE.g, b: WHITE.b,
                          a: Math.round(rimA * pA * 255) };
        var linePixel = { r: WHITE.r, g: WHITE.g, b: WHITE.b,
                          a: Math.round(lineA * 255) };

        pixel = blendOver(pixel, pillPixel);
        pixel = blendOver(pixel, rimPixel);
        pixel = blendOver(pixel, linePixel);
      }

      r += pixel.r; g += pixel.g; b += pixel.b; a += pixel.a;
    }
  }

  var n = SUPER * SUPER;
  return {
    r: Math.round(r / n),
    g: Math.round(g / n),
    b: Math.round(b / n),
    a: Math.round(a / n),
  };
}

// ---------------------------------------------------------------------------
// Generate one icon file
// ---------------------------------------------------------------------------
function generateIcon(size, outPath) {
  var png = new PNG({ width: size, height: size, filterType: -1 });

  for (var py = 0; py < size; py++) {
    for (var px = 0; px < size; px++) {
      var c   = renderPixel(px, py, size);
      var idx = (py * size + px) * 4;
      png.data[idx]     = c.r;
      png.data[idx + 1] = c.g;
      png.data[idx + 2] = c.b;
      png.data[idx + 3] = c.a;
    }
  }

  var buf = PNG.sync.write(png);
  fs.writeFileSync(outPath, buf);
  console.log('Written: ' + outPath + ' (' + size + 'x' + size + ')');
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
generateIcon(80,  'resources/images/store_icon_80.png');
generateIcon(144, 'resources/images/store_icon_144.png');
