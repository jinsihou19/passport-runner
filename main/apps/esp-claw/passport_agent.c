#include "passport_agent.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "bsp_display.h"
#include "claw_cap.h"
#include "claw_core.h"
#include "claw_event_router.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "passport_lua.h"

static const char *TAG = "passport_agent";
static const char MODEL_KEY[] = "model_v1";
static const char SCRIPT_KEY[] = "rule_v1";
static const char SYSTEM_PROMPT[] =
    "You are ESP-Claw on a FoloToy AI Passport. Reply in the user's language, concisely. "
    "Use the registered tools for device actions. Never claim an action succeeded if a tool failed. "
    "Bind an OK key script only when the user asks for a reusable physical-key action. "
    "Lua scripts can call passport.battery() (percent or nil) and passport.backlight(10..100). "
    "Use Lua language syntax only. No standard libraries, filesystem or network are exposed.";
static const char KEY_RULE[] =
    "[{\"id\":\"passport-ok\",\"match\":{\"event_type\":\"trigger\",\"event_key\":\"ok\","
    "\"source_cap\":\"passport_keys\"},\"actions\":[{\"type\":\"call_cap\","
    "\"cap\":\"passport_key_script\",\"caller\":\"system\",\"input\":{}}]}]";

typedef enum { AGENT_IDLE, AGENT_BUSY, AGENT_DONE, AGENT_ERROR } agent_state_t;

static SemaphoreHandle_t s_lock;
static claw_core_handle_t s_core;
static bool s_available;
static bool s_display_available;
static atomic_int s_battery = -1;
static atomic_int s_brightness = 70;
static passport_model_t s_model;
static bool s_model_changed;
static agent_state_t s_state;
static passport_rule_state_t s_rule;
static bool s_rule_pending;
static uint32_t s_request_id;
static char s_prompt[PASSPORT_CHAT_BYTES + 1];
static char s_previous_prompt[PASSPORT_CHAT_BYTES + 1];
static char *s_previous_reply;
static char s_error[160];
static char s_script[PASSPORT_SCRIPT_BYTES + 1];
static char s_script_error[160];

static int battery(void)
{
    return atomic_load(&s_battery);
}

static esp_err_t backlight(int percent)
{
    if (!s_display_available) return ESP_ERR_INVALID_STATE;
    bsp_display_backlight((uint8_t)percent);
    atomic_store(&s_brightness, percent);
    return ESP_OK;
}

static const passport_lua_device_t LUA_DEVICE = {.battery = battery, .backlight = backlight};

static esp_err_t status_tool(const char *input, const claw_cap_call_context_t *ctx, char *out, size_t size)
{
    (void)input;
    (void)ctx;
    snprintf(out, size, "{\"battery_percent\":%d,\"backlight_percent\":%d,\"free_heap\":%zu}",
             battery(), atomic_load(&s_brightness), heap_caps_get_free_size(MALLOC_CAP_8BIT));
    return ESP_OK;
}

static esp_err_t backlight_tool(const char *input, const claw_cap_call_context_t *ctx, char *out, size_t size)
{
    (void)ctx;
    cJSON *json = cJSON_ParseWithOpts(input, NULL, true);
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(json, "percent");
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(value) && value->valuedouble >= 10 && value->valuedouble <= 100 &&
        value->valuedouble == value->valueint) {
        err = backlight(value->valueint);
        if (err == ESP_OK) snprintf(out, size, "{\"backlight_percent\":%d}", value->valueint);
    }
    cJSON_Delete(json);
    return err;
}

static esp_err_t lua_tool(const char *input, bool save, char *out, size_t size)
{
    cJSON *json = cJSON_ParseWithOpts(input, NULL, true);
    const char *script = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "script"));
    char detail[160] = {0};
    esp_err_t err = script ? passport_lua_run(script, save, &LUA_DEVICE, detail, sizeof(detail))
                           : ESP_ERR_INVALID_ARG;
    if (err == ESP_OK && save) {
        char next[sizeof(s_script)] = {0};
        strlcpy(next, script, sizeof(next));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        err = passport_config_write(SCRIPT_KEY, next, sizeof(next));
        if (err == ESP_OK) {
            memcpy(s_script, next, sizeof(s_script));
            s_script_error[0] = '\0';
            s_rule = PASSPORT_RULE_READY;
        }
        xSemaphoreGive(s_lock);
    }
    cJSON_Delete(json);
    if (err == ESP_OK) snprintf(out, size, "%s", save ? "OK key script saved" : "Lua script ran successfully");
    else if (detail[0]) snprintf(out, size, "%s", detail);
    return err;
}

static esp_err_t run_lua_tool(const char *input, const claw_cap_call_context_t *ctx, char *out, size_t size)
{
    (void)ctx;
    return lua_tool(input, false, out, size);
}

static esp_err_t bind_lua_tool(const char *input, const claw_cap_call_context_t *ctx, char *out, size_t size)
{
    (void)ctx;
    return lua_tool(input, true, out, size);
}

static esp_err_t key_script_tool(const char *input, const claw_cap_call_context_t *ctx, char *out, size_t size)
{
    (void)input;
    if (!ctx || ctx->caller != CLAW_CAP_CALLER_SYSTEM) return ESP_ERR_NOT_SUPPORTED;

    // The app worker holds the running state while the rule reads the saved script.
    char detail[160] = {0};
    esp_err_t err = passport_lua_run(s_script, false, &LUA_DEVICE, detail, sizeof(detail));
    if (err != ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        strlcpy(s_script_error, detail[0] ? detail : esp_err_to_name(err), sizeof(s_script_error));
        xSemaphoreGive(s_lock);
    }
    snprintf(out, size, "%s", err == ESP_OK ? "OK" : esp_err_to_name(err));
    return err;
}

static const char SCRIPT_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{\"script\":{\"type\":\"string\"}},\"required\":[\"script\"]}";
static const claw_cap_descriptor_t TOOLS[] = {
    {.id = "device_status", .name = "device_status", .description = "Read battery percentage (-1 if unavailable) and backlight.",
     .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM, .input_schema_json = "{\"type\":\"object\",\"properties\":{}}",
     .execute = status_tool},
    {.id = "set_backlight", .name = "set_backlight", .description = "Set screen backlight from 10 to 100 percent.",
     .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
     .input_schema_json = "{\"type\":\"object\",\"properties\":{\"percent\":{\"type\":\"integer\","
                          "\"minimum\":10,\"maximum\":100}},\"required\":[\"percent\"]}",
     .execute = backlight_tool},
    {.id = "run_lua", .name = "run_lua", .description = "Run up to 768 bytes of Lua using only passport.battery() and passport.backlight().",
     .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM, .input_schema_json = SCRIPT_SCHEMA, .execute = run_lua_tool},
    {.id = "bind_ok_lua", .name = "bind_ok_lua", .description = "Save a Lua script to run when the user presses OK on the assistant screen.",
     .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM, .input_schema_json = SCRIPT_SCHEMA, .execute = bind_lua_tool},
    {.id = "passport_key_script", .name = "passport_key_script", .description = "Run the saved script on a local event.",
     .execute = key_script_tool},
};

static esp_err_t register_tools(void)
{
    static const claw_cap_group_t group = {
        .group_id = "passport", .plugin_name = "AI Passport", .version = "1",
        .descriptors = TOOLS, .descriptor_count = sizeof(TOOLS) / sizeof(TOOLS[0]),
    };
    const char *visible[] = {"passport"};
    esp_err_t err = claw_cap_init();
    if (err == ESP_OK) err = claw_cap_register_group(&group);
    if (err == ESP_OK) err = claw_cap_set_llm_visible_groups(visible, 1);
    if (err == ESP_OK) err = claw_cap_start_all();
    return err;
}

static esp_err_t collect_history(const claw_core_request_t *request, claw_core_context_t *context, void *user)
{
    (void)request;
    (void)user;
    context->kind = CLAW_CORE_CONTEXT_KIND_MESSAGES;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_previous_reply) {
        cJSON *messages = cJSON_CreateArray();
        cJSON *prompt = cJSON_CreateObject();
        cJSON *reply = cJSON_CreateObject();
        if (cJSON_AddStringToObject(prompt, "role", "user") &&
            cJSON_AddStringToObject(prompt, "content", s_previous_prompt) &&
            cJSON_AddStringToObject(reply, "role", "assistant") &&
            cJSON_AddStringToObject(reply, "content", s_previous_reply) &&
            cJSON_AddItemToArray(messages, prompt)) {
            prompt = NULL;
            if (cJSON_AddItemToArray(messages, reply)) {
                reply = NULL;
                context->content = cJSON_PrintUnformatted(messages);
            }
        }
        cJSON_Delete(messages);
        cJSON_Delete(prompt);
        cJSON_Delete(reply);
        xSemaphoreGive(s_lock);
        return context->content ? ESP_OK : ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_lock);
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t ensure_core(void)
{
    const claw_core_config_t config = {
        .backend_type = "openai_compatible", .base_url = s_model.base_url, .model = s_model.model,
        .api_key = s_model.api_key, .auth_type = s_model.api_key[0] ? "bearer" : "none",
        .timeout_ms = 45000, .max_tokens = 384, .supports_tools = true,
        .system_prompt = SYSTEM_PROMPT, .call_cap = claw_cap_call_from_core,
        .task_core = tskNO_AFFINITY, .task_stack_size = 8192,
        .request_queue_len = 1, .response_queue_len = 1, .max_tool_iterations = 4,
        .max_context_providers = 2,
    };
    if (s_core) {
        if (!s_model_changed) return ESP_OK;
        esp_err_t err = claw_core_update_llm_config(s_core, &config);
        if (err == ESP_OK) s_model_changed = false;
        return err;
    }

    const claw_core_context_provider_t history = {
        .name = "recent_chat", .collect = collect_history,
        .flags = CLAW_CORE_CONTEXT_PROVIDER_FLAG_REQUEST_START_ONLY,
    };
    esp_err_t err = claw_core_create(&config, &s_core);
    if (err == ESP_OK) err = claw_core_add_context_provider(s_core, &claw_cap_root_agent_tools_provider);
    if (err == ESP_OK) err = claw_core_add_context_provider(s_core, &history);
    if (err == ESP_OK) err = claw_core_start(s_core);
    if (err != ESP_OK && s_core) {
        claw_core_destroy(s_core);
        s_core = NULL;
    }
    if (err == ESP_OK) s_model_changed = false;
    return err;
}

esp_err_t passport_agent_init(bool display_available)
{
    s_display_available = display_available;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    passport_config_read(MODEL_KEY, &s_model, sizeof(s_model));
    if (!passport_model_valid(&s_model)) memset(&s_model, 0, sizeof(s_model));
    passport_config_read(SCRIPT_KEY, s_script, sizeof(s_script));
    s_script[sizeof(s_script) - 1] = '\0';
    s_rule = s_script[0] ? PASSPORT_RULE_READY : PASSPORT_RULE_EMPTY;
    const claw_event_router_config_t router = {
        .builtin_rules_json = KEY_RULE, .max_rules = 1, .max_actions_per_rule = 1,
        .event_queue_len = 1, .cap_output_size = 192,
    };
    esp_err_t err = register_tools();
    if (err == ESP_OK) err = claw_event_router_init(&router);
    s_available = err == ESP_OK;
    return err;
}

void passport_agent_set_battery(int soc)
{
    atomic_store(&s_battery, soc);
}

static bool busy(void)
{
    return s_state == AGENT_BUSY || s_rule == PASSPORT_RULE_RUNNING;
}

static void clear_chat(void)
{
    free(s_previous_reply);
    s_previous_reply = NULL;
    s_previous_prompt[0] = '\0';
    s_prompt[0] = '\0';
    s_error[0] = '\0';
    s_state = AGENT_IDLE;
}

esp_err_t passport_agent_configure(const passport_model_t *model, bool keep_key)
{
    if (!s_available) return ESP_ERR_INVALID_STATE;
    passport_model_t next = *model;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (keep_key && !next.api_key[0] && !strcmp(s_model.base_url, next.base_url)) {
        strlcpy(next.api_key, s_model.api_key, sizeof(next.api_key));
    }
    esp_err_t err;
    if (busy()) err = ESP_ERR_INVALID_STATE;
    else if (!passport_model_valid(&next)) err = ESP_ERR_INVALID_ARG;
    else err = passport_config_write(MODEL_KEY, &next, sizeof(next));
    if (err == ESP_OK) {
        s_model = next;
        s_model_changed = true;
        clear_chat();
    }
    xSemaphoreGive(s_lock);
    return err;
}

static esp_err_t prepare_model_clock(time_t browser_time)
{
    // A paired browser can initialize the RTC when the network blocks NTP.
    const time_t first_valid_time = 1704067200; // 2024-01-01 UTC
    if (strncmp(s_model.base_url, "https://", 8) != 0 || time(NULL) >= first_valid_time) return ESP_OK;
    if (browser_time < first_valid_time) return ESP_ERR_INVALID_RESPONSE;

    const struct timeval clock = {.tv_sec = browser_time};
    if (settimeofday(&clock, NULL) != 0) return ESP_FAIL;
    ESP_LOGI(TAG, "Initialized TLS clock from paired browser");
    return ESP_OK;
}

esp_err_t passport_agent_submit(const char *message, time_t browser_time)
{
    if (!s_available) return ESP_ERR_INVALID_STATE;
    size_t len = strlen(message);
    if (!len || len > PASSPORT_CHAT_BYTES || passport_utf8_prefix(message, len) != len) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err;
    if (busy()) err = ESP_ERR_INVALID_STATE;
    else if (!s_model.model[0]) err = ESP_ERR_NOT_FOUND;
    else {
        err = prepare_model_clock(browser_time);
        if (err == ESP_OK) err = ensure_core();
    }
    if (err == ESP_OK) {
        const claw_core_request_t request = {
            .request_id = s_request_id == UINT32_MAX ? 1 : s_request_id + 1,
            .session_id = "passport", .user_text = message, .source_channel = "web",
        };
        err = claw_core_start_run(s_core, &request, 0);
        if (err == ESP_OK) {
            s_request_id = request.request_id;
            strlcpy(s_prompt, message, sizeof(s_prompt));
            s_state = AGENT_BUSY;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t passport_agent_clear_chat(void)
{
    if (!s_available) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = busy() ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (err == ESP_OK) clear_chat();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t passport_agent_run_rule(void)
{
    if (!s_available) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = busy() ? ESP_ERR_INVALID_STATE : !s_script[0] ? ESP_ERR_NOT_FOUND : ESP_OK;
    if (err == ESP_OK) {
        s_rule = PASSPORT_RULE_RUNNING;
        s_rule_pending = true;
        s_script_error[0] = '\0';
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t passport_agent_clear_rule(void)
{
    if (!s_available) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = busy() ? ESP_ERR_INVALID_STATE : passport_config_write(SCRIPT_KEY, NULL, 0);
    if (err == ESP_OK) {
        s_script[0] = s_script_error[0] = '\0';
        s_rule = PASSPORT_RULE_EMPTY;
    }
    xSemaphoreGive(s_lock);
    return err;
}

void passport_agent_tick(void)
{
    if (!s_available) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool run_rule = s_rule_pending;
    s_rule_pending = false;
    claw_core_response_t response = {0};
    if (s_state == AGENT_BUSY && claw_core_receive(s_core, &response, 1) == ESP_OK) {
        size_t len = response.text ? passport_utf8_prefix(response.text, PASSPORT_REPLY_BYTES) : 0;
        s_state = response.status == CLAW_CORE_RESPONSE_STATUS_OK && len ? AGENT_DONE : AGENT_ERROR;
        if (s_state == AGENT_DONE) {
            size_t original_length = strlen(response.text);
            response.text[len] = '\0';
            if (len < original_length) {
                char *shortened = realloc(response.text, len + 1);
                if (shortened) response.text = shortened;
            }
            free(s_previous_reply);
            s_previous_reply = response.text;
            response.text = NULL;
            strlcpy(s_previous_prompt, s_prompt, sizeof(s_previous_prompt));
        } else {
            const char *detail = response.error_message ? response.error_message : "Empty model response";
            size_t bytes = passport_utf8_prefix(detail, sizeof(s_error) - 1);
            memcpy(s_error, detail, bytes);
            s_error[bytes] = '\0';
        }
        ESP_LOGI(TAG, "Request %" PRIu32 " finished, status=%s, free=%zu, minimum=%zu, largest=%zu",
                 s_request_id, s_state == AGENT_DONE ? "ok" : "error", heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        claw_core_response_free(&response);
    }
    xSemaphoreGive(s_lock);
    if (run_rule) {
        const claw_event_t event = {.event_type = "trigger", .message_id = "ok", .source_cap = "passport_keys"};
        claw_event_router_result_t result = {0};
        esp_err_t err = claw_event_router_handle_event(&event, &result);
        bool ok = err == ESP_OK && result.matched && result.action_count == 1 && result.failed_actions == 0;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_rule = ok ? PASSPORT_RULE_DONE : PASSPORT_RULE_FAILED;
        if (!ok && !s_script_error[0]) strlcpy(s_script_error, "Rule did not complete", sizeof(s_script_error));
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "OK rule finished, status=%s", ok ? "ok" : "error");
    }
}

void passport_agent_status(passport_agent_status_t *status)
{
    *status = (passport_agent_status_t){.available = s_available, .battery = battery()};
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    status->configured = s_model.model[0] != '\0';
    status->busy = busy();
    status->rule = s_rule;
    xSemaphoreGive(s_lock);
}

bool passport_agent_add_json(cJSON *json)
{
    static const char *const states[] = {"idle", "busy", "done", "error"};
    static const char *const rules[] = {"empty", "ready", "running", "done", "error"};
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = cJSON_AddBoolToObject(json, "available", s_available) &&
        cJSON_AddStringToObject(json, "agent", states[s_state]) &&
        cJSON_AddNumberToObject(json, "request_id", s_request_id) &&
        cJSON_AddStringToObject(json, "prompt", s_prompt) &&
        cJSON_AddStringToObject(json, "reply", s_state == AGENT_DONE ? s_previous_reply : "") &&
        cJSON_AddStringToObject(json, "error", s_state == AGENT_ERROR ? s_error : "") &&
        cJSON_AddStringToObject(json, "model", s_model.model) &&
        cJSON_AddStringToObject(json, "base_url", s_model.base_url) &&
        cJSON_AddBoolToObject(json, "key_saved", s_model.api_key[0] != '\0') &&
        cJSON_AddStringToObject(json, "script", s_script) &&
        cJSON_AddStringToObject(json, "rule", rules[s_rule]) &&
        cJSON_AddStringToObject(json, "rule_error", s_script_error) &&
        cJSON_AddNumberToObject(json, "battery", battery()) &&
        cJSON_AddNumberToObject(json, "backlight", atomic_load(&s_brightness));
    xSemaphoreGive(s_lock);
    return ok;
}
