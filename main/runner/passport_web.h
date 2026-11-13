#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t passport_web_start(void);
bool passport_web_acquire_https(void);
void passport_web_release_https(void);
