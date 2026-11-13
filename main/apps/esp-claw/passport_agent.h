#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "cJSON.h"
#include "passport_model.h"

typedef enum {
    PASSPORT_RULE_EMPTY,
    PASSPORT_RULE_READY,
    PASSPORT_RULE_RUNNING,
    PASSPORT_RULE_DONE,
    PASSPORT_RULE_FAILED,
} passport_rule_state_t;

typedef struct {
    bool available;
    bool configured;
    bool busy;
    int battery;
    passport_rule_state_t rule;
} passport_agent_status_t;

esp_err_t passport_agent_init(bool display_available);
void passport_agent_set_battery(int battery);
void passport_agent_tick(void);
void passport_agent_status(passport_agent_status_t *status);
bool passport_agent_add_json(cJSON *state);

esp_err_t passport_agent_configure(const passport_model_t *model, bool keep_key);
esp_err_t passport_agent_submit(const char *message, time_t browser_time);
esp_err_t passport_agent_clear_chat(void);
esp_err_t passport_agent_run_rule(void);
esp_err_t passport_agent_clear_rule(void);
