idf_component_register(
    SRCS
        "src/claw_cap.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        claw_core
        claw_skill
        freertos
)

# Passport tools return compact status or Lua results.
target_compile_definitions(${COMPONENT_LIB} PRIVATE CLAW_CAP_CORE_OUTPUT_SIZE=512)
