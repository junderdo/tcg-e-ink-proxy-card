#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_partition.h"

/**
 * Maps the last saved frame from the "image" flash partition.
 * Returns NULL if nothing valid is saved; otherwise release it with esp_partition_munmap().
 */
const uint8_t *image_store_map(esp_partition_mmap_handle_t *handle);

/** Replaces the saved frame with one of EPD_3IN6E_BUFFER_SIZE bytes. */
esp_err_t image_store_save(const uint8_t *frame);
