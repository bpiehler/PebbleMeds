```markdown
# Specification: Pebble Meds (Spring 2026 Contest Entry)

## 1. Project Overview
**Pebble Meds** is a privacy-focused, multi-profile medication reminder and adherence tracking app built for the Pebble smartwatch ecosystem. It is optimized for the **Pebble Round 2** (260x260, 64-color) while maintaining full compatibility with rectangular and monochrome legacy devices. 

The app balances high-utility "at-a-glance" status via the Pebble Timeline with a robust notification system for humans and pets.

---

## 2. Hardware Targets
* **Primary:** Pebble Round 2 (260x260, 64-color e-paper).
* **Secondary:** Pebble Time / Time 2 (Rectangular color).
* **Legacy:** Pebble 2 / Classic (Monochrome).

---

## 3. Data Model (Medication Entry)
The app must support an array of medication objects, each containing:

* **Taker Profile:** String (e.g., "Self", "Murray", "Lola").
* **Medication Name:** String (e.g., "Acetaminophen").
* **Dose Amount:** String, optional (e.g., "1000mg", "2 capsules", "5ml").
* **Scheduling Type:**
    * **Fixed Times:** Specific clock times (e.g., `08:00`, `22:00`).
    * **Interval:** Hourly frequency (e.g., every `6`, `8`, or `12` hours) with a required **Start Time**.
* **Visual Configuration (Optional):**
    * **Shape:** `round` (default), `oval`, `shield`, `oblong`, or `drop` (for liquids).
    * **Color:** Pebble GColor hex (e.g., `GColorLiberty`, `GColorWindsorTan`). Defaults to `GColorWhite`.

---

## 4. System Settings (Global)
These settings apply to the entire application via the phone configuration page:

* **Snooze Duration:** Configurable integer (Minutes). Default: `15`.
* **Privacy Mode:** Toggle (On/Off). 
    * *Behavior:* If ON, notifications and Timeline pins show "Medication Due" instead of the drug name. The specific name/dose is only revealed in the Watchapp Detail View after a "Select" button press.

---

## 5. Functional Logic & UX

### A. Notification Grouping
To prevent "vibration fatigue" during morning/evening routines:
* If $\ge 2$ medications are due within a **5-minute window**, trigger a single **Group Notification**.
* **UI:** Display a summary (e.g., *"3 Meds Due: Self (2), Murray (1)"*).
* **Action:** "View Details" opens a **Grouped Checklist** in the watchapp to mark each as "Taken" or "Skipped" individually.

### B. Sticky Reminders & Snooze
* Notifications require **explicit dismissal** (Taken/Skipped).
* If ignored, the notification will re-trigger (vibrate) according to the global **Snooze Duration** until addressed.

### C. Timeline API Integration
* The PebbleKit JS component must push "Pins" to the Pebble Timeline for the next 48 hours of scheduled doses.
* **Pin Metadata:** Include Taker, Dose Amount, and a color-coded icon matching the pill color.
* **Actions:** "Taken" and "Snooze" actions must be available directly on the Timeline Pin.

---

## 6. UI & Interaction Design

### A. Main Watchapp Views
1.  **Dose List:** A `MenuLayer` showing a chronological list of "Next Doses."
2.  **Detail View:** * **Visual:** A centered, high-resolution icon of the selected `Shape` in the chosen `Color`.
    * **Text:** Display Med Name, Taker, and Dose Amount.
    * **Adaptability:** Use `text_layer_enable_screen_text_flow_and_paging` for Round 2 layouts.
3.  **Confirmation Animation:** Upon marking a dose as "Taken," the pill icon should use a `PropertyAnimation` to slide off the bottom of the screen while a checkmark fades in.

### B. Asset Requirements
* Provide 5 distinct shape icons (`round`, `oval`, `shield`, `oblong`, `drop`).
* Resources must use platform-specific suffixes: `shape~color.png` (64-color) and `shape~bw.png` (1-bit dithered).

---

## 7. Technical Implementation Requirements

* **Language:** C (Watchapp) and JavaScript (PebbleKit JS/Clay).
* **Communication:** Use `AppMessage` for syncing the JSON medication array between phone and watch.
* **Persistence:** Use `persist_write_data` for on-watch caching and `localStorage` for phone-side backup.
* **Build System:** Compatible with the modern Rebble/Waf build system (ensure `appinfo.json`/`package.json` includes `chalk`, `diorite`, and `basalt` platforms).
* **Memory:** Strictly manage memory for the dynamic medication list using `malloc` and `free`.

---

## 8. Development Milestones
1.  **Phase 1:** Build the Clay configuration page with Fixed/Interval scheduling logic.
2.  **Phase 2:** Implement the C `MenuLayer` and `AppMessage` sync.
3.  **Phase 3:** Develop the Grouping Logic for notifications and the Detail View UI.
4.  **Phase 4:** Integrate Timeline API and refine Round 2 specific animations.
```