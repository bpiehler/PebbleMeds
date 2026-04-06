#pragma once
#include <pebble.h>
#include <stdbool.h>

#define MED_MAX 16

typedef enum {
    SCHEDULE_FIXED    = 0,
    SCHEDULE_INTERVAL = 1,
} ScheduleType;

typedef struct {
    uint8_t h;
    uint8_t m;
} TimeEntry;

typedef enum {
    SHAPE_ROUND   = 0,
    SHAPE_OVAL    = 1,
    SHAPE_SHIELD  = 2,
    SHAPE_OBLONG  = 3,
    SHAPE_DROP    = 4,
} PillShape;

typedef struct {
    char         taker[24];
    char         name[32];
    char         dose[24];
    ScheduleType scheduleType;
    // Fixed schedule
    uint8_t      timeCount;
    TimeEntry    times[4];
    // Interval schedule
    uint8_t      intervalHours;
    uint8_t      startHour;
    uint8_t      startMinute;
    uint32_t     lastTakenTs;
    // Visual
    PillShape    shape;
    GColor       color;
} MedEntry;

typedef struct {
    uint16_t snoozeMins;
    bool     privacyMode;
} AppSettings;

void         med_list_init(void);
void         med_list_deinit(void);
uint8_t      med_list_count(void);
MedEntry    *med_list_get(uint8_t index);
void         med_list_set_count(uint8_t count);
void         med_list_set(uint8_t index, const MedEntry *entry);
AppSettings *med_list_get_settings(void);
void         med_list_save_settings(void);
time_t       med_list_next_dose_time(const MedEntry *med, time_t after);
