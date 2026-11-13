idf_component_register(
    SRCS passport_config.c ../../assets/fonts/passport_cjk_16.c
    INCLUDE_DIRS "."
    REQUIRES bsp nvs_flash
)
