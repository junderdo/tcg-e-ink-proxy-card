#include "image_store.h"

#include "epd_3in6e.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_crc.h"

#define PARTITION_LABEL "image"
#define HEADER_MAGIC    0x49474354 // "TCGI"
#define FRAME_OFFSET    0x1000

static const char *TAG = "image_store";

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t crc32;
} header_t;

static const esp_partition_t *find_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION_LABEL);
}

const uint8_t *image_store_map(esp_partition_mmap_handle_t *handle)
{
    const esp_partition_t *part = find_partition();
    if (part == NULL) {
        ESP_LOGW(TAG, "no \"%s\" partition", PARTITION_LABEL);
        return NULL;
    }

    header_t header;
    if (esp_partition_read(part, 0, &header, sizeof(header)) != ESP_OK ||
        header.magic != HEADER_MAGIC || header.size != EPD_3IN6E_BUFFER_SIZE) {
        ESP_LOGI(TAG, "no saved image");
        return NULL;
    }

    const void *frame;
    if (esp_partition_mmap(part, FRAME_OFFSET, header.size, ESP_PARTITION_MMAP_DATA, &frame, handle) != ESP_OK) {
        ESP_LOGE(TAG, "mmap failed");
        return NULL;
    }
    if (esp_rom_crc32_le(0, frame, header.size) != header.crc32) {
        ESP_LOGW(TAG, "saved image is corrupt");
        esp_partition_munmap(*handle);
        return NULL;
    }
    return frame;
}

esp_err_t image_store_save(const uint8_t *frame)
{
    const esp_partition_t *part = find_partition();
    ESP_RETURN_ON_FALSE(part != NULL, ESP_ERR_NOT_FOUND, TAG, "no \"%s\" partition", PARTITION_LABEL);
    ESP_RETURN_ON_FALSE(part->size >= FRAME_OFFSET + EPD_3IN6E_BUFFER_SIZE, ESP_ERR_INVALID_SIZE, TAG,
                        "partition too small");

    const header_t header = {
        .magic = HEADER_MAGIC,
        .size = EPD_3IN6E_BUFFER_SIZE,
        .crc32 = esp_rom_crc32_le(0, frame, EPD_3IN6E_BUFFER_SIZE),
    };
    size_t erase_size = (FRAME_OFFSET + EPD_3IN6E_BUFFER_SIZE + part->erase_size - 1) & ~(part->erase_size - 1);
    ESP_RETURN_ON_ERROR(esp_partition_erase_range(part, 0, erase_size), TAG, "erase");
    ESP_RETURN_ON_ERROR(esp_partition_write(part, FRAME_OFFSET, frame, EPD_3IN6E_BUFFER_SIZE), TAG, "write frame");
    // Header goes last so a power cut mid-save leaves no valid image rather than a torn one.
    ESP_RETURN_ON_ERROR(esp_partition_write(part, 0, &header, sizeof(header)), TAG, "write header");
    ESP_LOGI(TAG, "saved image");
    return ESP_OK;
}
