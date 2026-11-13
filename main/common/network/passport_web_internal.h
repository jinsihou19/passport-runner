#pragma once

#include "cJSON.h"
#include "esp_http_server.h"

typedef struct {
    esp_err_t code;
    const char *status;
    const char *body;
} passport_web_error_t;

// Each web target implements these hooks for its own routes and responses.
const passport_web_error_t *passport_web_target_error(esp_err_t err);
bool passport_web_add_state(httpd_req_t *req, cJSON *json);
bool passport_web_wifi_busy(void);

esp_err_t passport_web_start_server(esp_err_t (*page_handler)(httpd_req_t *),
                                    const httpd_uri_t *extra_routes, size_t extra_count);
esp_err_t passport_web_page(httpd_req_t *req, const char *policy, const char *start, const char *end);
esp_err_t passport_web_send_json(httpd_req_t *req, const char *status, const char *body);
bool passport_web_authorized(httpd_req_t *req);
cJSON *passport_web_read_json(httpd_req_t *req);
esp_err_t passport_web_invalid_body(httpd_req_t *req);
bool passport_web_copy_field(cJSON *json, const char *name, char *buffer, size_t size);
esp_err_t passport_web_action_result(httpd_req_t *req, esp_err_t err);
