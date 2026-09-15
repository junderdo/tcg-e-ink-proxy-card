idf_component_register(SRCS "main.c" "ble.c"
                       PRIV_REQUIRES epd_3in6e bt nvs_flash
                       EMBED_FILES "image.bin")
