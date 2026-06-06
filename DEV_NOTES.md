# PebbleMeds — Development Notes

## Tools & Environment

- Build: `pebble build` (Waf-based, SDK 4.9.169)
- Test: `npx jest` (Jest, runs JS mirror tests — no emulator needed)
- Format: No auto-formatter configured; maintain consistent C style manually

## Architecture

### Wakeup Slot Allocation (8 max total)

| Slot | Condition | Cookie |
|------|-----------|--------|
| 1-6 | Dose events (always) | `WAKEUP_COOKIE_DOSE` (0) |
| 7 | Dose event (only when NO snooze) | `WAKEUP_COOKIE_DOSE` (0) |
| 7 | Snooze (when snooze pending) | `WAKEUP_COOKIE_SNOOZE` (1) |
| 8 | Heartbeat safety net (always, when meds exist) | `WAKEUP_COOKIE_HEARTBEAT` (2) |

- `max_dose = has_snooze ? 6 : 7`
- Heartbeat at `now + 6 * 3600` seconds; fires silently (no vibrate, just reschedules)

### Persist Key Layout

| Key | Type | Owner | Purpose |
|-----|------|-------|---------|
| 0 | uint8 | med_list.c | Med count |
| 1-16 | MedEntry | med_list.c | One per med (max 16) |
| 17 | AppSettings | med_list.c | snoozeMins, privacyMode |
| 18 | int32 | notifications.c | Pending snooze timestamp (0 = none) |
| 19 | DoseLogStore | dose_log.c | 30-entry ring buffer |
| 20 | uint8 | notifications.c | Snoozed med index |
| 21 | int32 | notifications.c | Snoozed dose timestamp |

No key conflicts exist.

### C/JS Code Mirrors

Three pairs of C and JS functions must be kept in sync:
- `med_list_next_dose_time()` ↔ `schedule.js:getNextDoseTimes()`
- `collect_dose_events()` ↔ `wakeups.js:collectDoseEvents()`
- `notifications_schedule_wakeups()` ↔ `wakeups.js:planWakeups()`

The JS mirrors are testable without a Pebble emulator.

### Build Targets (7 platforms)

`gabbro` (Round 2 260×260), `chalk` (Time Round 180×180), `emery` (Time 2 200×228), `basalt` (Time 144×168), `aplite` (Classic 144×168), `diorite` (Pebble 2 144×168), `flint` (Pebble 2 Duo 144×168).

## Gotchas

### Wakeup Fragility (mitigated by heartbeat)

Before the heartbeat fix, wakeup chain breaks (app crash, watch reboot) were permanent until manual app open. The heartbeat runs `notifications_schedule_wakeups()` every 6 hours to recover silently.

### Pebble Wakeup Platform Limits

- Max 8 simultaneous wakeups (hard limit)
- `wakeup_schedule()` returns `E_RANGE` for times < 60 seconds from now
- Wakeups may be cleared on watch reboot or app reinstall
- There is NO "background app" concept in SDK 3/4 — the Wakeup API IS the background mechanism

### AppMessage Chunking

JSON config is ~3KB; max AppMessage inbox size is 768 bytes. Config is split into chunks of ~200 bytes each, reassembled on watch. Buffer is 3300 bytes.

### Monochrome Drawing

On `!PBL_COLOR` platforms (aplite, diorite, flint), pill shapes must NOT fill with a background color (the background is transparent). Drawing is outline-only (black stroke on device background).

### LSP Errors on pebble.h

The LSP can't resolve `pebble.h` or Pebble SDK types. These are NOT real errors — `pebble build` resolves them correctly. Ignore all `pebble.h` file-not-found and unknown-type diagnostics in C files.

### Animation Lifecycle

`PropertyAnimation` objects must be destroyed via `property_animation_destroy()`, NOT `animation_destroy()`. The stopped handler must clear `s_prop_anim` BEFORE calling `animation_unschedule()` to prevent double-free in re-entrant stopped handler calls.

### CloudPebble Compatibility

The app uses `sdkVersion: "3"` in `appinfo.json` and flat `appKeys` format for compatibility with both CloudPebble and modern Rebble SDK. Manual `#define KEY_*` constants in `appmessage.c` prevent drift.
