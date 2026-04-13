#include "appmessage.h"
#include "med_list.h"
#include "dose_list_window.h"
#include "jsmn.h"
#include <pebble.h>
#include <string.h>
#include <stdlib.h>

// AppMessage key IDs — must match appinfo.json appKeys.
#define KEY_CONFIG_JSON  0
#define KEY_CHUNK_INDEX  1
#define KEY_CHUNK_TOTAL  2
#define KEY_ACTION       3
#define KEY_MED_INDEX    4
#define KEY_DOSE_TS      5
#define KEY_REQUEST_SYNC 6

// Chunked JSON reassembly. 16 meds * ~200 bytes each + overhead.
#define JSON_BUF_SIZE    3300
...
