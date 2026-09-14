#include <stddef.h>
#include "epd_3in6e.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

extern const uint8_t image_bin_start[] asm("_binary_image_bin_start");
extern const uint8_t image_bin_end[] asm("_binary_image_bin_end");

static const char *TAG = "static_image";

static esp_err_t show_image(void)
{
    size_t size = image_bin_end - image_bin_start;
    if (size != EPD_3IN6E_BUFFER_SIZE) {
        ESP_LOGE(TAG, "image.bin is %u bytes, expected %u; regenerate it with tools/png_to_epd.py",
                 (unsigned)size, (unsigned)EPD_3IN6E_BUFFER_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    const epd_3in6e_pins_t pins = {
        .mosi = CONFIG_EPD_PIN_DIN,
        .sclk = CONFIG_EPD_PIN_CLK,
        .cs = CONFIG_EPD_PIN_CS,
        .dc = CONFIG_EPD_PIN_DC,
        .rst = CONFIG_EPD_PIN_RST,
        .busy = CONFIG_EPD_PIN_BUSY,
        .pwr = CONFIG_EPD_PIN_PWR,
    };
    ESP_RETURN_ON_ERROR(epd_3in6e_open(&pins), TAG, "open");

    esp_err_t err = epd_3in6e_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "refreshing display");
        err = epd_3in6e_display(image_bin_start);
    }

    // The panel must not stay powered between refreshes or it can be damaged.
    epd_3in6e_sleep();
    vTaskDelay(pdMS_TO_TICKS(2000));
    epd_3in6e_close();
    return err;
}

void app_main(void)
{
    if (show_image() == ESP_OK) {
        ESP_LOGI(TAG, "done, panel powered off");
    }
}
