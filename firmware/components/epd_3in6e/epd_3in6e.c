/* Register sequences ported from Waveshare's EPD_3in6e driver (MIT licensed). */

#include "epd_3in6e.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SPI_HOST_ID     SPI2_HOST
#define SPI_FREQ_HZ     (4 * 1000 * 1000)
#define SPI_CHUNK_SIZE  4096
#define BUSY_TIMEOUT_MS 60000

static const char *TAG = "epd_3in6e";

static epd_3in6e_pins_t s_pins;
static spi_device_handle_t s_spi;

static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static esp_err_t spi_write(const uint8_t *data, size_t len)
{
    while (len > 0) {
        size_t n = len < SPI_CHUNK_SIZE ? len : SPI_CHUNK_SIZE;
        spi_transaction_t t = { .length = n * 8, .tx_buffer = data };
        ESP_RETURN_ON_ERROR(spi_device_polling_transmit(s_spi, &t), TAG, "spi transmit failed");
        data += n;
        len -= n;
    }
    return ESP_OK;
}

static esp_err_t send_command(uint8_t cmd)
{
    gpio_set_level(s_pins.dc, 0);
    return spi_write(&cmd, 1);
}

static esp_err_t send_data(const uint8_t *data, size_t len)
{
    gpio_set_level(s_pins.dc, 1);
    return spi_write(data, len);
}

#define SEND(cmd, ...) do {                                                   \
        const uint8_t payload_[] = { __VA_ARGS__ };                           \
        ESP_RETURN_ON_ERROR(send_command(cmd), TAG, "cmd 0x%02x", cmd);       \
        ESP_RETURN_ON_ERROR(send_data(payload_, sizeof(payload_)), TAG, "data 0x%02x", cmd); \
    } while (0)

/* BUSY is low while the controller is working. */
static esp_err_t wait_until_idle(void)
{
    for (int waited = 0; !gpio_get_level(s_pins.busy); waited += 10) {
        if (waited >= BUSY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "busy timeout; check wiring");
            return ESP_ERR_TIMEOUT;
        }
        delay_ms(10);
    }
    delay_ms(100);
    return ESP_OK;
}

static void reset(void)
{
    gpio_set_level(s_pins.rst, 1);
    delay_ms(200);
    gpio_set_level(s_pins.rst, 0);
    delay_ms(20);
    gpio_set_level(s_pins.rst, 1);
    delay_ms(200);
}

static esp_err_t refresh(void)
{
    ESP_RETURN_ON_ERROR(send_command(0x04), TAG, "power on"); // POWER_ON
    ESP_RETURN_ON_ERROR(wait_until_idle(), TAG, "power on");
    delay_ms(200);

    SEND(0x06, 0x6F, 0x1F, 0x16, 0x29); // booster soft start, second setting
    delay_ms(200);

    uint32_t start = esp_log_timestamp();
    SEND(0x12, 0x00); // DISPLAY_REFRESH
    ESP_RETURN_ON_ERROR(wait_until_idle(), TAG, "refresh");
    ESP_LOGI(TAG, "refresh took %lu ms", (unsigned long)(esp_log_timestamp() - start));

    SEND(0x02, 0x00); // POWER_OFF
    return wait_until_idle();
}

esp_err_t epd_3in6e_open(const epd_3in6e_pins_t *pins)
{
    s_pins = *pins;

    gpio_config_t out = {
        .pin_bit_mask = BIT64(pins->dc) | BIT64(pins->rst) | BIT64(pins->pwr),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out), TAG, "output gpio");
    gpio_config_t in = {
        .pin_bit_mask = BIT64(pins->busy),
        .mode = GPIO_MODE_INPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&in), TAG, "busy gpio");

    spi_bus_config_t bus = {
        .mosi_io_num = pins->mosi,
        .miso_io_num = -1,
        .sclk_io_num = pins->sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_CHUNK_SIZE,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI_HOST_ID, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    spi_device_interface_config_t dev = {
        .mode = 0,
        .clock_speed_hz = SPI_FREQ_HZ,
        .spics_io_num = pins->cs,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI_HOST_ID, &dev, &s_spi), TAG, "spi device");

    gpio_set_level(pins->pwr, 1);
    return ESP_OK;
}

esp_err_t epd_3in6e_init(void)
{
    reset();
    ESP_RETURN_ON_ERROR(wait_until_idle(), TAG, "reset");
    delay_ms(30);

    SEND(0xAA, 0x49, 0x55, 0x20, 0x08, 0x09, 0x18);
    SEND(0x01, 0x3F);
    SEND(0x00, 0x5F, 0x69);
    SEND(0x05, 0x40, 0x1F, 0x1F, 0x2C);
    SEND(0x08, 0x6F, 0x1F, 0x1F, 0x22);
    SEND(0x06, 0x6F, 0x1F, 0x17, 0x17); // booster soft start, first setting
    SEND(0x03, 0x00, 0x54, 0x00, 0x44);
    SEND(0x60, 0x02, 0x00);
    SEND(0x30, 0x08); // PLL; required on version 2 controllers
    SEND(0x50, 0x3F);
    SEND(0x61, 0x01, 0x90, 0x02, 0x58); // resolution 400x600
    SEND(0xE3, 0x2F);
    SEND(0x84, 0x01);
    return wait_until_idle();
}

esp_err_t epd_3in6e_clear(epd_3in6e_color_t color)
{
    uint8_t row[EPD_3IN6E_WIDTH / 2];
    memset(row, (color << 4) | color, sizeof(row));

    ESP_RETURN_ON_ERROR(send_command(0x10), TAG, "start frame");
    for (int y = 0; y < EPD_3IN6E_HEIGHT; y++) {
        ESP_RETURN_ON_ERROR(send_data(row, sizeof(row)), TAG, "frame data");
    }
    return refresh();
}

esp_err_t epd_3in6e_display(const uint8_t *frame)
{
    ESP_RETURN_ON_ERROR(send_command(0x10), TAG, "start frame");
    ESP_RETURN_ON_ERROR(send_data(frame, EPD_3IN6E_BUFFER_SIZE), TAG, "frame data");
    return refresh();
}

esp_err_t epd_3in6e_sleep(void)
{
    SEND(0x07, 0xA5); // DEEP_SLEEP
    return ESP_OK;
}

void epd_3in6e_close(void)
{
    gpio_set_level(s_pins.pwr, 0);
    gpio_set_level(s_pins.rst, 0);
    spi_bus_remove_device(s_spi);
    spi_bus_free(SPI_HOST_ID);
    s_spi = NULL;
}
