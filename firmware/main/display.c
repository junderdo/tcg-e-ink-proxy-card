#include "display.h"

#include <stdatomic.h>
#include <stdlib.h>
#include "epd_3in6e.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define TASK_STACK_SIZE 4096
#define MIN_REFRESH_INTERVAL_S 180

static const char *TAG = "display";

typedef struct {
    uint8_t *frame;
    display_done_fn done;
} job_t;

static QueueHandle_t s_jobs;
static atomic_bool s_busy;
// Uptime restarts at every boot, so the cooldown also runs from boot: a reboot can't skip it.
static atomic_uint_least32_t s_next_refresh_s = MIN_REFRESH_INTERVAL_S;

static uint32_t uptime_s(void)
{
    return esp_timer_get_time() / 1000000;
}

static esp_err_t render(const uint8_t *frame)
{
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
        err = epd_3in6e_display(frame);
    }

    // The panel must not stay powered between refreshes or it can be damaged.
    epd_3in6e_sleep();
    vTaskDelay(pdMS_TO_TICKS(2000));
    epd_3in6e_close();
    // Counted even on failure: the panel may have been partly driven.
    atomic_store(&s_next_refresh_s, uptime_s() + MIN_REFRESH_INTERVAL_S);
    return err;
}

static void display_task(void *arg)
{
    ESP_LOGI(TAG, "ready, panel untouched until an upload");
    job_t job;
    for (;;) {
        xQueueReceive(s_jobs, &job, portMAX_DELAY);
        esp_err_t err = render(job.frame);
        free(job.frame);
        atomic_store(&s_busy, false);
        job.done(err);
    }
}

esp_err_t display_start(void)
{
    s_jobs = xQueueCreate(1, sizeof(job_t));
    ESP_RETURN_ON_FALSE(s_jobs != NULL, ESP_ERR_NO_MEM, TAG, "queue");
    ESP_RETURN_ON_FALSE(xTaskCreate(display_task, "display", TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

bool display_busy(void)
{
    return atomic_load(&s_busy);
}

uint32_t display_cooldown_s(void)
{
    uint32_t next = atomic_load(&s_next_refresh_s);
    uint32_t now = uptime_s();
    return next > now ? next - now : 0;
}

esp_err_t display_show_new(uint8_t *frame, display_done_fn done)
{
    bool idle = false;
    if (!atomic_compare_exchange_strong(&s_busy, &idle, true)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (display_cooldown_s() > 0) {
        atomic_store(&s_busy, false);
        return ESP_ERR_INVALID_STATE;
    }
    const job_t job = { .frame = frame, .done = done };
    xQueueSend(s_jobs, &job, 0);
    return ESP_OK;
}
