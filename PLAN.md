# PebbleMeds — Implementation Plan

## Project Overview

Privacy-focused medication reminder and adherence tracking app for Pebble smartwatches. Targets the new Pebble Round 2 (primary) with full backwards compatibility to Pebble Time/Time 2 (color rectangular) and Pebble Classic/2 (monochrome).

**Contest:** Spring 2026 Rebble App Contest  
**SDK:** Original Pebble SDK 4.x (enables real-hardware testing on user's physical device; Round 2 enhancements guarded by `#ifdef PBL_ROUND` / `PBL_COLOR`)

---

## Architecture

```
Phone (PebbleKit JS)          Watch (C)
─────────────────────         ─────────────────
config.html (HTML UI)  ──►    main.c
index.js               ◄──►   appmessage.c
timeline.js            ──►    med_list.c
localStorage           ──►    persistence (persist_write_data)
                              dose_list_window.c
                              detail_window.c
                              notifications.c
```

**Communication:** `AppMessage` (JSON chunks phone→watch, action events watch→phone)  
**Config Page:** Custom HTML page (not Clay) — required for dynamic med array management and help text  

---

## Data Model

### Medication Entry (per item in array, max 16)

```json
{
  "taker": "Self",
  "name": "Acetaminophen",
  "dose": "1000mg",
  "scheduleType": "fixed",
  "times": [{"h": 8, "m": 0}, {"h": 22, "m": 0}],
  "shape": "round",
  "color": "GColorWhite"
}
```

```json
{
  "taker": "Murray",
  "name": "Amoxicillin",
  "dose": "250mg",
  "scheduleType": "interval",
  "intervalHours": 8,
  "startHour": 8,
  "startMinute": 0,
  "lastTakenTs": 0,
  "shape": "oval",
  "color": "GColorLiberty"
}
```

**Fields:**
- `taker` — free text, e.g. "Self", "Murray", "Lola"
- `name` — medication name
- `dose` — optional free text, e.g. "1000mg", "2 capsules", "5ml"
- `scheduleType` — `"fixed"` or `"interval"`
- `times` — array of `{h, m}` objects, 1–4 entries (fixed only)
- `intervalHours` — 4, 6, 8, 12, or 24 (interval only)
- `startHour`, `startMinute` — initial dose time (interval only)
- `lastTakenTs` — Unix timestamp of last "Taken" confirmation; interval next-dose calculated from this
- `shape` — `"round"` | `"oval"` | `"shield"` | `"oblong"` | `"drop"`
- `color` — Pebble GColor constant name string, e.g. `"GColorWhite"`

**Max 16 medications.** Estimated ~80 bytes/entry × 16 = ~1280 bytes, well within 4KB persistent storage.

### Global Settings

```json
{
  "snoozeMins": 15,
  "privacyMode": false
}
```

### Full Config Payload (phone → watch via AppMessage)

```json
{
  "meds": [ ... ],
  "settings": { "snoozeMins": 15, "privacyMode": false }
}
```

### Dose Log Entry (for future history view)

```json
{ "medIndex": 0, "ts": 1712100000, "status": "taken" }
```
Logged to `localStorage` (phone) and `persist_write_data` (watch). Status: `"taken"` or `"skipped"`.

---

## Scheduling Logic

### Fixed Times
- Medications fire at each listed `{h, m}` time every day.
- "Next dose" = the earliest upcoming `{h, m}` today, or first one tomorrow.

### Interval
- Timer starts from `lastTakenTs` (NOT midnight reset).
- Next dose = `lastTakenTs + intervalHours * 3600`.
- On first run (lastTakenTs = 0), schedule from `{startHour, startMinute}` today.
- **Important for users:** The interval timer resets when you mark a dose as Taken — e.g. if you take an 8-hour med at 9am, the next dose is due at 5pm regardless of the scheduled start time.

---

## Notification & Wakeup System

### Wakeup Strategy
Pebble allows max 8 scheduled wakeups at once. Strategy:
1. Watch calculates upcoming doses from local med cache.
2. Schedules next 6-7 dose wakeup slots (1 always reserved for heartbeat safety net, +1 for snooze when pending).
3. After each wakeup fires and is handled, schedules the next batch.
4. A heartbeat wakeup fires every 6 hours as a safety net — if the chain breaks (crash, reboot), it rebuilds everything within 6 hours.
5. Phone JS syncs med list on app open + whenever config changes (no periodic sync).

### Grouping Logic
- If ≥2 medications due within a 5-minute window: fire a **single grouped notification**.
- Grouping is per-taker (e.g., Self's meds group separately from Murray's).
- Group UI: summary row per taker (e.g., "Self: 2 meds"), then individual checklist.

### Sticky Reminders / Snooze
- Notifications require explicit Taken or Skipped action.
- If ignored: re-triggers after `snoozeMins` via a new wakeup.
- **Skipping stops the snooze timer** (dose is recorded as skipped, no further reminders).

### Privacy Mode
- When ON: notification and Timeline pin text shows "Medication Due" (not med name/dose).
- Timeline pin icon: generic white pill (not the configured color, which could identify the med).
- Detail view still shows full info after pressing Select.

---

## AppMessage Keys

Defined in `package.json` → auto-generated into C header.

| Key | ID | Direction | Purpose |
|---|---|---|---|
| `KEY_CONFIG_JSON` | 0 | Phone→Watch | Full config JSON (chunked) |
| `KEY_CHUNK_INDEX` | 1 | Phone→Watch | Current chunk number |
| `KEY_CHUNK_TOTAL` | 2 | Phone→Watch | Total chunks |
| `KEY_ACTION` | 3 | Watch→Phone | "taken", "skipped", "snooze" |
| `KEY_MED_INDEX` | 4 | Watch→Phone | Index of med acted upon |
| `KEY_DOSE_TS` | 5 | Watch→Phone | Timestamp of dose |
| `KEY_REQUEST_SYNC` | 6 | Watch→Phone | Watch requesting config refresh |

**Chunking:** JSON is split into 200-byte chunks; reassembled on watch before parsing.

---

## UI & Views

### 1. Dose List Window (`MenuLayer`)
- Rows sorted chronologically by next scheduled dose time.
- Row format: `[HH:MM] Med Name — Taker (Dose)`
- Privacy mode: `[HH:MM] Medication Due — Taker`
- Select → opens Detail View.

### 2. Detail View
- Centered pill shape drawn with GDraw primitives (`#ifdef PBL_COLOR` for color fill, white on monochrome).
- Text: Med Name, Taker, Dose Amount.
- Round 2: `text_layer_enable_screen_text_flow_and_paging` for text wrapping.
- Privacy mode: reveals name/dose only after Select press.
- Action Bar: Taken / Snooze / Back.

### 3. Group Checklist View
- Triggered when a grouped notification's "View Details" is tapped.
- `MenuLayer` showing each grouped med; each row checkable as Taken/Skipped.

### 4. Confirmation Animation
- On "Taken": `PropertyAnimation` slides pill icon off bottom of screen.
- Checkmark fades in using `property_animation_create` + opacity interpolation (or alpha layer on color platforms).

---

## Pill Shape Drawing (C, GDraw primitives)

| Shape | Drawing approach |
|---|---|
| `round` | `graphics_fill_circle` |
| `oval` | `gpath_draw_filled` with ellipse approximation (or scaled circle) |
| `shield` | Rounded rect + triangle cap via `GPath` |
| `oblong` | `graphics_fill_rect` with rounded corners (`GCornerAll`) |
| `drop` | Circle + triangle via `GPath` |

Platform variants:
- `PBL_COLOR`: fill with configured GColor
- `!PBL_COLOR` (aplite/diorite): fill white, stroke black

---

## Timeline API

PebbleKit JS pushes Timeline pins for next 48 hours on every sync.

**Pin structure:**
```json
{
  "id": "pebble-meds-{medIndex}-{doseTs}",
  "time": "<ISO8601>",
  "layout": {
    "type": "genericPin",
    "title": "Med Name (or 'Medication Due' in privacy mode)",
    "body": "Taker — Dose",
    "tinyIcon": "system://images/GENERIC_WARNING"
  },
  "actions": [
    { "title": "Taken", "type": "openWatchApp", "launchCode": 1 },
    { "title": "Snooze", "type": "openWatchApp", "launchCode": 2 }
  ]
}
```

---

## File Structure

```
PebbleMeds/
├── package.json              # App manifest + appKeys
├── appinfo.json              # CloudPebble app manifest
├── wscript                   # Waf build script
├── .gitignore
├── PLAN.md                   # This file
├── tests/
│   └── schedule.test.js      # Jest unit tests for scheduling logic
├── src/
│   └── c/
│       ├── main.c            # App init, window stack, wakeup handler
│       ├── med_list.c/h      # In-memory med array, persistence (keys 0–17)
│       ├── appmessage.c/h    # AppMessage send/receive, JSON chunking
│       ├── dose_list_window.c/h  # MenuLayer dose list
│       ├── detail_window.c/h # Detail view + pill drawing
│       ├── notifications.c/h # Wakeup scheduling, grouping logic, snooze (key 18)
│       ├── dose_log.c/h      # Ring-buffer adherence log (key 19, 30 entries)
│       └── pkjs/
│           ├── app.js        # PebbleKit JS: config, AppMessage, wakeup sync
│           ├── schedule.js   # Pure scheduling functions (also used by tests)
│           ├── timeline.js   # Timeline pin management
│           └── config.html   # HTML configuration page
└── resources/
    └── images/
        ├── app_icon~color.png  # 25×25 teal pill icon (color platforms)
        └── app_icon~bw.png     # 25×25 white pill icon (monochrome platforms)
```

---

## Build Targets

| Platform | Hardware | Color | Shape | Resolution | Notes |
|---|---|---|---|---|---|
| `aplite` | Pebble Classic / Steel | Monochrome | Rectangular | 144×168 | Legacy |
| `diorite` | Pebble 2 | Monochrome | Rectangular | 144×168 | Legacy |
| `basalt` | Pebble Time / Time Steel | 64-color | Rectangular | 144×168 | |
| `chalk` | Pebble Time Round | 64-color | Round | 180×180 | |
| `emery` | Pebble Time 2 | 64-color | Rectangular | 200×228 | |
| `flint` | Pebble 2 Duo (new 2025) | Monochrome | Rectangular | 144×168 | No touchscreen |
| `gabbro` | Pebble Round 2 (new 2025) | 64-color | Round | 260×260 | **Primary contest target**; touchscreen; 200+ DPI e-paper |

### Platform capability macros

| Macro | Platforms |
|---|---|
| `PBL_ROUND` | `chalk`, `gabbro` |
| `PBL_COLOR` | `basalt`, `chalk`, `emery`, `gabbro` |
| `PBL_RECT` | `aplite`, `diorite`, `basalt`, `emery`, `flint` |
| Monochrome (no PBL_COLOR) | `aplite`, `diorite`, `flint` |

Use `PBL_ROUND` for round-layout code (text flow, circular canvas math), `PBL_COLOR` for color drawing, never check platform names directly. `gabbro`'s touchscreen requires additional handling in Phase 4 — button-based interaction used for all platforms in Phases 2/3.

---

## Development Phases

### Phase 1 — Project Scaffold & HTML Config Page ✅
- [x] `package.json` / `appinfo.json` with all 7 target platforms
- [x] `wscript` build script
- [x] `src/c/main.c` skeleton (app init, wakeup + Timeline launch handling)
- [x] `src/pkjs/index.js` (config page launch, webviewclosed handler, chunked AppMessage, Timeline pins, dose logging)
- [x] `src/pkjs/config.html` full medication management UI:
  - Global settings: snooze duration, privacy mode toggle
  - Medication list: add / edit / remove (up to 16)
  - Per-med fields: taker, name, dose, schedule (fixed times or interval), shape, color
  - Help text explaining interval reset behavior
  - Save → encodes JSON → sends to watch via `pebblejs://close#`
- [x] Fixed CloudPebble compatibility: `appinfo.json` flat format, `src/c/` source layout, `AppLaunchReason`, `author` field
- [x] Switched to automatic `messageKeys` (no more deprecated `appKeys`)
- [x] Switched JS handling to CommonJS entry point (no more deprecated concatenation)

### Phase 2 — C Watchapp Core & AppMessage Sync ✅
- [x] `jsmn.h`: single-header JSON tokenizer
- [x] `med_list.c/h`: medication array with `persist_write_data` caching; `AppSettings` struct (snooze, privacy) persisted to key 17
- [x] `dose_list_window.c/h`: `MenuLayer` with next-dose calculation (fixed + interval), insertion sort, 12h/24h formatting, privacy mode, color highlight
- [x] `appmessage.c/h`: chunked JSON receive + two-level parse (find_matching_brace + per-med 64-token jsmn); GColor/PillShape name lookup; settings parse; action send
- [x] `detail_window.c/h`: stub (text-only) ready for Phase 3 pill drawing

### Phase 3 — Notifications, Grouping & Detail View

#### 3a. Export `next_dose_time` from `med_list`
`next_dose_time()` is currently `static` in `dose_list_window.c`. Both the dose list and notifications need it, so move it to `med_list.c` and export via `med_list.h`:
```c
time_t med_list_next_dose_time(const MedEntry *med, time_t after);
```
`dose_list_window.c` then calls the exported version.

#### 3b. `detail_window.c/h` — full implementation

**Layout (rectangular, 144×168 with 30px action bar → 114px content):**
- Pill canvas layer: 56×56, centered horizontally at x≈57, top at y≈20
- Name text layer: `GOTHIC_18_BOLD`, below pill, full content width
- Taker + dose text layer: `GOTHIC_14_BOLD`, below name

**Layout (round — chalk 180×180, gabbro 260×260):**
- No action bar; pill canvas larger (72×72 for chalk, 100×100 for gabbro via `PBL_DISPLAY_WIDTH`)
- Text layers below pill; call `text_layer_enable_screen_text_flow_and_paging(layer, 5)` on `PBL_ROUND` — **must be called after `layer_add_child`** or it silently does nothing

**Pill drawing — `draw_pill(GContext, GPoint center, int r, PillShape, GColor)`:**
| Shape | Drawing |
|---|---|
| `SHAPE_ROUND` | `graphics_fill_circle(center, r)` + stroke |
| `SHAPE_OVAL` | `graphics_fill_rect` tall rounded rect (w=r, h=2r, corner=r/2) |
| `SHAPE_OBLONG` | `graphics_fill_rect` wide rounded rect (w=2r, h=r, corner=r/4) |
| `SHAPE_SHIELD` | 5-point `GPath`: top-center, upper-right, lower-right, lower-left, upper-left |
| `SHAPE_DROP` | 7-point `GPath` approximating teardrop (pointed top, round bottom) |

Color: `PBL_COLOR` → `med->color`; monochrome → `GColorWhite` fill + `GColorBlack` stroke.  
GPath objects created once in `window_load`, destroyed in `window_unload`.

**Buttons:**
- `PBL_RECT`: `ActionBarLayer` (right edge, 30px). Up = Snooze, Select = Taken/Reveal, Down = Skip. Icons NULL for Phase 3; Phase 4 adds bitmaps.
- `PBL_ROUND`: window click handlers only (no action bar). Select = Taken/Reveal, Up = Snooze, Down = Skip.

**Privacy mode reveal:**
- State: `static bool s_revealed`; starts `false` when `privacyMode` is ON
- While `!s_revealed`: name layer shows "Medication Due", dose hidden, Select = Reveal
- After reveal or when `privacyMode` OFF: normal display, Select = Taken

**Confirmation animation (Taken):**
- `property_animation_create_layer_frame(layer, NULL, &to_rect)` — `NULL` from uses current frame
- Retrieve base animation: `Animation *anim = property_animation_get_animation(prop_anim)`
- Set on `anim`: duration 300ms, `AnimationCurveEaseIn`, `.stopped` handler
- Stopped callback signature: `void cb(Animation *anim, bool finished, void *ctx)`
- In stopped callback: `property_animation_destroy(prop_anim)` (**not** `animation_destroy`), then `window_stack_pop(false)`
- All other actions (Snooze, Skip, Back): `window_stack_pop(true)` directly

**Actions (all call `appmessage_send_action` then navigate):**
- Taken: send `"taken"`, vibrate short, animate pill off, reschedule wakeups
- Snooze: send `"snooze"`, schedule snooze wakeup, pop window
- Skip: send `"skipped"`, reschedule wakeups, pop window
- Back (no action): schedule snooze wakeup (treat as deferred), pop window

#### 3c. `notifications.c/h` — full implementation

**Persist key layout** (extending med_list.c's layout):
```
18 = int32  pending snooze Unix timestamp (0 = none)
19 = DoseLogStore  ring-buffer adherence log (30 entries)
20 = uint8  med index of snoozed dose
21 = int32  Unix timestamp of snoozed dose
```

**API additions to `notifications.h`:**
```c
void notifications_handle_wakeup(WakeupId id, int32_t cookie);
void notifications_handle_timeline_action(uint32_t launch_code);
void notifications_schedule_wakeups(void);   // cancel-all + reschedule doses + snooze + heartbeat
void notifications_schedule_snooze(uint8_t med_index, time_t dose_time);  // persist + reschedule
```
`main.c` updated to pass `(wakeup_id, wakeup_cookie)` to `notifications_handle_wakeup`.

**Wakeup cookie values:**
```c
#define WAKEUP_COOKIE_DOSE      0
#define WAKEUP_COOKIE_SNOOZE    1
#define WAKEUP_COOKIE_HEARTBEAT 2
#define HEARTBEAT_INTERVAL_SECS (6 * 3600)  // safety-net recovery window
```

**`notifications_schedule_wakeups()` algorithm:**
1. `wakeup_cancel_all()`
2. If no meds configured, return early (nothing to schedule)
3. Read snooze persist (key 18); if snooze_t > now, reserve 1 slot for it
4. Build `DoseEvent candidates[32]` by iterating all meds:
   - `t = med_list_next_dose_time(med, now)`
   - Append up to 4 occurrences per med while `t ≤ now + 7 days`
5. Insertion-sort `candidates` by `dose_ts`
6. Deduplicate by time (skip if same `dose_ts` as previous)
7. Schedule first `min(max_dose, count)` dose slots via `wakeup_schedule(t, WAKEUP_COOKIE_DOSE, false)`; skip any slot where `id < 0` (e.g. `E_RANGE` for times within 60s of now)
   - `max_dose = has_snooze ? 6 : 7` (8 total - 1 heartbeat - 1 snooze if pending)
8. If snooze pending, schedule snooze wakeup
9. Always schedule heartbeat wakeup at `now + HEARTBEAT_INTERVAL_SECS`

**`notifications_handle_wakeup()` algorithm:**
1. If `cookie == WAKEUP_COOKIE_HEARTBEAT`: reschedule all wakeups, push dose list, return (no vibrate, no alert — silent recovery)
2. If `cookie == WAKEUP_COOKIE_SNOOZE`: clear persisted snooze state (keys 18, 20, 21), reschedule, vibrate, push detail window for the snoozed dose
3. For `WAKEUP_COOKIE_DOSE`: find due doses within `[now - DUE_WINDOW, now + 60]` (DUE_WINDOW = 300 s)
4. If 0 due: log warning, reschedule, push dose list
5. `vibes_short_pulse()`
6. Push `detail_window` for the first due dose
7. Reschedule wakeups after handling (accounts for new state)

**`notifications_schedule_snooze()`:**
```c
time_t snooze_t = time(NULL) + (time_t)settings->snoozeMins * 60;
persist_write_int(PERSIST_KEY_SNOOZE,      (int32_t)snooze_t);
persist_write_int(PERSIST_KEY_SNOOZE_MED,  (int32_t)med_index);
persist_write_int(PERSIST_KEY_SNOOZE_DOSE, (int32_t)dose_time);
notifications_schedule_wakeups();  // includes the snooze + heartbeat slots
```

#### 3d. `appmessage.c` — switch to `MESSAGE_KEY_*`
Replace the 7 `#define KEY_* N` lines with:
```c
#define KEY_CONFIG_JSON  MESSAGE_KEY_ConfigJson
#define KEY_CHUNK_INDEX  MESSAGE_KEY_ChunkIndex
#define KEY_CHUNK_TOTAL  MESSAGE_KEY_ChunkTotal
#define KEY_ACTION       MESSAGE_KEY_Action
#define KEY_MED_INDEX    MESSAGE_KEY_MedIndex
#define KEY_DOSE_TS      MESSAGE_KEY_DoseTs
#define KEY_REQUEST_SYNC MESSAGE_KEY_RequestSync
```
Keeps all existing code unchanged; values stay 0–6; eliminates manual/auto drift risk.

#### Phase 3 checklist ✅
- [x] `med_list.h/c`: export `med_list_next_dose_time()`
- [x] `dose_list_window.c`: use exported `med_list_next_dose_time()`
- [x] `detail_window.c/h`: pill drawing, action bar/click handlers, privacy reveal, Taken animation, all actions
- [x] `notifications.h`: updated API with `WakeupId`/cookie params and `notifications_schedule_snooze()`
- [x] `notifications.c`: full wakeup scheduling, due-dose detection, grouping, snooze persist
- [x] `main.c`: pass wakeup id+cookie to `notifications_handle_wakeup`
- [x] `appmessage.c`: `MESSAGE_KEY_*` constants not generated by CloudPebble — keeping numeric `#define KEY_* N` values

### Phase 3.5 — JS Scheduling Tests ✅
- [x] Extract `getFixedTimes`, `getIntervalTimes`, `getNextDoseTimes` to `src/pkjs/schedule.js` (no Pebble deps, CommonJS exports)
- [x] Update `app.js` to `require('./schedule')` (via `timeline.js`)
- [x] Add Jest as dev dependency; add `"test": "jest"` script to `package.json`
- [x] Write `tests/schedule.test.js` — 22 cases covering fixed/interval schedules and edge cases; all passing

### Phase 4 — Timeline API, App Icon & Round 2 Polish ✅
- [x] Fix Timeline bugs: wrong API endpoint (`getpebble.com` → `rebble.io`), spurious `timelineSubscribe` calls removed, icon changed to `NOTIFICATION_REMINDER`
- [x] Extract Timeline logic to `src/pkjs/timeline.js`; `app.js` requires it; `timeline.js` requires `schedule.js`
- [x] Pin metadata: confirmed privacy-mode title/body; `openWatchApp` + `launchCode` verified correct
- [x] App icon: 25×25 PNG files in `resources/images/`; registered with `"type": "bitmap", "menuIcon": true` in `appinfo.json` and `package.json`
- [x] Round 2 / gabbro layout: `PILL_R` now 32 for `PBL_DISPLAY_WIDTH >= 200` (gabbro 260px, emery 200px), 28 for chalk (PBL_ROUND 180px), 20 for 144px platforms
- [x] gabbro touchscreen: no swipe API in C SDK — firmware maps touch→buttons automatically; no code change needed
- [x] Adherence log: `dose_log.c/h` — 30-entry ring buffer at persist key 19; records Taken/Skipped/Snoozed actions from `detail_window.c`; flushed immediately on each record
- [x] Timeline pin insertion working via `Pebble.insertTimelinePin()` local API
- [x] Timeline tests: `tests/timeline.test.js` — 16 cases covering `buildPin` structure and `pushTimelinePins` behaviour; 38 total tests passing

### Phase 5 — UI Polish ✅

- [x] **Taker smart dropdown** (`config.html`): replaced free-text taker field with a `<select>` populated from `knownTakers` (persisted in `settings.knownTakers`, defaults to `["Self"]`). "Add person…" option appends new names permanently. `package.json` JS entry point corrected from `index.js` → `app.js`.
- [x] **Action bar icons** (`detail_window.c`): replaced text hint line with bitmap icons on `ActionBarLayer` (PBL_RECT) — ✓ checkmark (Taken/Select), Z (Snooze/Up), × (Skip/Down). Icons generated as 18×18 grayscale PNGs; registered in `appinfo.json` and `package.json`.
- [x] **Larger text in detail view**: name font upgraded to `GOTHIC_24_BOLD`, taker to `GOTHIC_18_BOLD`, dose to `GOTHIC_14_BOLD`.
- [x] **Dose time label**: new `s_time_layer` shows "Due: HH:MM [AM/PM]" beneath the dose amount, using `clock_is_24h_style()`.
- [x] **Med list header**: `dose_list_window.c` gains `get_num_sections`, `get_header_height`, and `draw_header` callbacks; header reads "Upcoming Doses".
- [x] **Animations** (`detail_window.c`):
  - *Entry*: pill drops in from above on `window_appear` (`AnimationCurveEaseOut`, 280 ms); fires only once per push (`s_entry_done` flag).
  - *Taken*: existing slide-off-bottom retained; uses `layer_get_bounds` for display-height to work on all platforms.
  - *Skip*: pill slides off the top of the screen (`AnimationCurveEaseIn`, 250 ms).
  - *Snooze (wobble)*: 7-step `AppTimer` wobble (±8 → ±5 → ±3 → 0 px, 60 ms/step) before popping.
  - All animations share `cancel_current_anim()` which pre-clears `s_prop_anim` before `animation_unschedule` to prevent double-free in stopped handlers.
- [x] **Drop pill shape fix**: replaced 7-point `GPath` with 10-point version; added centre-bottom vertex `{0, PILL_R}` and extra intermediate side points to eliminate flat bottom and angular sides.

### Phase 6 — Display Polish & Platform Hardening ✅

- [x] **Mini-pill icons in dose list** (`dose_list_window.c`): `draw_pill_mini()` draws the actual pill shape (round/oval/oblong/shield/drop, r=8) to the left of each row. Color platforms use the configured `GColor`; monochrome platforms use black outline, inverted to white when the row is highlighted.
- [x] **Custom draw_row** (`dose_list_window.c`): replaced `menu_cell_basic_draw` with manual `graphics_draw_text` calls to control font sizes (`GOTHIC_18_BOLD` title, `GOTHIC_14` subtitle) and accommodate the pill icon. Row text truncated cleanly with `GTextOverflowModeFill`.
- [x] **Empty-state layer** (`dose_list_window.c`): when no medications are configured, the `MenuLayer` is hidden and a centred `TextLayer` reads "No meds scheduled.\nUse the phone app!" — `dose_list_window_refresh()` toggles visibility correctly.
- [x] **Round-device button hints** (`detail_window.c`, PBL_ROUND): `s_hints_layer` with `hints_update_proc` draws Z (Snooze/Up), a two-line checkmark (Taken/Select), and X (Skip/Down) as primitives aligned with the right-edge button positions. Added to layer tree *after* text layers so it renders on top.
- [x] **Monochrome drawing fix** (`detail_window.c`): removed black-background fill on monochrome platforms; pill draws outline-only (white background shows through). Resolved invisible-pill issue on aplite/diorite/flint.
- [x] **Round header height** (`dose_list_window.c`): custom 34 px header on PBL_ROUND using `GOTHIC_24_BOLD` centred, vs `MENU_CELL_BASIC_HEADER_HEIGHT` on rectangular.
- [x] **Animation timing** (`detail_window.c`): entry, taken, and skip durations tightened to 200 ms; wobble step interval 40 ms — feels snappier on e-paper.
- [x] **TEST_MODE scaffold** (`med_list.c`): `#define TEST_MODE 0/1` toggle loads 3 hardcoded meds (Multivitamin/Self, Apoquel/Murray, Ibuprofen/Self) for emulator-only testing without touching persistent storage. **Set to 0 before contest submission.**
- [x] **Build stability**: reverted to `sdkVersion: "3"` and flat `appKeys` format in `appinfo.json` for CloudPebble SDK 3/4 compatibility; manual `#define KEY_*` constants kept in `appmessage.c`.

---

## Key Decisions Log

| Decision | Choice | Reason |
|---|---|---|
| Config UI | Custom HTML (not Clay) | Clay can't handle dynamic arrays or rich help text |
| SDK version | Original SDK 4.x | Real-device testing on user's existing Pebble |
| Round 2 features | `#ifdef` guards | Preserves backward compat, enables contest submission |
| Interval reset | Resets on "Taken" action | More clinically accurate; shown clearly in config UI |
| Privacy mode icon | Generic white pill | Colored icon could identify the specific medication |
| Wakeup management | Self-rescheduling + heartbeat safety net | Works within Pebble's 8-wakeup limit; heartbeat recovers from crashes/reboots within 6 hours |
| JSON transport | Chunked AppMessage (200B) | Stays within AppMessage limits; reassembled on watch |
| Max medications | 16 | ~1280B of 4KB persistent storage; generous for real use |
| Notification grouping | Per-taker, 5-min window | Prevents vibration fatigue; keeps takers separate |
| Pill shapes | C drawing primitives | No image assets needed; scales to all screen sizes |
| Skip behavior | Stops snooze immediately | Skipping = intentional; no need to keep reminding |
| CloudPebble JS mode | CommonJS (not concatenation) | Required for `require()` to work; entry point must be `app.js` |
| Timeline insertion | `Pebble.insertTimelinePin()` | Remote timeline pins not yet supported on Rebble; `getAccountToken`/`getWatchToken` not implemented; local API added in Rebble app v1.0.6.8 |
