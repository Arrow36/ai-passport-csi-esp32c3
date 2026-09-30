#include "csi_scope.h"
#include "bsp_display.h"
#include "lvgl.h"
#include <stdatomic.h>
#include <stdio.h>

LV_FONT_DECLARE(csi_han_12);
LV_FONT_DECLARE(csi_han_14);

static lv_obj_t *s_screen, *s_status, *s_metric, *s_caption, *s_chart;
static lv_obj_t *s_radio, *s_count, *s_battery, *s_setup_box, *s_setup_text, *s_chart_title, *s_bar, *s_hint;
static lv_chart_series_t *s_series;
static atomic_bool s_spectrum_requested, s_hold_requested;
static bool s_spectrum, s_hold;
static uint32_t s_epoch = UINT32_MAX;

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, const char *text, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, &csi_han_14, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

static void on_button(bsp_btn_t button, bsp_btn_ev_t event, void *arg)
{
    if (event == BSP_BTN_CLICK && button == BSP_BTN_UP)
        atomic_store(&s_spectrum_requested, !atomic_load(&s_spectrum_requested));
    if (event == BSP_BTN_CLICK && button == BSP_BTN_DOWN)
        atomic_store(&s_hold_requested, !atomic_load(&s_hold_requested));
    csi_scope_key(button, event, arg);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    csi_view_t view;
    csi_scope_view(&view);
    bool spectrum = atomic_load(&s_spectrum_requested);
    s_hold = atomic_load(&s_hold_requested);
    if (s_spectrum != spectrum || s_epoch != view.epoch) {
        s_epoch = view.epoch;
        s_spectrum = spectrum;
        lv_chart_set_point_count(s_chart, spectrum ? CSI_BINS : 100);
        lv_chart_set_all_values(s_chart, s_series, LV_CHART_POINT_NONE);
        lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, spectrum ? 180 : 130);
    }
    lv_label_set_text(s_chart_title, spectrum ? "CSI 子载波幅度" : "CSI 平均幅度 / 10 秒");
    if (view.battery >= 0) lv_label_set_text_fmt(s_battery, "%d%%", view.battery);
    else lv_label_set_text(s_battery, "USB");
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    if (view.provisioning) {
        lv_obj_remove_flag(s_setup_box, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_setup_text,
            "手机连接设备热点\n\n%s\n\n密码: %s\n\n浏览器打开:\nhttp://192.168.4.1\n\n使用 2.4 GHz 路由器\n密码仅保存在设备上",
            view.ap_name, view.ap_password);
        lv_label_set_text(s_status, view.status);
        lv_label_set_text(s_hint, "长按下键: 清除配网\n已联网时按确定返回");
        return;
    }
    lv_label_set_text(s_hint, "上: 切换 下: 暂停 确定: 校准\n长按确定: 配网");
    lv_obj_add_flag(s_setup_box, LV_OBJ_FLAG_HIDDEN);
    if (!view.connected) {
        lv_label_set_text(s_status, view.status[0] ? view.status : "正在启动无线网络...");
        lv_label_set_text(s_metric, "--");
        lv_label_set_text(s_caption, "等待连接路由器");
    } else if (!view.fresh) {
        lv_label_set_text(s_status, "暂无 CSI，请检查路由器");
        lv_label_set_text(s_metric, "--");
        lv_label_set_text(s_caption, "等待 CSI 数据");
    } else if (!view.calibrated) {
        lv_label_set_text(s_status, "正在校准，请保持环境静止");
        lv_label_set_text_fmt(s_metric, "%u%%", view.calibration_percent);
        lv_label_set_text(s_caption, "正在学习环境背景");
    } else {
        lv_label_set_text(s_status, s_hold ? "曲线已暂停，采样继续" : "实时采样 · 环境相对变化");
        lv_label_set_text_fmt(s_metric, "%02d", (int)(view.score + 0.5f));
        lv_label_set_text(s_caption, "环境扰动 / 100");
    }
    uint32_t color = view.calibrated && view.score > 40 ? 0xFFBD69 : 0x56DCC3;
    lv_obj_set_style_text_color(s_metric, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_bar_set_value(s_bar, view.fresh ? (view.calibrated ? (int)view.score : view.calibration_percent) : 0, LV_ANIM_OFF);
    if (!s_hold) {
        if (s_spectrum) {
            for (unsigned i = 0; i < CSI_BINS; ++i)
                lv_chart_set_series_value_by_id(s_chart, s_series, i, view.fresh ? (int)view.spectrum[i] : LV_CHART_POINT_NONE);
            lv_chart_refresh(s_chart);
        } else lv_chart_set_next_value(s_chart, s_series, view.fresh ? (int)view.amplitude : LV_CHART_POINT_NONE);
    }
    if (view.fresh) lv_label_set_text_fmt(s_radio, "信道 %u  %d dBm  %lu 包/秒", view.channel, view.rssi, (unsigned long)view.rate);
    else lv_label_set_text(s_radio, "信道 --  信号 --  0 包/秒");
    lv_label_set_text_fmt(s_count, "接收 %lu  丢弃 %lu  无效 %lu", (unsigned long)view.frames,
                         (unsigned long)view.dropped, (unsigned long)view.rejected);
}

bool csi_scope_ui_init(void)
{
    if (!bsp_lvgl_lock(1000)) return false;
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x08111C), 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    label(s_screen, 18, 15, 165, "CSI / 环境感知", 0x56DCC3);
    s_battery = label(s_screen, 185, 15, 36, "USB", 0xA6B8C8);
    s_status = label(s_screen, 16, 39, 208, "正在启动...", 0xA6B8C8);
    lv_obj_set_style_text_font(s_status, &csi_han_12, 0);
    s_metric = label(s_screen, 16, 58, 208, "--", 0x56DCC3);
    lv_obj_set_style_text_font(s_metric, &lv_font_montserrat_40, 0);
    s_caption = label(s_screen, 18, 106, 204, "环境相对扰动", 0xA6B8C8);
    lv_obj_set_style_text_font(s_caption, &csi_han_12, 0);
    s_bar = lv_bar_create(s_screen);
    lv_obj_set_pos(s_bar, 18, 124);
    lv_obj_set_size(s_bar, 204, 5);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x25384B), LV_PART_MAIN);
    s_chart_title = label(s_screen, 18, 144, 208, "CSI 平均幅度 / 10 秒", 0xA6B8C8);
    lv_obj_set_style_text_font(s_chart_title, &csi_han_12, 0);
    s_chart = lv_chart_create(s_screen);
    lv_obj_set_pos(s_chart, 18, 163);
    lv_obj_set_size(s_chart, 204, 82);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x102030), 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_all(s_chart, 3, 0);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(0x263C50), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(s_chart, 3, 5);
    s_series = lv_chart_add_series(s_chart, lv_color_hex(0x56DCC3), LV_CHART_AXIS_PRIMARY_Y);
    s_radio = label(s_screen, 18, 250, 210, "信道 --  信号 --  0 包/秒", 0xE8F1F8);
    lv_obj_set_style_text_font(s_radio, &csi_han_12, 0);
    s_count = label(s_screen, 18, 267, 210, "接收 0  丢弃 0  无效 0", 0x829CB4);
    lv_obj_set_style_text_font(s_count, &csi_han_12, 0);
    s_hint = label(s_screen, 18, 286, 204, "上: 切换 下: 暂停 确定: 校准\n长按确定: 配网", 0x829CB4);
    lv_obj_set_style_text_font(s_hint, &csi_han_12, 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    s_setup_box = lv_obj_create(s_screen);
    lv_obj_set_pos(s_setup_box, 12, 60);
    lv_obj_set_size(s_setup_box, 216, 224);
    lv_obj_set_style_bg_color(s_setup_box, lv_color_hex(0x102030), 0);
    lv_obj_set_style_border_color(s_setup_box, lv_color_hex(0x29465A), 0);
    lv_obj_set_style_pad_all(s_setup_box, 8, 0);
    lv_obj_remove_flag(s_setup_box, LV_OBJ_FLAG_SCROLLABLE);
    s_setup_text = label(s_setup_box, 0, 0, 195, "", 0xE8F1F8);
    lv_obj_set_style_text_font(s_setup_text, &csi_han_12, 0);
    lv_obj_add_flag(s_setup_box, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(s_screen);
    lv_timer_create(tick, 100, NULL);
    bsp_lvgl_unlock();
    return bsp_button_init(on_button, NULL) == ESP_OK;
}
