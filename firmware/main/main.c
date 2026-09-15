#include "ble.h"
#include "display.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    if (display_start() != ESP_OK) {
        ESP_LOGE(TAG, "display task unavailable");
        return;
    }
    if (ble_start() != ESP_OK) {
        ESP_LOGE(TAG, "BLE unavailable; images can't be uploaded");
    }
}
