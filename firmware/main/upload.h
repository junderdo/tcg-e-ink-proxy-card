#pragma once

#include <stddef.h>
#include <stdint.h>

/** Sends a status message to the connected client. May be called from any task. */
typedef void (*upload_notify_fn)(const uint8_t *msg, size_t len);

/** The image upload protocol from docs/ble-image-upload.md, independent of the BLE stack. */
void upload_init(upload_notify_fn notify);

/** Handles a write to the control point characteristic. */
void upload_control(const uint8_t *msg, size_t len);

/** Handles a write to the data characteristic. */
void upload_data(const uint8_t *msg, size_t len);

/** Drops any transfer in progress, e.g. on disconnect. */
void upload_cancel(void);
