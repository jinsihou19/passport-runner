#include "passport_ui.h"

#include <stdio.h>
#include <string.h>

#include "bsp_display.h"
#include "esp_log.h"
#include "lvgl.h"

LV_FONT_DECLARE(passport_cjk_16);

enum { INK = 0x121f1e, PANEL = 0x213732, WHITE = 0xf1f7ef, MUTED = 0xb1c6bc, MINT = 0xa8ebcf };

static lv_obj_t *s_battery;
static lv_obj_t *s_network;
static lv_obj_t *s_heading;
static lv_obj_t *s_primary;
static lv_obj_t *s_secondary;
static lv_obj_t *s_detail;
static lv_obj_t *s_hint;

static const char *const NETWORK_TEXT[] = {
    [PASSPORT_NET_STARTING] = "正在启动",
    [PASSPORT_NET_SETUP] = "配网模式",
    [PASSPORT_NET_CONNECTING] = "正在连接",
    [PASSPORT_NET_ONLINE] = "网络已连接",
    [PASSPORT_NET_FAILED] = "网络不可用",
};
static const char *const RULE_TEXT[] = {
    [PASSPORT_RULE_EMPTY] = "暂无按键脚本",
    [PASSPORT_RULE_READY] = "按键脚本已保存",
    [PASSPORT_RULE_RUNNING] = "脚本运行中",
    [PASSPORT_RULE_DONE] = "脚本运行成功",
    [PASSPORT_RULE_FAILED] = "脚本运行失败",
};

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, unsigned color,
                       const lv_font_t *font, const char *text)
{
    lv_obj_t *obj = lv_label_create(parent);
    if (!obj) return NULL;
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(obj, text);
    return obj;
}

static bool check_text_font(lv_obj_t *label, const char *text)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    bool covered = true;
    // Fixed UTF-8 copy and generated ASCII addresses/codes reach these labels.
    const unsigned char *next = (const unsigned char *)text;
    while (*next) {
        uint32_t codepoint = *next++;
        if (codepoint >= 0xc0) {
            unsigned continuation = codepoint < 0xe0 ? 1 : codepoint < 0xf0 ? 2 : 3;
            codepoint &= (1U << (6 - continuation)) - 1;
            for (unsigned i = 0; i < continuation; ++i) codepoint = (codepoint << 6) | (*next++ & 0x3f);
        }
        lv_font_glyph_dsc_t glyph = {0};
        if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.is_placeholder) {
            ESP_LOGE("passport_ui", "Missing glyph U+%04lX", (unsigned long)codepoint);
            covered = false;
        }
    }
    return covered;
}

static void update_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) == 0) return;
    check_text_font(label, text);
    lv_label_set_text(label, text);
}

bool passport_ui_init(void)
{
    if (!bsp_lvgl_lock(1000)) return false;
    lv_obj_t *root = lv_screen_active();
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(INK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    const lv_font_t *cjk = &passport_cjk_16;
    label(root, 18, 17, 114, MINT, &lv_font_montserrat_14, "ESP-CLAW");
    s_battery = label(root, 150, 17, 72, MUTED, &lv_font_montserrat_14, "--");
    if (s_battery) lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    label(root, 18, 43, 204, WHITE, &lv_font_montserrat_20, "AI Passport");
    s_network = label(root, 18, 75, 204, MINT, cjk, "");

    lv_obj_t *panel = lv_obj_create(root);
    if (!panel) {
        bsp_lvgl_unlock();
        return false;
    }
    lv_obj_remove_style_all(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(panel, 16, 106);
    lv_obj_set_size(panel, 208, 145);
    lv_obj_set_style_radius(panel, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    s_heading = label(panel, 12, 13, 184, MINT, cjk, "");
    s_primary = label(panel, 12, 45, 184, WHITE, cjk, "");
    s_secondary = label(panel, 12, 75, 184, WHITE, cjk, "");
    s_detail = label(panel, 12, 109, 184, MUTED, cjk, "");
    s_hint = label(root, 18, 267, 204, WHITE, cjk, "");
    lv_obj_t *footer = label(root, 18, 293, 204, MUTED, cjk, "上/下切页 长按上键退出");
    bool ready = s_battery && s_network && s_heading && s_primary && s_secondary && s_detail && s_hint;
    if (footer) check_text_font(footer, lv_label_get_text(footer));
    bsp_lvgl_unlock();
    return ready;
}

void passport_ui_update(bool connection, const passport_network_status_t *network,
                        const passport_agent_status_t *agent)
{
    if (!bsp_lvgl_lock(50)) return;
    char line[64];
    if (agent->battery >= 0) snprintf(line, sizeof(line), "%d%%", agent->battery);
    else strlcpy(line, "--", sizeof(line));
    update_text(s_battery, line);

    update_text(s_network, NETWORK_TEXT[network->state]);
    if (connection) {
        update_text(s_heading, "设备连接");
        if (network->state == PASSPORT_NET_SETUP) {
            snprintf(line, sizeof(line), "%s", passport_network_ap());
        } else {
            strlcpy(line, network->state == PASSPORT_NET_ONLINE ? "在同一网络中打开" : "等待网络", sizeof(line));
        }
        update_text(s_primary, line);
        if (network->ip[0]) snprintf(line, sizeof(line), "http://%s", network->ip);
        else strlcpy(line, "等待地址", sizeof(line));
        update_text(s_secondary, line);
        snprintf(line, sizeof(line), "连接码 %s", passport_network_code());
        update_text(s_detail, line);
        update_text(s_hint, network->state == PASSPORT_NET_SETUP ? "用手机连接热点" : "长按确定键进入配网");
    } else {
        update_text(s_heading, "本地助手");
        const char *agent_text = !agent->available ? "助手不可用" : agent->busy ? "正在执行" :
                                 !agent->configured ? "等待设置模型" :
                                 network->state == PASSPORT_NET_ONLINE ? "可以发送指令" : "等待网络";
        update_text(s_primary, agent_text);
        update_text(s_secondary, RULE_TEXT[agent->rule]);
        update_text(s_detail, network->ip[0] ? network->ip : "");
        update_text(s_hint, "短按确定键运行脚本");
    }
    bsp_lvgl_unlock();
}
