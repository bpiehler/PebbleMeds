#include "dose_action.h"
#include "med_list.h"
#include "appmessage.h"
#include "notifications.h"

void dose_action_mark(uint8_t med_index, time_t dose_time, DoseAction action) {
    bool taken = (action == DOSE_TAKEN);
    dose_log_record(med_index, action, (uint32_t)dose_time);
    appmessage_send_action(med_index, taken ? "taken" : "skipped", (uint32_t)dose_time);

    // Interval doses count from the time taken; the phone syncs the same value later.
    MedEntry *med = med_list_get(med_index);
    if (taken && med && med->scheduleType == SCHEDULE_INTERVAL) {
        med->lastTakenTs = (uint32_t)time(NULL);
    }
    notifications_schedule_wakeups();
}

void dose_action_clear(uint8_t med_index, time_t dose_time) {
    dose_log_clear_mark(med_index, (uint32_t)dose_time);
    appmessage_send_action(med_index, "undo", (uint32_t)dose_time);

    // Interval doses count from the last dose still taken; the phone sends its exact time later.
    MedEntry *med = med_list_get(med_index);
    if (med && med->scheduleType == SCHEDULE_INTERVAL) {
        DoseLogEntry e;
        med->lastTakenTs = 0;
        for (int i = (int)dose_log_count() - 1; i >= 0; i--) {
            e = dose_log_get((uint8_t)i);
            if (e.med_index == med_index && e.action == DOSE_TAKEN) {
                med->lastTakenTs = e.ts;
                break;
            }
        }
    }
    notifications_schedule_wakeups();
}
