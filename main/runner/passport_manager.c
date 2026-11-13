#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "passport_apps.h"
#include "passport_launcher.h"
#include "passport_manage_menu.h"
#include "passport_network.h"
#include "passport_weather.h"
#include "passport_web.h"

LV_FONT_DECLARE(passport_cjk_16);

extern const uint8_t passport_wordmark_start[] asm("_binary_passport_wordmark_a8_start");
extern const uint8_t passport_wordmark_end[] asm("_binary_passport_wordmark_a8_end");

enum {
    VISIBLE_APPS = 3, PAGE_WIDTH = 208, HOME_TILE_WIDTH = 64, HOME_TILE_GAP = 8,
    WORDMARK_WIDTH = 176, WORDMARK_HEIGHT = 36,
    INK = 0x000000, TILE = 0x141518, DELETE_SELECTED = 0x913444,
    WHITE = 0xf5f5f4, MUTED = 0xa1a3a7, OUTLINE = 0x323436, HERO_TEXT = 0x141a17,
    THEME_TEXT = 0x1000000,
};

typedef struct {
    unsigned accent;
    unsigned selected;
    unsigned paper;
    unsigned mark;
    unsigned weather;
} palette_t;

static const palette_t palettes[PASSPORT_THEME_COUNT] = {
    [PASSPORT_THEME_GREEN] = {0x24e47c, 0x112b1e, 0xe6f2e9, 0x0e7745, 0x11291d},
    [PASSPORT_THEME_ORANGE] = {0xff684d, 0x39211b, 0xf8ece7, 0xc23820, 0x34201b},
};

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} key_event_t;

typedef struct {
    passport_manage_menu_t menu;
    passport_app_t app;
    size_t generation;
} managed_app_t;

typedef struct {
    passport_launcher_t launcher;
    passport_launcher_prefs_t prefs;
    passport_apps_status_t apps;
    passport_app_t rows[VISIBLE_APPS];
    size_t selection;
    size_t first;
    size_t row_count;
    size_t generation;
    managed_app_t managed;
    int battery;
    bool display;
    bool web_ready;
    const char *notice;
} manager_t;

static const char *TAG = "passport_manager";
static const char PREFS_KEY[] = "launcher_v2";

static lv_style_t s_accent_style;
static lv_style_t s_focus_style;
static uint8_t s_theme = PASSPORT_THEME_COUNT;
static lv_obj_t *s_pages[PAGE_COUNT];
static lv_obj_t *s_status_time;
static lv_obj_t *s_battery;
static lv_obj_t *s_wifi_icon;
static lv_obj_t *s_hint;
static lv_obj_t *s_home_rows[HOME_ITEMS];
static lv_obj_t *s_hero;
static lv_obj_t *s_wordmark;
static lv_obj_t *s_home_clock;
static lv_obj_t *s_home_date;
static lv_obj_t *s_weather_strip;
static lv_obj_t *s_weather_label;
static lv_obj_t *s_weather_temp;
static lv_obj_t *s_app_count;
static lv_obj_t *s_home_space;
static lv_obj_t *s_app_rows[VISIBLE_APPS];
static lv_obj_t *s_names[VISIBLE_APPS];
static lv_obj_t *s_position;
static lv_obj_t *s_space;
static lv_obj_t *s_apps_empty;
static lv_obj_t *s_web_title;
static lv_obj_t *s_web_help;
static lv_obj_t *s_network;
static lv_obj_t *s_ap;
static lv_obj_t *s_address;
static lv_obj_t *s_code;
static lv_obj_t *s_setting_rows[SETTING_ITEMS];
static lv_obj_t *s_home_value;
static lv_obj_t *s_theme_value;
static lv_obj_t *s_brightness_value;
static lv_obj_t *s_manage_title;
static lv_obj_t *s_manage_name;
static lv_obj_t *s_manage_info;
static lv_obj_t *s_manage_rows[2];
static lv_obj_t *s_manage_labels[2];

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, unsigned color,
                       const lv_font_t *font, const char *text)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    if (color == THEME_TEXT) lv_obj_add_style(obj, &s_accent_style, LV_PART_MAIN);
    else lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(obj, text);
    return obj;
}

static void update_text(lv_obj_t *obj, const char *text)
{
    if (strcmp(lv_label_get_text(obj), text) != 0) lv_label_set_text(obj, text);
}

static void text_align(lv_obj_t *obj, lv_text_align_t align)
{
    lv_obj_set_style_text_align(obj, align, LV_PART_MAIN);
}

static lv_obj_t *tile(lv_obj_t *parent, int x, int y, int width, int height, unsigned color)
{
    lv_obj_t *obj = panel(parent, x, y, width, height);
    lv_obj_set_style_radius(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    return obj;
}

static void color_tile(lv_obj_t *obj, unsigned color)
{
    lv_color_t next = lv_color_hex(color);
    if (!lv_color_eq(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), next)) {
        lv_obj_set_style_bg_color(obj, next, LV_PART_MAIN);
    }
}

static void init_home(lv_obj_t *page)
{
    static lv_image_dsc_t logo;
    logo.header.magic = LV_IMAGE_HEADER_MAGIC;
    logo.header.cf = LV_COLOR_FORMAT_A8;
    logo.header.w = WORDMARK_WIDTH;
    logo.header.h = WORDMARK_HEIGHT;
    logo.header.stride = WORDMARK_WIDTH;
    logo.data = passport_wordmark_start;
    logo.data_size = (uint32_t)(passport_wordmark_end - passport_wordmark_start);

    lv_obj_t *hero = tile(page, 0, 0, PAGE_WIDTH, 141, palettes[PASSPORT_THEME_GREEN].paper);
    s_hero = hero;
    lv_obj_set_style_radius(hero, 16, LV_PART_MAIN);
    label(hero, 15, 11, 128, HERO_TEXT, &lv_font_montserrat_14, "Passport Runner");
    s_home_date = label(hero, 147, 11, 46, HERO_TEXT, &lv_font_montserrat_14, "");
    text_align(s_home_date, LV_TEXT_ALIGN_RIGHT);
    s_wordmark = lv_image_create(hero);
    lv_image_set_src(s_wordmark, &logo);
    lv_obj_set_pos(s_wordmark, 16, 40);
    s_home_clock = label(hero, 8, 32, 192, HERO_TEXT, &lv_font_montserrat_48, "--:--");
    text_align(s_home_clock, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(s_home_clock, LV_OBJ_FLAG_HIDDEN);
    s_weather_strip = tile(hero, 10, 91, 188, 40, TILE);
    s_weather_label = label(s_weather_strip, 11, 9, 134, WHITE, &passport_cjk_16, "");
    s_weather_temp = label(s_weather_strip, 118, 8, 59, THEME_TEXT, &lv_font_montserrat_20, "");
    text_align(s_weather_temp, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t *library = tile(page, 0, 151, PAGE_WIDTH, 65, TILE);
    label(library, 14, 9, 104, WHITE, &passport_cjk_16, "应用库");
    s_home_space = label(library, 14, 35, 119, MUTED, &passport_cjk_16, "");
    s_app_count = label(library, 125, 6, 69, THEME_TEXT, &lv_font_montserrat_48, "");
    text_align(s_app_count, LV_TEXT_ALIGN_RIGHT);

    static const char *const titles[HOME_ITEMS] = {"开发", "应用", "设置"};
    static const char *const icons[HOME_ITEMS] = {LV_SYMBOL_UPLOAD, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS};
    for (size_t i = 0; i < HOME_ITEMS; ++i) {
        int x = (int)i * (HOME_TILE_WIDTH + HOME_TILE_GAP);
        lv_obj_t *row = tile(page, x, 226, HOME_TILE_WIDTH, 57, TILE);
        s_home_rows[i] = row;
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_hex(OUTLINE), LV_PART_MAIN);
        lv_obj_add_style(row, &s_focus_style, LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_STATE_TRICKLE);
        int inner_width = HOME_TILE_WIDTH - 2;
        lv_obj_t *icon = label(row, 0, 6, inner_width, WHITE, &lv_font_montserrat_20, icons[i]);
        text_align(icon, LV_TEXT_ALIGN_CENTER);
        lv_obj_add_style(icon, &s_accent_style, LV_STATE_FOCUSED);
        text_align(label(row, 0, 29, inner_width, WHITE, &passport_cjk_16, titles[i]), LV_TEXT_ALIGN_CENTER);
    }
}

static void init_apps(lv_obj_t *page, const lv_font_t *font)
{
    label(page, 0, 0, 125, THEME_TEXT, font, "应用");
    s_position = label(page, 125, 0, 83, MUTED, font, "");
    text_align(s_position, LV_TEXT_ALIGN_RIGHT);
    for (size_t i = 0; i < VISIBLE_APPS; ++i) {
        s_app_rows[i] = tile(page, 0, 29 + (int)i * 51, PAGE_WIDTH, 43, TILE);
        s_names[i] = label(s_app_rows[i], 12, 10, 184, WHITE, font, "");
        lv_obj_set_height(s_names[i], 23);
        lv_label_set_long_mode(s_names[i], LV_LABEL_LONG_MODE_DOTS);
    }
    s_apps_empty = label(page, 0, 53, PAGE_WIDTH, MUTED, font, "");
    lv_label_set_long_mode(s_apps_empty, LV_LABEL_LONG_MODE_WRAP);
    s_space = label(page, 0, 194, PAGE_WIDTH, THEME_TEXT, font, "");
}

static void init_web(lv_obj_t *page, const lv_font_t *font)
{
    s_web_title = label(page, 0, 0, PAGE_WIDTH, THEME_TEXT, font, "");
    s_web_help = label(page, 0, 28, PAGE_WIDTH, WHITE, font, "");
    lv_obj_t *card = tile(page, 0, 61, PAGE_WIDTH, 149, TILE);
    s_network = label(card, 12, 12, 184, THEME_TEXT, font, "");
    s_ap = label(card, 12, 43, 184, MUTED, font, "");
    s_address = label(card, 12, 75, 184, WHITE, font, "");
    s_code = label(card, 12, 111, 184, WHITE, font, "");
}

static void init_settings(lv_obj_t *page, const lv_font_t *font)
{
    label(page, 0, 0, PAGE_WIDTH, THEME_TEXT, font, "设置");
    static const char *const labels[SETTING_ITEMS] = {"首页展示", "主题色", "屏幕亮度", "Wi-Fi 设置"};
    for (size_t i = 0; i < SETTING_ITEMS; ++i) {
        s_setting_rows[i] = tile(page, 0, 29 + (int)i * 46, PAGE_WIDTH, 40, TILE);
        label(s_setting_rows[i], 12, 8, 98, WHITE, font, labels[i]);
    }
    s_home_value = label(s_setting_rows[SETTING_HOME], 108, 8, 88, THEME_TEXT, font, "");
    s_theme_value = label(s_setting_rows[SETTING_THEME], 108, 8, 88, THEME_TEXT, font, "");
    s_brightness_value = label(s_setting_rows[SETTING_BRIGHTNESS], 108, 8, 88, THEME_TEXT, font, "");
    text_align(s_home_value, LV_TEXT_ALIGN_RIGHT);
    text_align(s_theme_value, LV_TEXT_ALIGN_RIGHT);
    text_align(s_brightness_value, LV_TEXT_ALIGN_RIGHT);
    label(s_setting_rows[SETTING_WIFI], 181, 11, 15, THEME_TEXT, &lv_font_montserrat_14, LV_SYMBOL_RIGHT);
}

static void init_management(lv_obj_t *page, const lv_font_t *font)
{
    s_manage_title = label(page, 0, 0, PAGE_WIDTH, THEME_TEXT, font, "");
    s_manage_name = label(page, 0, 30, PAGE_WIDTH, WHITE, font, "");
    lv_label_set_long_mode(s_manage_name, LV_LABEL_LONG_MODE_WRAP);
    s_manage_info = label(page, 0, 80, PAGE_WIDTH, MUTED, font, "");
    for (size_t i = 0; i < 2; ++i) {
        s_manage_rows[i] = tile(page, 0, 130 + (int)i * 44, PAGE_WIDTH, 38, TILE);
        s_manage_labels[i] = label(s_manage_rows[i], 12, 8, 184, WHITE, font, "");
    }
}

static bool init_display(uint8_t brightness)
{
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init() || !bsp_lvgl_lock(1000)) return false;
    lv_style_init(&s_accent_style);
    lv_style_init(&s_focus_style);
    lv_obj_t *root = lv_screen_active();
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(INK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    s_status_time = label(root, 18, 9, 62, MUTED, &lv_font_montserrat_14, "--:--");
    s_wifi_icon = label(root, 133, 10, 35, MUTED, &lv_font_montserrat_14, "");
    s_battery = label(root, 168, 10, 54, MUTED, &lv_font_montserrat_14, "--");
    text_align(s_battery, LV_TEXT_ALIGN_RIGHT);

    for (size_t i = 0; i < PAGE_COUNT; ++i) {
        if (i == WIFI_PAGE) continue;
        if (i == HOME_PAGE) {
            s_pages[i] = panel(root, 16, 26, PAGE_WIDTH, 283);
        } else {
            s_pages[i] = panel(root, 16, 50, PAGE_WIDTH, 222);
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    const lv_font_t *font = &passport_cjk_16;
    init_home(s_pages[HOME_PAGE]);
    init_apps(s_pages[APPS_PAGE], font);
    init_web(s_pages[DEVELOP_PAGE], font);
    init_settings(s_pages[SETTINGS_PAGE], font);
    init_management(s_pages[MANAGE_PAGE], font);
    s_hint = label(root, 16, 271, PAGE_WIDTH, MUTED, font, "");
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    bsp_lvgl_unlock();
    bsp_display_backlight(brightness);
    return true;
}

static const char *app_label(const passport_app_t *app)
{
    const char *name = app->name + sizeof("Passport-") - 1;
    return strcmp(name, "Pomodoro") == 0 ? "番茄钟" : name;
}

static void apply_theme(uint8_t theme)
{
    if (theme == s_theme) return;
    const palette_t *palette = &palettes[theme];
    lv_color_t accent = lv_color_hex(palette->accent);
    lv_style_set_text_color(&s_accent_style, accent);
    lv_style_set_border_color(&s_focus_style, accent);
    lv_style_set_bg_color(&s_focus_style, lv_color_hex(palette->selected));
    lv_obj_report_style_change(&s_accent_style);
    lv_obj_report_style_change(&s_focus_style);
    color_tile(s_hero, palette->paper);
    color_tile(s_weather_strip, palette->weather);
    lv_obj_set_style_image_recolor(s_wordmark, lv_color_hex(palette->mark), LV_PART_MAIN);
    s_theme = theme;
}

static void draw_home(const manager_t *manager, const passport_launcher_clock_t *clock,
                      const passport_weather_status_t *weather, bool online)
{
    bool show_clock = manager->prefs.show_clock;
    lv_obj_set_flag(s_wordmark, LV_OBJ_FLAG_HIDDEN, show_clock);
    lv_obj_set_flag(s_home_clock, LV_OBJ_FLAG_HIDDEN, !show_clock);
    update_text(s_home_clock, clock->time);
    update_text(s_home_date, clock->date);

    static const char *const conditions[] = {"天气", "晴", "多云", "雾", "有雨", "有雪", "雷雨"};
    const char *weather_text = "在网页设置天气";
    char temp[16] = "";
    if (weather->configured) {
        weather_text = online && weather->pending ? "正在更新天气" : "天气暂不可用";
    }
    if (weather->ready) {
        weather_text = conditions[weather->condition];
        snprintf(temp, sizeof(temp), "%d\xc2\xb0" "C", weather->temperature);
    }
    update_text(s_weather_label, weather_text);
    update_text(s_weather_temp, temp);

    char line[32];
    const lv_font_t *count_font = manager->apps.count >= 100 ? &lv_font_montserrat_20 : &lv_font_montserrat_48;
    if (lv_obj_get_style_text_font(s_app_count, LV_PART_MAIN) != count_font) {
        lv_obj_set_style_text_font(s_app_count, count_font, LV_PART_MAIN);
    }
    if (manager->apps.ready) {
        snprintf(line, sizeof(line), "%zu", manager->apps.count);
        update_text(s_app_count, line);
        snprintf(line, sizeof(line), "剩余 %.1f MiB", manager->apps.free_bytes / 1048576.0);
    } else {
        update_text(s_app_count, "--");
        strlcpy(line, "存储不可用", sizeof(line));
    }
    update_text(s_home_space, line);
    for (size_t i = 0; i < HOME_ITEMS; ++i) {
        lv_obj_set_state(s_home_rows[i], LV_STATE_FOCUSED, i == manager->launcher.home_item);
    }
}

static void draw_apps(const manager_t *manager)
{
    const passport_apps_status_t *apps = &manager->apps;
    char line[42] = "";
    if (apps->count) snprintf(line, sizeof(line), "%zu / %zu", manager->selection + 1, apps->count);
    update_text(s_position, line);
    lv_obj_set_flag(s_apps_empty, LV_OBJ_FLAG_HIDDEN, apps->count > 0);
    if (!apps->count) {
        update_text(s_apps_empty, apps->ready ? "还没有应用\n从「开发」进入网页安装" : "应用存储不可用");
    }
    for (size_t i = 0; i < VISIBLE_APPS; ++i) {
        lv_obj_set_flag(s_app_rows[i], LV_OBJ_FLAG_HIDDEN, i >= manager->row_count);
        if (i >= manager->row_count) continue;
        color_tile(s_app_rows[i], manager->selection == manager->first + i ? palettes[s_theme].selected : TILE);
        update_text(s_names[i], app_label(&manager->rows[i]));
    }
    if (apps->ready) snprintf(line, sizeof(line), "剩余 %zu KiB", apps->free_bytes / 1024);
    else line[0] = '\0';
    update_text(s_space, line);
}

static void draw_web(const manager_t *manager, const passport_network_status_t *net, const char *network)
{
    bool settings = manager->launcher.page == WIFI_PAGE;
    update_text(s_web_title, settings ? "Wi-Fi 设置" : "开发");
    update_text(s_web_help, settings ? "在网页 Wi-Fi 设置中配网" : "电脑编译后，从网页上传");
    update_text(s_network, manager->web_ready ? network : "网页服务不可用");
    char line[42];
    if (net->state == PASSPORT_NET_SETUP) snprintf(line, sizeof(line), "加入 %s", passport_network_ap());
    else strlcpy(line, "连接同一 Wi-Fi", sizeof(line));
    update_text(s_ap, line);
    if (net->ip[0]) snprintf(line, sizeof(line), "http://%s", net->ip);
    else strlcpy(line, "等待地址", sizeof(line));
    update_text(s_address, line);
    if (passport_network_code()[0]) snprintf(line, sizeof(line), "连接码 %s", passport_network_code());
    else line[0] = '\0';
    update_text(s_code, line);
}

static void draw_settings(const manager_t *manager)
{
    char percent[8];
    snprintf(percent, sizeof(percent), "%u%%", passport_launcher_brightness(&manager->prefs));
    update_text(s_home_value, manager->prefs.show_clock ? "时钟" : "标志");
    update_text(s_theme_value, manager->prefs.theme == PASSPORT_THEME_GREEN ? "Passport 绿" : "橘色");
    update_text(s_brightness_value, percent);
    for (size_t i = 0; i < SETTING_ITEMS; ++i) {
        color_tile(s_setting_rows[i], manager->launcher.setting_item == i ? palettes[s_theme].selected : TILE);
    }
}

static void draw_management(const manager_t *manager)
{
    const managed_app_t *managed = &manager->managed;
    bool confirm = managed->menu.page == PASSPORT_MANAGE_CONFIRM;
    char info[64];
    if (confirm) strlcpy(info, "删除后可从网页重新安装", sizeof(info));
    else snprintf(info, sizeof(info), "大小 %zu KiB\n剩余 %zu KiB",
                  managed->app.size / 1024, manager->apps.free_bytes / 1024);
    update_text(s_manage_title, confirm ? "确认删除" : "管理应用");
    update_text(s_manage_name, app_label(&managed->app));
    update_text(s_manage_info, info);
    update_text(s_manage_labels[0], confirm ? "取消" : "删除应用");
    update_text(s_manage_labels[1], confirm ? "删除" : "返回");
    bool first_selected = managed->menu.delete_selected != confirm;
    unsigned selected = palettes[s_theme].selected;
    color_tile(s_manage_rows[0], first_selected ? selected : TILE);
    color_tile(s_manage_rows[1], first_selected ? TILE : (confirm ? DELETE_SELECTED : selected));
}

static void draw(const manager_t *manager)
{
    static const char *const net_labels[] = {"正在启动", "配网热点", "正在联网", "Wi-Fi 已连接", "网络不可用"};
    static const char *const wifi_icons[] = {
        LV_SYMBOL_WIFI LV_SYMBOL_CLOSE, LV_SYMBOL_WIFI LV_SYMBOL_UPLOAD, LV_SYMBOL_WIFI "...",
        LV_SYMBOL_WIFI, LV_SYMBOL_WIFI LV_SYMBOL_CLOSE,
    };
    passport_network_status_t net;
    passport_network_status(&net);
    passport_launcher_page_t page = manager->launcher.page;
    passport_weather_status_t weather = {0};
    if (page == HOME_PAGE) passport_weather_status(&weather);
    static time_t displayed_minute = -1;
    static passport_launcher_clock_t clock;
    time_t minute = time(NULL) / 60;
    if (minute != displayed_minute) {
        passport_launcher_clock(minute * 60, &clock);
        displayed_minute = minute;
    }
    if (!bsp_lvgl_lock(50)) return;
    apply_theme(manager->prefs.theme);

    static const char *const battery_icons[] = {
        LV_SYMBOL_BATTERY_EMPTY, LV_SYMBOL_BATTERY_1, LV_SYMBOL_BATTERY_2,
        LV_SYMBOL_BATTERY_3, LV_SYMBOL_BATTERY_FULL,
    };
    char battery[16];
    if (manager->battery >= 0 && manager->battery <= 100) {
        snprintf(battery, sizeof(battery), "%s %d%%", battery_icons[manager->battery / 21], manager->battery);
    } else strlcpy(battery, LV_SYMBOL_BATTERY_EMPTY " --", sizeof(battery));
    update_text(s_battery, battery);
    update_text(s_status_time, clock.time);
    update_text(s_wifi_icon, wifi_icons[net.state]);

    passport_launcher_page_t panel_page = page == WIFI_PAGE ? DEVELOP_PAGE : page;
    for (size_t i = 0; i < PAGE_COUNT; ++i) {
        if (s_pages[i]) lv_obj_set_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN, i != panel_page);
    }
    lv_obj_set_flag(s_hint, LV_OBJ_FLAG_HIDDEN, page == HOME_PAGE);
    if (page == HOME_PAGE) {
        draw_home(manager, &clock, &weather, net.state == PASSPORT_NET_ONLINE && !net.changing);
        bsp_lvgl_unlock();
        return;
    }
    const char *hint;
    switch (page) {
    case DEVELOP_PAGE:
    case WIFI_PAGE:
        draw_web(manager, &net, net_labels[net.state]);
        hint = net.state == PASSPORT_NET_SETUP ? "热点密码同连接码" : "长按确定键开启热点";
        break;
    case APPS_PAGE:
        draw_apps(manager);
        hint = manager->apps.count ? "确定键打开  长按管理" : "";
        break;
    case SETTINGS_PAGE:
        draw_settings(manager);
        hint = manager->launcher.setting_item == SETTING_WIFI ? "确定键进入" : "确定键切换";
        break;
    case MANAGE_PAGE:
        draw_management(manager);
        hint = "上/下选择  确定键确认";
        break;
    default:
        hint = "";
        break;
    }
    if (manager->apps.busy && (page == APPS_PAGE || page == MANAGE_PAGE)) hint = "正在处理应用";
    else if (manager->notice) hint = manager->notice;
    char footer[78];
    snprintf(footer, sizeof(footer), "%s%s长按上键返回", hint, hint[0] ? "\n" : "");
    update_text(s_hint, footer);
    bsp_lvgl_unlock();
}

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *context)
{
    if (event != BSP_BTN_CLICK && event != BSP_BTN_LONG) return;
    const key_event_t key = {.button = button, .event = event};
    xQueueSend((QueueHandle_t)context, &key, 0);
}

static void refresh_catalog(manager_t *manager)
{
    if (manager->launcher.page != APPS_PAGE && manager->launcher.page != MANAGE_PAGE) return;
    passport_apps_status_t *apps = &manager->apps;
    if (manager->selection >= apps->count) manager->selection = apps->count ? apps->count - 1 : 0;
    size_t first = passport_launcher_first(manager->selection, manager->first, apps->count, VISIBLE_APPS);
    if (manager->generation == apps->generation && first == manager->first) return;
    manager->first = first;
    manager->row_count = passport_apps_page(first, manager->rows, VISIBLE_APPS, apps);
    manager->generation = apps->generation;
}

static void manage_key(manager_t *manager, bool ok)
{
    managed_app_t *managed = &manager->managed;
    if (passport_manage_click(&managed->menu, ok)) {
        esp_err_t err = passport_apps_remove(managed->app.name, &managed->generation);
        if (err == ESP_OK) manager->notice = "应用已删除";
        else if (err == ESP_ERR_NOT_FOUND) manager->notice = "列表已更新，请重新选择";
        else if (err == ESP_ERR_INVALID_STATE) manager->notice = "请稍后再试";
        else manager->notice = "删除失败";
        passport_apps_status(&manager->apps);
    }
    if (managed->menu.page == PASSPORT_MANAGE_CLOSED) passport_launcher_back(&manager->launcher);
}

static void apps_key(manager_t *manager, const key_event_t *key)
{
    if (key->button != BSP_BTN_OK) {
        if (key->event == BSP_BTN_CLICK) {
            manager->selection = passport_launcher_move(manager->selection, manager->apps.count,
                                                        key->button == BSP_BTN_DOWN);
        }
        return;
    }
    if (manager->apps.busy || manager->selection >= manager->apps.count || manager->selection < manager->first ||
        manager->selection - manager->first >= manager->row_count ||
        manager->generation != manager->apps.generation) return;
    const passport_app_t *app = &manager->rows[manager->selection - manager->first];
    if (key->event == BSP_BTN_LONG) {
        if (!manager->display) return;
        manager->managed.app = *app;
        manager->managed.generation = manager->generation;
        passport_manage_open(&manager->managed.menu);
        manager->launcher.page = MANAGE_PAGE;
    } else {
        manager->apps.busy = true;
        if (manager->display) draw(manager);
        esp_err_t err = passport_apps_launch(app->name);
        ESP_LOGW(TAG, "Launch refused: %s", esp_err_to_name(err));
        passport_apps_status(&manager->apps);
        manager->notice = "应用无法打开";
    }
}

static void change_setting(manager_t *manager)
{
    passport_launcher_prefs_t updated = manager->prefs;
    size_t item = manager->launcher.setting_item;
    passport_launcher_change(&updated, item);
    if (passport_config_write(PREFS_KEY, &updated, sizeof(updated)) != ESP_OK) {
        manager->notice = "设置保存失败，请重试";
        return;
    }
    manager->prefs = updated;
    if (item == SETTING_BRIGHTNESS && manager->display) {
        bsp_display_backlight(passport_launcher_brightness(&updated));
    }
}

static void load_preferences(passport_launcher_prefs_t *prefs)
{
    passport_launcher_prefs_t saved;
    if (passport_config_read(PREFS_KEY, &saved, sizeof(saved)) == ESP_OK && passport_launcher_prefs_valid(&saved)) {
        *prefs = saved;
        return;
    }
    uint8_t old[2];
    if (passport_config_read("launcher_v1", old, sizeof(old)) == ESP_OK) {
        saved = (passport_launcher_prefs_t){.show_clock = old[0], .brightness_step = old[1]};
        if (passport_launcher_prefs_valid(&saved)) *prefs = saved;
    }
}

static void handle_key(manager_t *manager, const key_event_t *key)
{
    manager->notice = NULL;
    passport_launcher_t *launcher = &manager->launcher;
    if (key->button == BSP_BTN_UP && key->event == BSP_BTN_LONG) {
        passport_launcher_back(launcher);
        return;
    }

    bool ok = key->button == BSP_BTN_OK;
    bool click = key->event == BSP_BTN_CLICK;
    bool forward = key->button == BSP_BTN_DOWN;
    switch (launcher->page) {
    case HOME_PAGE:
        if (click && ok) passport_launcher_open(launcher);
        else if (click) launcher->home_item = passport_launcher_move(launcher->home_item, HOME_ITEMS, forward);
        break;
    case DEVELOP_PAGE:
    case WIFI_PAGE:
        if (!ok) break;
        if (click) passport_launcher_back(launcher);
        else if (manager->apps.busy || passport_network_setup(false) != ESP_OK) manager->notice = "请稍后再试";
        break;
    case APPS_PAGE:
        apps_key(manager, key);
        break;
    case SETTINGS_PAGE:
        if (click && !ok) {
            launcher->setting_item = passport_launcher_move(launcher->setting_item, SETTING_ITEMS, forward);
        } else if (click && launcher->setting_item == SETTING_WIFI) {
            passport_launcher_open(launcher);
        } else if (click) {
            change_setting(manager);
        }
        break;
    case MANAGE_PAGE:
        if (click) manage_key(manager, ok);
        break;
    default:
        break;
    }
}

void app_main(void)
{
    manager_t manager = {
        .launcher = {.page = HOME_PAGE, .home_item = HOME_APPS},
        .prefs = {.brightness_step = PASSPORT_DEFAULT_BRIGHTNESS_STEP},
        .battery = -1,
        .generation = SIZE_MAX,
    };
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) {
        load_preferences(&manager.prefs);
        passport_weather_init();
        err = passport_apps_init();
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "App manager unavailable: %s", esp_err_to_name(err));
    manager.display = init_display(passport_launcher_brightness(&manager.prefs));
    err = passport_network_init();
    if (err == ESP_OK) err = passport_web_start();
    manager.web_ready = err == ESP_OK;
    if (!manager.web_ready) ESP_LOGE(TAG, "Web install unavailable: %s", esp_err_to_name(err));

    QueueHandle_t keys = xQueueCreate(6, sizeof(key_event_t));
    if (keys && bsp_button_init(on_key, keys) != ESP_OK) ESP_LOGW(TAG, "Keys unavailable");
    bool battery_available = bsp_battery_init() == ESP_OK;
    ESP_LOGI(TAG, "Ready, display=%d battery=%d", manager.display, battery_available);

    int64_t next_battery = 0;
    for (;;) {
        key_event_t key;
        bool pressed = false;
        if (keys) pressed = xQueueReceive(keys, &key, pdMS_TO_TICKS(100)) == pdTRUE;
        else vTaskDelay(pdMS_TO_TICKS(100));
        passport_network_tick();
        passport_apps_status(&manager.apps);
        if (manager.launcher.page == MANAGE_PAGE && manager.managed.generation != manager.apps.generation) {
            passport_launcher_back(&manager.launcher);
            manager.notice = "列表已更新，请重新选择";
        } else if (pressed) {
            handle_key(&manager, &key);
        }
        refresh_catalog(&manager);
        passport_weather_tick(manager.launcher.page == HOME_PAGE);
        int64_t now = esp_timer_get_time();
        if (battery_available && now >= next_battery) {
            manager.battery = bsp_battery_soc();
            next_battery = now + 5000000;
        }
        if (manager.display) draw(&manager);
    }
}
