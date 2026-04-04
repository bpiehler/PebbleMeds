#pragma once
#include <stdint.h>

void appmessage_init(void);
void appmessage_deinit(void);
void appmessage_send_action(uint8_t med_index, const char *action, uint32_t dose_ts);
void appmessage_request_sync(void);
