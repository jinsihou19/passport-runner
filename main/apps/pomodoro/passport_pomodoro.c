#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "passport_config.h"
#include "passport_pomodoro_model.h"

LV_FONT_DECLARE(passport_cjk_16);

enum { INK = 0x241a1e, TRACK = 0x50323b, WHITE = 0xfff1ea, MUTED = 0xe5bcb6,
       TOMATO = 0xff9984, REST = 0xaee4bb };
static const char TIMER_KEY[] = "pomodoro_v1";

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} key_event_t;

static lv_obj_t *s_phase;
static lv_obj_t *s_clock;
static lv_obj_t *s_progress;
static lv_obj_t *s_action;
static lv_obj_t *s_hint;
static lv_obj_t *s_battery;

static lv_obj_t *label(lv_obj_t *parent, int y, const lv_font_t *font, unsigned color, const char *text)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, 16, y);
    lv_obj_set_width(obj, 208);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(obj, text);
    return obj;
}

static void update_text(lv_obj_t *obj, const char *text)
{
    if (strcmp(lv_label_get_text(obj), text) != 0) lv_label_set_text(obj, text);
}

static bool init_display(void)
{
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init() || !bsp_lvgl_lock(1000)) return false;
    bsp_display_backlight(70);
    lv_obj_t *root = lv_screen_active();
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(INK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    const lv_font_t *font = &passport_cjk_16;
    lv_obj_t *title = label(root, 18, font, WHITE, "番茄钟");
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    s_battery = label(root, 19, &lv_font_montserrat_14, MUTED, "--");
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s_phase = label(root, 80, font, TOMATO, "专注");
    s_clock = label(root, 109, &lv_font_montserrat_48, WHITE, "25:00");
    s_progress = lv_bar_create(root);
    lv_obj_set_pos(s_progress, 30, 188);
    lv_obj_set_size(s_progress, 180, 8);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(TOMATO), LV_PART_INDICATOR);
    s_action = label(root, 218, font, WHITE, "确定键开始");
    s_hint = label(root, 247, font, MUTED, "上/下选择时长");
    label(root, 285, font, MUTED, "长按上键返回应用管理");
    bsp_lvgl_unlock();
    return true;
}

static void draw(const passport_pomodoro_t *timer, int battery)
{
    if (!bsp_lvgl_lock(50)) return;
    uint32_t remaining = timer->state.remaining;
    uint32_t duration = passport_pomodoro_duration(timer);
    bool running = timer->deadline_us != 0;
    bool resting = timer->state.resting != 0;
    bool fresh = remaining == duration;
    char line[16];
    snprintf(line, sizeof(line), "%02u:%02u", (unsigned)(remaining / 60), (unsigned)(remaining % 60));
    update_text(s_clock, line);
    if (battery >= 0) snprintf(line, sizeof(line), "%d%%", battery);
    else strlcpy(line, "--", sizeof(line));
    update_text(s_battery, line);
    update_text(s_phase, resting ? "休息" : "专注");
    lv_color_t accent = lv_color_hex(resting ? REST : TOMATO);
    if (!lv_color_eq(lv_obj_get_style_text_color(s_phase, LV_PART_MAIN), accent)) {
        lv_obj_set_style_text_color(s_phase, accent, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_progress, accent, LV_PART_INDICATOR);
    }
    lv_bar_set_value(s_progress, (int32_t)((duration - remaining) * 100 / duration), LV_ANIM_OFF);
    update_text(s_action, running ? "确定键暂停" : !fresh ? "确定键继续" : resting ? "确定键开始休息" : "确定键开始");
    update_text(s_hint, running ? "" : !resting && fresh ? "上/下选择时长" : "下键重置");
    bsp_lvgl_unlock();
}

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *context)
{
    if (event != BSP_BTN_CLICK && !(button == BSP_BTN_UP && event == BSP_BTN_LONG)) return;
    const key_event_t key = {.button = button, .event = event};
    xQueueSend((QueueHandle_t)context, &key, 0);
}

static void save(const passport_pomodoro_t *timer)
{
    esp_err_t err = passport_config_write(TIMER_KEY, &timer->state, sizeof(timer->state));
    if (err != ESP_OK) ESP_LOGW("passport_pomodoro", "Timer not saved: %s", esp_err_to_name(err));
}

void app_main(void)
{
    passport_pomodoro_t timer = {0};
    if (nvs_flash_init() == ESP_OK) passport_config_read(TIMER_KEY, &timer.state, sizeof(timer.state));
    passport_pomodoro_restore(&timer);
    bool display = init_display();
    QueueHandle_t keys = xQueueCreate(6, sizeof(key_event_t));
    if (keys && bsp_button_init(on_key, keys) != ESP_OK) ESP_LOGW("passport_pomodoro", "Keys unavailable");
    bool battery_available = bsp_battery_init() == ESP_OK;
    ESP_LOGI("passport_pomodoro", "Ready, display=%d battery=%d remaining=%u resting=%u",
             display, battery_available, (unsigned)timer.state.remaining, (unsigned)timer.state.resting);

    int battery = -1;
    int64_t next_battery = 0;
    for (;;) {
        key_event_t key;
        bool pressed = false;
        if (keys) pressed = xQueueReceive(keys, &key, pdMS_TO_TICKS(100)) == pdTRUE;
        else vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now = esp_timer_get_time();
        if (passport_pomodoro_tick(&timer, now)) save(&timer);
        if (pressed && key.event == BSP_BTN_LONG) {
            save(&timer);
            esp_restart();
        }
        if (pressed && key.event == BSP_BTN_CLICK) {
            if (key.button == BSP_BTN_OK) {
                passport_pomodoro_toggle(&timer, now);
                save(&timer);
            } else if (passport_pomodoro_adjust(&timer, key.button == BSP_BTN_DOWN ? 1 : -1)) {
                save(&timer);
            }
        }
        if (battery_available && now >= next_battery) {
            battery = bsp_battery_soc();
            next_battery = now + 5000000;
        }
        if (display) draw(&timer, battery);
    }
}
