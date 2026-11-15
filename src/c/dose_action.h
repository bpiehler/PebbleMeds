#pragma once
#include <pebble.h>
#include "dose_log.h"

// Marks a dose taken or skipped: logs it, tells the phone and reschedules
// the reminders.
void dose_action_mark(uint8_t med_index, time_t dose_time, DoseAction action);

// Pauses or resumes a med's reminders and tells the phone.
void dose_action_set_paused(uint8_t med_index, bool paused);
