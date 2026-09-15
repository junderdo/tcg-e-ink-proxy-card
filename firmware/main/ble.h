#pragma once

#include "esp_err.h"

/** Starts the NimBLE host and advertises as a connectable "TCG Proxy Card". */
esp_err_t ble_start(void);
