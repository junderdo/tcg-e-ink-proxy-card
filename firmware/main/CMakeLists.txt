idf_component_register(SRCS "main.c" "ble.c" "display.c" "upload.c"
                       PRIV_REQUIRES epd_3in6e bt nvs_flash esp_timer)
