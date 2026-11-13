#pragma once

#include "passport_config.h"

enum {
    PASSPORT_CHAT_BYTES = 768,
    PASSPORT_REPLY_BYTES = 2048,
    PASSPORT_SCRIPT_BYTES = 768,
};

typedef struct {
    char base_url[193];
    char model[97];
    char api_key[385];
} passport_model_t;

bool passport_model_valid(const passport_model_t *model);
