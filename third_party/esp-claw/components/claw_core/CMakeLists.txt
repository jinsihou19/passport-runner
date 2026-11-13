idf_component_register(
    SRCS
        "src/claw_core.c"
        "src/claw_version.c"
        "src/claw_paths.c"
        "src/claw_core_agent_loop.c"
        "src/claw_core_context.c"
        "src/claw_core_control.c"
        "src/claw_core_events.c"
        "src/claw_core_messages.c"
        "src/claw_core_context_persist.c"
        "src/claw_core_utils.c"
        "src/claw_task.c"
        "src/claw_core_llm.c"
        "src/llm/claw_llm_runtime.c"
        "src/llm/claw_llm_http_transport.c"
        "src/llm/backends/claw_llm_backend_anthropic.c"
        "src/llm/backends/claw_llm_backend_openai_compatible.c"
        "src/llm/backends/claw_llm_backend_openai_responses.c"
        "src/llm/backends/claw_llm_backend_custom.c"
        "src/llm/media/claw_media_pipeline.c"
    INCLUDE_DIRS
        "include"
        "src"
    REQUIRES
        esp_http_client
        http_reuse
        esp-tls
        freertos
        mbedtls
        claw_utils
        claw_endpoint
        claw_http
    PRIV_REQUIRES
        claw_event_router
)

# Passport vendors a pinned revision rather than an independent ESP-Claw checkout.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CLAW_GIT_VERSION="1183aa7-passport" CLAW_CORE_LOG_SNIPPET_LEN=0)
