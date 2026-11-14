idf_component_register(
    SRCS passport_manager.c passport_launcher.c passport_manage_menu.c passport_apps.c
         passport_weather.c passport_web.c passport_title_font.c passport_app_pool.c
    INCLUDE_DIRS "."
    EMBED_FILES assets/passport_wordmark.a8 ../../assets/fonts/runner-cjk.pfnt
    EMBED_TXTFILES passport_manager.html assets/passport_import.mjs assets/passport_hashes.mjs
    REQUIRES network
    PRIV_REQUIRES app_update bootloader_support esp_http_client esp_timer mbedtls joltwallet__littlefs
)
