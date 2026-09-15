idf_component_register(SRCS "main.c" "ble.c" "display.c" "image_store.c" "upload.c"
                       PRIV_REQUIRES epd_3in6e bt nvs_flash esp_partition
                       EMBED_FILES "image.bin")
