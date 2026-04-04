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
- [x] `package.json` with all target platforms and appKeys
- [x] `wscript` build script
- [x] `src/main.c` minimal skeleton (app init, placeholder windows)
- [x] `src/pkjs/index.js` (config page launch, webviewclosed handler, AppMessage stubs)
- [x] `src/pkjs/config.html` full medication management UI:
  - Global settings: snooze duration, privacy mode toggle
  - Medication list: add / edit / remove (up to 16)
  - Per-med fields: taker, name, dose, schedule (fixed times or interval), shape, color
  - Help text explaining interval reset behavior
  - Save → encodes JSON → sends to watch via `pebblejs://close#`

### Phase 2 — C Watchapp Core & AppMessage Sync
- [ ] `med_list.c/h`: medication array with `malloc`/`free`; `persist_write_data` caching
- [ ] `dose_list_window.c/h`: `MenuLayer` chronological next-dose list
- [ ] `appmessage.c/h`: chunked JSON receive from phone, action send to phone
- [ ] Wire up `main.c`: window stack, AppMessage handlers, persist load on boot

### Phase 3 — Notifications, Grouping & Detail View
- [ ] `notifications.c/h`: wakeup scheduling (next 8 slots), reschedule on fire
- [ ] Grouping logic: 5-min window, per-taker groups
- [ ] Snooze loop: reschedule wakeup after `snoozeMins` if unacknowledged
- [ ] `detail_window.c/h`: pill shape drawing, med info text, Round 2 text flow
- [ ] Confirmation animation: `PropertyAnimation` slide + checkmark fade
- [ ] Privacy mode: hide name until Select pressed

### Phase 4 — Timeline API & Round 2 Polish
- [ ] `src/pkjs/timeline.js`: push 48h of pins on sync
- [ ] Pin metadata: taker, dose, privacy-mode-aware title
- [ ] Pin actions: Taken / Snooze via `launchCode`
- [ ] Round 2 layout refinements (260×260 canvas, larger text, round-optimized pill centering)
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
