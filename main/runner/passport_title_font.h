#pragma once

#include "lvgl.h"

// Bind the built-in font before creating LVGL labels.
void passport_title_font_init(void);
const lv_font_t *passport_title_font(void);
