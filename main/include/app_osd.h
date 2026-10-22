/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化屏幕提示（OSD）模块。
 *
 * 在 app_lcd_init() 之后调用。会在内部 RAM 中申请一行像素的 DMA 缓冲区，
 * 用于把文字逐行刷到屏幕上。
 */
esp_err_t app_osd_init(void);

/** USB 连接状态，用于首屏提示内容。 */
typedef enum {
    APP_OSD_USB_STATE_DISCONNECTED = 0, /**< 未连接 PC：提示插入 USB */
    APP_OSD_USB_STATE_CONNECTED,        /**< 已连接 PC：提示等待画面 / 检查驱动 */
    APP_OSD_USB_STATE_SUSPENDED,        /**< 总线挂起：熄屏，不显示误导性提示 */
} app_osd_usb_state_t;

/**
 * @brief 记录 USB 连接状态变化。
 *
 * 可在 TinyUSB 回调里调用，只记录状态并置位刷新请求；
 * 屏幕刷新由 app_osd_process() 在绘图任务里完成。
 */
void app_osd_report_usb_state(app_osd_usb_state_t state);

/**
 * @brief 记录 PC 端送来的画面分辨率不匹配。
 *
 * 可在 USB 接收回调里调用，只记录参数并置位请求，不做任何耗时操作；
 * 真正的绘制由 app_osd_process() 在绘图任务里完成。
 *
 * @param width  PC 端画面宽度
 * @param height PC 端画面高度
 */
void app_osd_report_resolution(uint16_t width, uint16_t height);

/**
 * @brief 绘制待显示的提示。
 *
 * 在绘图任务上下文调用。同一错误分辨率只绘制一次，避免反复刷屏；
 * 没有待显示内容时立即返回。
 */
void app_osd_process(void);

/**
 * @brief 通知 OSD 模块一帧正常画面已经画上屏。
 *
 * 提示会被正常画面覆盖，之后再次收到相同分辨率的错误画面时需要重新提示。
 */
void app_osd_on_frame_drawn(void);

#ifdef __cplusplus
}
#endif
