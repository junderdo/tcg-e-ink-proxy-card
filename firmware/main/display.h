#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef void (*display_done_fn)(esp_err_t save_err, esp_err_t display_err);

/** Starts the display task, which first shows the saved image, or the built-in one if none is saved. */
esp_err_t display_start(void);

/** True while the display task is saving or refreshing and can't take a new frame. */
bool display_busy(void);

/**
 * Hands a heap-allocated frame of EPD_3IN6E_BUFFER_SIZE bytes to the display task, which saves it
 * to flash, refreshes the panel, frees it, and then calls done from its own task.
 * Returns ESP_ERR_INVALID_STATE, keeping ownership with the caller, while busy.
 */
esp_err_t display_show_new(uint8_t *frame, display_done_fn done);
