idf_component_register(
    SRCS passport_main.c passport_agent.c passport_lua.c passport_model.c passport_ui.c passport_web.c
    INCLUDE_DIRS "."
    EMBED_TXTFILES passport_web.html
    REQUIRES network
    PRIV_REQUIRES claw_cap claw_core claw_event_router esp_timer
)
