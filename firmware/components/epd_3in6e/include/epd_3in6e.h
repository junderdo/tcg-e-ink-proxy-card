#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native panel orientation is portrait; frames are 4 bits per pixel, row-major. */
#define EPD_3IN6E_WIDTH       400
#define EPD_3IN6E_HEIGHT      600
#define EPD_3IN6E_BUFFER_SIZE (EPD_3IN6E_WIDTH / 2 * EPD_3IN6E_HEIGHT)

typedef enum {
    EPD_3IN6E_BLACK  = 0x0,
    EPD_3IN6E_WHITE  = 0x1,
    EPD_3IN6E_YELLOW = 0x2,
    EPD_3IN6E_RED    = 0x3,
    EPD_3IN6E_BLUE   = 0x5,
    EPD_3IN6E_GREEN  = 0x6,
} epd_3in6e_color_t;

typedef struct {
    int mosi;
    int sclk;
    int cs;
    int dc;
    int rst;
    int busy;
    int pwr;
} epd_3in6e_pins_t;

/** Configure GPIO and SPI, and switch on panel power. */
esp_err_t epd_3in6e_open(const epd_3in6e_pins_t *pins);

/** Reset and load the controller register settings. Required after sleep. */
esp_err_t epd_3in6e_init(void);

esp_err_t epd_3in6e_clear(epd_3in6e_color_t color);

/** Refresh the panel with a frame of EPD_3IN6E_BUFFER_SIZE bytes. */
esp_err_t epd_3in6e_display(const uint8_t *frame);

/** Put the controller into deep sleep. Wait at least 2 s before epd_3in6e_close(). */
esp_err_t epd_3in6e_sleep(void);

/** Switch off panel power and release SPI. */
void epd_3in6e_close(void);

#ifdef __cplusplus
}
#endif
