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
2. Schedules next 8 wakeup slots.
3. After each wakeup fires and is handled, schedules the next one.
4. Phone JS syncs med list on app open + every 24 hours (keeps cache fresh).

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
├── wscript                   # Waf build script
├── .gitignore
├── PLAN.md                   # This file
├── spec.md                   # Original spec
├── src/
│   ├── main.c                # App init, window stack, wakeup handler
│   ├── med_list.c/h          # In-memory med array, malloc/free, persistence
│   ├── appmessage.c/h        # AppMessage send/receive, JSON chunking
│   ├── dose_list_window.c/h  # MenuLayer dose list
│   ├── detail_window.c/h     # Detail view + pill drawing
│   ├── notifications.c/h     # Wakeup scheduling, grouping logic, snooze
│   └── pkjs/
│       ├── index.js          # PebbleKit JS: config, AppMessage, wakeup sync
│       ├── config.html       # HTML configuration page
│       └── timeline.js       # Timeline pin management
└── resources/
    └── (no image resources — shapes drawn in C)
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
- Text layers below pill; enable `text_layer_enable_screen_text_flow_and_paging` on `PBL_ROUND`

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
- `PropertyAnimation` slides pill canvas layer off bottom of screen (300ms, `AnimationCurveEaseIn`)
- `animation_stopped` callback pops window without transition
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
```

**API additions to `notifications.h`:**
```c
void notifications_handle_wakeup(WakeupId id, int32_t cookie);
void notifications_handle_timeline_action(uint32_t launch_code);
void notifications_schedule_wakeups(void);   // cancel-all + reschedule next ≤8 regular + any pending snooze
void notifications_schedule_snooze(void);    // persist snooze_t = now + snoozeMins, then schedule_wakeups
```
`main.c` updated to pass `(wakeup_id, wakeup_cookie)` to `notifications_handle_wakeup`.

**Wakeup cookie values:**
```c
#define WAKEUP_COOKIE_DOSE   0
#define WAKEUP_COOKIE_SNOOZE 1
```

**`notifications_schedule_wakeups()` algorithm:**
1. `wakeup_cancel_all()`
2. Build `DoseEvent candidates[32]` by iterating all meds:
   - `t = med_list_next_dose_time(med, now)`
   - Append up to 4 occurrences per med while `t ≤ now + 48h`: for interval advance `t += intervalHours*3600`; for fixed call `med_list_next_dose_time(med, t)` again
3. Insertion-sort `candidates` by `dose_ts`
4. Deduplicate by time (skip if same `dose_ts` as previous)
5. Schedule first `min(8, count)` via `wakeup_schedule(t, WAKEUP_COOKIE_DOSE, false)`
6. If snooze is pending (`persist_read_int(18) > now`) and `scheduled < 8`, also schedule the snooze wakeup

**`notifications_handle_wakeup()` algorithm:**
1. If `cookie == WAKEUP_COOKIE_SNOOZE`: clear persisted snooze (`persist_write_int(18, 0)`)
2. Find due doses: for each med, check `med_list_next_dose_time(med, now - DUE_WINDOW - 1)` ∈ `[now - DUE_WINDOW, now + 60]` (DUE_WINDOW = 300 s)
3. Group due doses by `taker` string into `DueGroup[]` (max 8 groups)
4. If 0 due: log warning, reschedule, open dose list
5. `vibes_short_pulse()`
6. Push `detail_window` for the first due dose in the first group (Phase 4: full group checklist window for >1 med per taker)
7. Reschedule wakeups after handling (accounts for new state)

**`notifications_schedule_snooze()`:**
```c
time_t snooze_t = time(NULL) + (time_t)settings->snoozeMins * 60;
persist_write_int(18, (int32_t)snooze_t);
notifications_schedule_wakeups();  // includes the snooze slot
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

#### Phase 3 checklist
- [ ] `med_list.h/c`: export `med_list_next_dose_time()`
- [ ] `dose_list_window.c`: use exported `med_list_next_dose_time()`
- [ ] `detail_window.c/h`: pill drawing, action bar/click handlers, privacy reveal, Taken animation, all actions
- [ ] `notifications.h`: updated API with `WakeupId`/cookie params and `notifications_schedule_snooze()`
- [ ] `notifications.c`: full wakeup scheduling, due-dose detection, grouping, snooze persist
- [ ] `main.c`: pass wakeup id+cookie to `notifications_handle_wakeup`
- [ ] `appmessage.c`: switch to `MESSAGE_KEY_*` constants

### Phase 4 — Timeline API, App Icon & Round 2 Polish
- [ ] `src/pkjs/timeline.js`: push 48h of pins on sync
- [ ] Pin metadata: taker, dose, privacy-mode-aware title; generic white pill icon in privacy mode
- [ ] Pin actions: Taken / Snooze via `launchCode`
- [ ] App icon: 25×25 pill graphic in `resources/images/` — `app_icon~color.png` + `app_icon~bw.png`; register in `appinfo.json` resources
- [ ] Round 2 / gabbro layout refinements (260×260 canvas, larger text, round-optimized pill centering)
- [ ] gabbro touchscreen: swipe gesture handling layered on top of button-based flow
- [ ] Adherence log: append taken/skipped to `localStorage` + `persist_write_data`

---

## Key Decisions Log

| Decision | Choice | Reason |
|---|---|---|
| Config UI | Custom HTML (not Clay) | Clay can't handle dynamic arrays or rich help text |
| SDK version | Original SDK 4.x | Real-device testing on user's existing Pebble |
| Round 2 features | `#ifdef` guards | Preserves backward compat, enables contest submission |
| Interval reset | Resets on "Taken" action | More clinically accurate; shown clearly in config UI |
| Privacy mode icon | Generic white pill | Colored icon could identify the specific medication |
| Wakeup management | Self-rescheduling (next 8) | Works within Pebble's 8-wakeup limit |
| JSON transport | Chunked AppMessage (200B) | Stays within AppMessage limits; reassembled on watch |
| Max medications | 16 | ~1280B of 4KB persistent storage; generous for real use |
| Notification grouping | Per-taker, 5-min window | Prevents vibration fatigue; keeps takers separate |
| Pill shapes | C drawing primitives | No image assets needed; scales to all screen sizes |
| Skip behavior | Stops snooze immediately | Skipping = intentional; no need to keep reminding |
