#pragma once
#include <pebble.h>

// Full-screen result dialog modeled on the PebbleOS alarm confirmation:
// green background, the system's animated check, and a message. Closes
// itself after 2 seconds or on any button.
void confirm_dialog_push(const char *text);
