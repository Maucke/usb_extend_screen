/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 屏幕提示（OSD）：把简单文字画到 LCD 上，用于首屏连接提示、驱动提示
 * 以及 PC 端画面分辨率不匹配提示。不依赖 LVGL，直接用 8x8 ASCII 点阵字体。
 *
 * 这里先把整屏内容画到一块 RGB565 画布，最后一次性全屏刷到面板，
 * 和 JPEG 帧的显示路径完全一致，避免逐行局部刷新带来的 DMA / 窗口问题。
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_lcd.h"
#include "app_osd.h"
#include "esp_check.h"
#include "esp_log.h"
#include "font8x8_basic.h"
#include "lcd_amoled.h"

static const char *TAG = "app_osd";

#define OSD_COLOR_BLACK 0x0000
#define OSD_COLOR_WHITE 0xFFFF
#define OSD_COLOR_RED   0xF800
#define OSD_COLOR_GRAY  0xC618

/* app_lcd.c 统一使用 JPEG_PIXEL_FORMAT_RGB565_BE，面板也按大端取像素，
 * 所以这里手写的 RGB565 常量需要交换字节后再放进画布。 */
static inline uint16_t osd_rgb565(uint16_t rgb565)
{
    return (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
}

static uint16_t s_width;
static uint16_t s_height;
static uint16_t *s_canvas; /* 整屏 RGB565 画布 */

/* USB 回调只写这些字段，绘图任务负责消费 */
static volatile bool s_pending;
static volatile uint16_t s_pending_width;
static volatile uint16_t s_pending_height;

/* 上一次已经画上屏的分辨率错误内容，用于去重 */
static uint16_t s_shown_width;
static uint16_t s_shown_height;
static bool s_shown_valid;

/* 首屏 / USB 连接状态提示 */
typedef enum {
    OSD_SPLASH_NONE = 0,    /* 不显示提示（已收到画面或总线挂起） */
    OSD_SPLASH_CONNECT,     /* USB 未连接：提示连接 PC */
    OSD_SPLASH_WAITING,     /* USB 已连接但还没收到画面：提示检查驱动 */
} osd_splash_t;

static volatile app_osd_usb_state_t s_usb_state = APP_OSD_USB_STATE_DISCONNECTED;
static volatile bool s_usb_event_pending;
static bool s_frame_seen;      /* 本次启动后是否已经成功显示过画面 */
static osd_splash_t s_splash_shown;

static int osd_block_top(int block_height)
{
    int top = (s_height - block_height) / 2;
    return (top < 8) ? 8 : top;
}

static void osd_clear(void)
{
    memset(s_canvas, 0, (size_t)s_width * s_height * sizeof(uint16_t));
}

static void osd_flush(void)
{
    lcd_amoled_draw(0, 0, s_width, s_height, s_canvas);
}

static void osd_draw_hline(int y, int thickness, uint16_t rgb565)
{
    const uint16_t color = osd_rgb565(rgb565);

    for (int row = 0; row < thickness; row++) {
        const int py = y + row;
        if (py < 0 || py >= s_height) {
            continue;
        }
        uint16_t *dst = s_canvas + (size_t)py * s_width;
        for (int x = 0; x < s_width; x++) {
            dst[x] = color;
        }
    }
}

/* 从 (x, y) 开始画一行文字，scale 为整数放大倍数。 */
static void osd_draw_text_line(int x, int y, const char *text, int scale, uint16_t rgb565)
{
    if (x < 0) {
        x = 0;
    }
    const int text_len = (int)strlen(text);
    const uint16_t color = osd_rgb565(rgb565);

    for (int row = 0; row < FONT8X8_HEIGHT * scale; row++) {
        const int py = y + row;
        if (py < 0 || py >= s_height) {
            continue;
        }

        const int glyph_row = row / scale;
        uint16_t *dst = s_canvas + (size_t)py * s_width;

        for (int i = 0; i < text_len; i++) {
            unsigned char c = (unsigned char)text[i];
            if (c < FONT8X8_FIRST_CHAR || c > FONT8X8_LAST_CHAR) {
                c = '?';
            }
            const uint8_t bits = font8x8_basic[c - FONT8X8_FIRST_CHAR][glyph_row];
            for (int bit = 0; bit < FONT8X8_WIDTH; bit++) {
                if ((bits & (1u << bit)) == 0) {
                    continue;
                }
                for (int sx = 0; sx < scale; sx++) {
                    const int px = x + (i * FONT8X8_WIDTH + bit) * scale + sx;
                    if (px >= 0 && px < s_width) {
                        dst[px] = color;
                    }
                }
            }
        }
    }
}

static void osd_draw_text_centered(int y, const char *text, int scale, uint16_t rgb565)
{
    const int text_width = (int)strlen(text) * FONT8X8_WIDTH * scale;
    int x = (s_width - text_width) / 2;
    if (x < 0) {
        x = 0;
    }
    osd_draw_text_line(x, y, text, scale, rgb565);
}

static void osd_draw_connect_splash(void)
{
    const int top = osd_block_top(128);

    osd_clear();
    osd_draw_text_centered(top, "USB EXTEND SCREEN", 3, OSD_COLOR_WHITE);
    osd_draw_hline(top + 40, 2, OSD_COLOR_WHITE);
    osd_draw_text_centered(top + 64, "PLEASE CONNECT TO PC", 2, OSD_COLOR_WHITE);
    osd_draw_text_centered(top + 96, "WAITING FOR USB SIGNAL...", 2, OSD_COLOR_GRAY);
    osd_flush();
}

static void osd_draw_waiting_splash(void)
{
    const int top = osd_block_top(188);

    osd_clear();
    osd_draw_text_centered(top, "USB CONNECTED", 3, OSD_COLOR_WHITE);
    osd_draw_hline(top + 40, 2, OSD_COLOR_WHITE);
    osd_draw_text_centered(top + 64, "WAITING FOR PC SIGNAL...", 2, OSD_COLOR_WHITE);
    osd_draw_text_centered(top + 100, "IF THE SCREEN STAYS BLACK,", 2, OSD_COLOR_GRAY);
    osd_draw_text_centered(top + 128, "PLEASE CHECK / INSTALL THE", 2, OSD_COLOR_GRAY);
    osd_draw_text_centered(top + 156, "USB DISPLAY DRIVER", 2, OSD_COLOR_GRAY);
    osd_flush();
}

static void osd_draw_resolution_error(uint16_t input_width, uint16_t input_height)
{
    char line[48];

    const int top = osd_block_top(176);

    osd_clear();
    osd_draw_text_centered(top, "RESOLUTION ERROR", 3, OSD_COLOR_RED);
    osd_draw_hline(top + 40, 2, OSD_COLOR_WHITE);

    snprintf(line, sizeof(line), "INPUT : %ux%u", input_width, input_height);
    osd_draw_text_centered(top + 64, line, 2, OSD_COLOR_WHITE);

    snprintf(line, sizeof(line), "PANEL : %ux%u", s_width, s_height);
    osd_draw_text_centered(top + 92, line, 2, OSD_COLOR_WHITE);

    osd_draw_text_centered(top + 132, "PLEASE SET THE PC RESOLUTION", 2, OSD_COLOR_GRAY);
    snprintf(line, sizeof(line), "TO %ux%u", s_width, s_height);
    osd_draw_text_centered(top + 160, line, 2, OSD_COLOR_GRAY);
    osd_flush();
}

esp_err_t app_osd_init(void)
{
    ESP_RETURN_ON_ERROR(app_lcd_get_resolution(&s_width, &s_height), TAG,
                        "get display resolution failed");

    /* 复用 app_lcd 的整屏帧缓冲（内部 RAM、DMA 可访问）作为画布。
     * 另申请一块 257KB 的 PSRAM 画布会被 SPI DMA 拒绝，屏幕会直接不亮。 */
    s_canvas = app_lcd_get_frame_buffer();
    ESP_RETURN_ON_FALSE(s_canvas != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "LCD frame buffer is not ready");

    ESP_LOGI(TAG, "OSD ready: %ux%u, borrow LCD frame buffer", s_width, s_height);

    /* 上电首屏：提示用户把板子接到 PC */
    osd_draw_connect_splash();
    s_splash_shown = OSD_SPLASH_CONNECT;
    return ESP_OK;
}

void app_osd_report_usb_state(app_osd_usb_state_t state)
{
    s_usb_state = state;
    s_usb_event_pending = true;
}

void app_osd_report_resolution(uint16_t width, uint16_t height)
{
    s_pending_width = width;
    s_pending_height = height;
    s_pending = true;
}

void app_osd_process(void)
{
    if (s_canvas == NULL) {
        return;
    }

    /* 1. 分辨率错误优先显示 */
    if (s_pending) {
        s_pending = false;
        const uint16_t width = s_pending_width;
        const uint16_t height = s_pending_height;

        if (!(s_shown_valid && width == s_shown_width && height == s_shown_height)) {
            ESP_LOGW(TAG, "input resolution %ux%u does not match panel %ux%u, show hint",
                     width, height, s_width, s_height);
            osd_draw_resolution_error(width, height);

            s_shown_width = width;
            s_shown_height = height;
            s_shown_valid = true;
        }
        return;
    }

    /* 2. 还没收到过正常画面：显示连接 / 驱动提示 */
    if (!s_frame_seen) {
        osd_splash_t want;
        switch (s_usb_state) {
        case APP_OSD_USB_STATE_CONNECTED:
            want = OSD_SPLASH_WAITING;
            break;
        case APP_OSD_USB_STATE_SUSPENDED:
            want = OSD_SPLASH_NONE;
            break;
        default:
            want = OSD_SPLASH_CONNECT;
            break;
        }

        if (!s_usb_event_pending && want == s_splash_shown) {
            return;
        }
        s_usb_event_pending = false;

        if (want == OSD_SPLASH_NONE) {
            osd_clear();
            osd_flush();
        } else if (want != s_splash_shown) {
            if (want == OSD_SPLASH_CONNECT) {
                osd_draw_connect_splash();
            } else {
                osd_draw_waiting_splash();
            }
        }
        s_splash_shown = want;
        return;
    }

    /* 3. 已经显示过画面：USB 拔插 / 挂起时按原行为熄屏，等 PC 重发画面 */
    if (s_usb_event_pending) {
        s_usb_event_pending = false;
        osd_clear();
        osd_flush();
    }
}

void app_osd_on_frame_drawn(void)
{
    /* 正常画面已经覆盖整屏：首屏提示结束，同时丢弃之前的错误请求 */
    s_frame_seen = true;
    s_splash_shown = OSD_SPLASH_NONE;
    s_pending = false;
    s_shown_valid = false;
}
