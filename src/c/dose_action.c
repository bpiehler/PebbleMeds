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
