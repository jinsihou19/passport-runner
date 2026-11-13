#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "passport_agent.h"
#include "passport_network.h"
#include "passport_ui.h"
#include "passport_web.h"

static const char *TAG = "passport";

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} key_event_t;

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *context)
{
    if (event != BSP_BTN_CLICK && event != BSP_BTN_LONG) return;
    key_event_t key = {.button = button, .event = event};
    xQueueSend((QueueHandle_t)context, &key, 0);
}

static void handle_key(key_event_t key, bool *connection)
{
    if (key.button == BSP_BTN_UP && key.event == BSP_BTN_LONG) esp_restart();
    if (key.button != BSP_BTN_OK && key.event == BSP_BTN_CLICK) {
        *connection = !*connection;
        return;
    }
    if (key.button != BSP_BTN_OK) return;
    if (!*connection && key.event == BSP_BTN_CLICK) passport_agent_run_rule();
    if (*connection && key.event == BSP_BTN_LONG) {
        passport_agent_status_t agent;
        passport_agent_status(&agent);
        if (!agent.busy) passport_network_setup(false);
    }
}

void app_main(void)
{
    bool display = bsp_display_init() == ESP_OK && bsp_lvgl_init() && passport_ui_init();
    if (display) bsp_display_backlight(70);
    else ESP_LOGE(TAG, "Display UI initialization failed");

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
    err = passport_agent_init(display);
    if (err != ESP_OK) ESP_LOGE(TAG, "Agent initialization failed: %s", esp_err_to_name(err));
    err = passport_network_init();
    if (err == ESP_OK) err = passport_web_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "Web access unavailable: %s", esp_err_to_name(err));

    // Network startup releases the ADC entropy source before the key driver starts.
    QueueHandle_t keys = xQueueCreate(6, sizeof(key_event_t));
    if (keys) {
        err = bsp_button_init(on_key, keys);
        if (err != ESP_OK) ESP_LOGW(TAG, "Button input unavailable: %s", esp_err_to_name(err));
    } else ESP_LOGW(TAG, "Unable to allocate button queue");
    bool battery = bsp_battery_init() == ESP_OK;
    ESP_LOGI(TAG, "Ready, display=%d battery=%d free=%zu largest=%zu", display, battery,
             heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    bool connection = true;
    int64_t next_battery_read = 0;
    for (;;) {
        key_event_t key;
        if (keys) {
            if (xQueueReceive(keys, &key, pdMS_TO_TICKS(100)) == pdTRUE) handle_key(key, &connection);
        } else vTaskDelay(pdMS_TO_TICKS(100));
        passport_network_tick();
        passport_agent_tick();

        int64_t now = esp_timer_get_time();
        if (battery && now >= next_battery_read) {
            passport_agent_set_battery(bsp_battery_soc());
            next_battery_read = now + 5000000;
        }
        if (display) {
            passport_network_status_t network;
            passport_agent_status_t agent;
            passport_network_status(&network);
            passport_agent_status(&agent);
            passport_ui_update(connection, &network, &agent);
        }
    }
}
