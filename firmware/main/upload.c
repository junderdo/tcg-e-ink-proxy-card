#include "upload.h"

#include <stdlib.h>
#include <string.h>
#include "display.h"
#include "epd_3in6e.h"
#include "esp_log.h"
#include "esp_rom_crc.h"

#define OP_START  0x01
#define OP_COMMIT 0x02
#define OP_ABORT  0x03

#define START_LEN       10
#define DATA_HEADER_LEN 4

#define FORMAT_EPD_3IN6E_4BPP 0x01

#define EVENT_READY     0x01
#define EVENT_PROGRESS  0x02
#define EVENT_VERIFIED  0x03
#define EVENT_DISPLAYED 0x04
#define EVENT_ERROR     0xFF

#define PROGRESS_STEP 8192

typedef enum {
    ERR_NONE = 0x00,
    ERR_INVALID_MESSAGE = 0x01,
    ERR_UNSUPPORTED_FORMAT = 0x02,
    ERR_INVALID_SIZE = 0x03,
    ERR_BUSY = 0x04,
    ERR_NO_TRANSFER = 0x05,
    ERR_BAD_OFFSET = 0x06,
    ERR_OVERFLOW = 0x07,
    ERR_INCOMPLETE = 0x08,
    ERR_CRC_MISMATCH = 0x09,
    ERR_NO_MEMORY = 0x0A,
    ERR_STORAGE_FAILED = 0x0B,
    ERR_DISPLAY_FAILED = 0x0C,
    ERR_COOLDOWN = 0x0D,
} upload_error_t;

static const char *TAG = "upload";

static upload_notify_fn s_notify;
static uint8_t *s_frame;
static uint32_t s_size;
static uint32_t s_crc32;
static uint32_t s_received;
static uint32_t s_next_progress;

static uint32_t read_u32_le(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void send_status(uint8_t event, upload_error_t error, uint32_t value)
{
    const uint8_t msg[] = { event, error, value, value >> 8, value >> 16, value >> 24 };
    s_notify(msg, sizeof(msg));
}

static void reset(void)
{
    free(s_frame);
    s_frame = NULL;
    s_size = 0;
    s_received = 0;
}

/** Every error ends the transfer in progress. */
static void report(upload_error_t error, uint32_t value)
{
    if (error == ERR_NONE) {
        return;
    }
    ESP_LOGW(TAG, "error 0x%02x (value %lu)", error, (unsigned long)value);
    reset();
    send_status(EVENT_ERROR, error, value);
}

static void on_displayed(esp_err_t save_err, esp_err_t display_err)
{
    if (display_err != ESP_OK) {
        send_status(EVENT_ERROR, ERR_DISPLAY_FAILED, 0);
    } else {
        send_status(EVENT_DISPLAYED, save_err == ESP_OK ? ERR_NONE : ERR_STORAGE_FAILED, 0);
    }
}

static upload_error_t start(const uint8_t *msg, size_t len, uint32_t *value)
{
    reset();
    if (len != START_LEN) {
        return ERR_INVALID_MESSAGE;
    }
    uint8_t format = msg[1];
    uint32_t size = read_u32_le(&msg[2]);
    if (format != FORMAT_EPD_3IN6E_4BPP) {
        *value = format;
        return ERR_UNSUPPORTED_FORMAT;
    }
    if (size != EPD_3IN6E_BUFFER_SIZE) {
        *value = EPD_3IN6E_BUFFER_SIZE;
        return ERR_INVALID_SIZE;
    }
    if (display_busy()) {
        return ERR_BUSY;
    }
    *value = display_cooldown_s();
    if (*value > 0) {
        return ERR_COOLDOWN;
    }
    s_frame = malloc(size);
    if (s_frame == NULL) {
        return ERR_NO_MEMORY;
    }

    s_size = size;
    s_crc32 = read_u32_le(&msg[6]);
    s_next_progress = PROGRESS_STEP;
    ESP_LOGI(TAG, "receiving %lu bytes", (unsigned long)size);
    send_status(EVENT_READY, ERR_NONE, size);
    return ERR_NONE;
}

static upload_error_t commit(size_t len, uint32_t *value)
{
    if (len != 1) {
        return ERR_INVALID_MESSAGE;
    }
    if (s_frame == NULL) {
        return ERR_NO_TRANSFER;
    }
    if (s_received != s_size) {
        *value = s_received;
        return ERR_INCOMPLETE;
    }
    uint32_t crc32 = esp_rom_crc32_le(0, s_frame, s_size);
    if (crc32 != s_crc32) {
        *value = crc32;
        return ERR_CRC_MISMATCH;
    }
    if (display_busy()) {
        return ERR_BUSY;
    }
    *value = display_cooldown_s();
    if (*value > 0) {
        return ERR_COOLDOWN;
    }

    // Sent before the hand-off so it always precedes the display task's DISPLAYED.
    send_status(EVENT_VERIFIED, ERR_NONE, s_size);
    if (display_show_new(s_frame, on_displayed) != ESP_OK) {
        return ERR_BUSY;
    }
    ESP_LOGI(TAG, "image verified, handed to display");
    s_frame = NULL;
    reset();
    return ERR_NONE;
}

static upload_error_t receive(const uint8_t *msg, size_t len, uint32_t *value)
{
    if (len < DATA_HEADER_LEN) {
        return ERR_INVALID_MESSAGE;
    }
    if (s_frame == NULL) {
        return ERR_NO_TRANSFER;
    }
    uint32_t offset = read_u32_le(msg);
    size_t n = len - DATA_HEADER_LEN;
    if (offset != s_received) {
        *value = s_received;
        return ERR_BAD_OFFSET;
    }
    if (n > s_size - s_received) {
        *value = s_size;
        return ERR_OVERFLOW;
    }

    memcpy(s_frame + offset, msg + DATA_HEADER_LEN, n);
    s_received += n;
    if (s_received >= s_next_progress || s_received == s_size) {
        s_next_progress = s_received + PROGRESS_STEP;
        send_status(EVENT_PROGRESS, ERR_NONE, s_received);
    }
    return ERR_NONE;
}

void upload_init(upload_notify_fn notify)
{
    s_notify = notify;
}

void upload_control(const uint8_t *msg, size_t len)
{
    uint32_t value = 0;
    switch (len > 0 ? msg[0] : 0) {
    case OP_START:
        report(start(msg, len, &value), value);
        break;
    case OP_COMMIT:
        report(commit(len, &value), value);
        break;
    case OP_ABORT:
        ESP_LOGI(TAG, "aborted by client");
        reset();
        break;
    default:
        report(ERR_INVALID_MESSAGE, 0);
        break;
    }
}

void upload_data(const uint8_t *msg, size_t len)
{
    uint32_t value = 0;
    report(receive(msg, len, &value), value);
}

void upload_cancel(void)
{
    if (s_frame != NULL) {
        ESP_LOGI(TAG, "transfer dropped at %lu/%lu bytes", (unsigned long)s_received, (unsigned long)s_size);
    }
    reset();
}
