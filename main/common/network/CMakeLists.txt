idf_component_register(
    SRCS passport_input.c passport_network.c passport_web.c
    INCLUDE_DIRS "."
    REQUIRES common esp_http_server
    PRIV_REQUIRES bootloader_support esp_event esp_hw_support esp_netif esp_timer esp_wifi mbedtls
)
