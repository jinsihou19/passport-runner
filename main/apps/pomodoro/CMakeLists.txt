idf_component_register(
    SRCS passport_pomodoro.c passport_pomodoro_model.c
    INCLUDE_DIRS "."
    REQUIRES common
    PRIV_REQUIRES esp_timer
)
