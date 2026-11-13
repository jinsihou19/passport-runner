#include "passport_web.h"
#include "passport_web_internal.h"

#include <stdlib.h>
#include <string.h>

#include "passport_network.h"
#include "esp_heap_caps.h"
#include "passport_agent.h"
extern const char passport_html_start[] asm("_binary_passport_web_html_start");
extern const char passport_html_end[] asm("_binary_passport_web_html_end");

static esp_err_t target_page(httpd_req_t *req)
{
    return passport_web_page(req,
                "default-src 'none'; style-src 'unsafe-inline'; "
                "connect-src 'self'; script-src 'unsafe-inline'; "
                "frame-ancestors 'none'; form-action 'none'; base-uri 'none'",
                passport_html_start, passport_html_end);
}

static esp_err_t model(httpd_req_t *req)
{
    if (!passport_web_authorized(req)) return ESP_OK;
    cJSON *json = passport_web_read_json(req);
    if (!json) return passport_web_invalid_body(req);
    passport_model_t next = {0};
    bool valid = passport_web_copy_field(json, "base_url", next.base_url, sizeof(next.base_url)) &&
                 passport_web_copy_field(json, "model", next.model, sizeof(next.model)) &&
                 passport_web_copy_field(json, "api_key", next.api_key, sizeof(next.api_key));
    bool keep_key = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "keep_key"));
    cJSON_Delete(json);
    return passport_web_action_result(req, valid ? passport_agent_configure(&next, keep_key) : ESP_ERR_INVALID_ARG);
}

static esp_err_t chat(httpd_req_t *req)
{
    if (!passport_web_authorized(req)) return ESP_OK;
    if (req->method == HTTP_DELETE) return passport_web_action_result(req, passport_agent_clear_chat());
    passport_network_status_t network;
    passport_network_status(&network);
    if (network.state != PASSPORT_NET_ONLINE || network.changing) {
        return passport_web_send_json(req, "409 Conflict", "{\"error\":\"network\"}");
    }
    cJSON *json = passport_web_read_json(req);
    if (!json) return passport_web_invalid_body(req);
    const char *message = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "message"));
    const cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(json, "unix_time");
    time_t browser_time = 0;
    if (cJSON_IsNumber(timestamp) && timestamp->valuedouble > 0 && timestamp->valuedouble < 4102444800.0) {
        browser_time = (time_t)timestamp->valuedouble;
    }
    esp_err_t err = message ? passport_agent_submit(message, browser_time) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(json);
    if (err == ESP_ERR_INVALID_RESPONSE) {
        return passport_web_send_json(req, "409 Conflict", "{\"error\":\"clock\"}");
    }
    return passport_web_action_result(req, err);
}

static esp_err_t rule(httpd_req_t *req)
{
    if (!passport_web_authorized(req)) return ESP_OK;
    return passport_web_action_result(req, req->method == HTTP_DELETE ? passport_agent_clear_rule() : passport_agent_run_rule());
}

const passport_web_error_t *passport_web_target_error(esp_err_t err)
{
    static const passport_web_error_t errors[] = {
        {ESP_ERR_NOT_FOUND, "409 Conflict", "{\"error\":\"unconfigured\"}"},
    };
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        if (errors[i].code == err) return &errors[i];
    }
    return NULL;
}

bool passport_web_add_state(httpd_req_t *req, cJSON *json)
{
    (void)req;
    return cJSON_AddStringToObject(json, "web_app", "claw") &&
           cJSON_AddNumberToObject(json, "heap_free", heap_caps_get_free_size(MALLOC_CAP_8BIT)) &&
           cJSON_AddNumberToObject(json, "heap_min", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)) &&
           cJSON_AddNumberToObject(json, "heap_largest", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) &&
           passport_agent_add_json(json);
}

bool passport_web_wifi_busy(void)
{
    passport_agent_status_t agent;
    passport_agent_status(&agent);
    return agent.busy;
}

esp_err_t passport_web_start(void)
{
    static const httpd_uri_t routes[] = {
        {.uri = "/api/model", .method = HTTP_POST, .handler = model},
        {.uri = "/api/chat", .method = HTTP_POST, .handler = chat},
        {.uri = "/api/chat", .method = HTTP_DELETE, .handler = chat},
        {.uri = "/api/rule", .method = HTTP_POST, .handler = rule},
        {.uri = "/api/rule", .method = HTTP_DELETE, .handler = rule},
    };
    return passport_web_start_server(target_page, routes, sizeof(routes) / sizeof(routes[0]));
}
