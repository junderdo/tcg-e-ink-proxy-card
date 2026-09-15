#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef void (*display_done_fn)(esp_err_t save_err, esp_err_t display_err);

/** Starts the display task. The panel is left untouched until a new frame arrives. */
esp_err_t display_start(void);

/** True while the display task is saving or refreshing and can't take a new frame. */
bool display_busy(void);

/** Seconds until the panel may refresh again, or 0 if it may refresh now. */
uint32_t display_cooldown_s(void);

/**
 * Hands a heap-allocated frame of EPD_3IN6E_BUFFER_SIZE bytes to the display task, which saves it
 * to flash, refreshes the panel, frees it, and then calls done from its own task.
 * Returns ESP_ERR_INVALID_STATE, keeping ownership with the caller, while busy or cooling down.
 */
esp_err_t display_show_new(uint8_t *frame, display_done_fn done);
