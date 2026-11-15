#pragma once
#include <pebble.h>

typedef void (*SkipConfirmCallback)(void);

// Ask "Skip this dose?" with check (Up) and X (Down), like a system confirmation.
void skip_confirm_push(SkipConfirmCallback on_confirm);
