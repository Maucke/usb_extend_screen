/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESP_LCD_H
#define ESP_LCD_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the LCD panel.
 *
 * This function initializes the LCD panel with the provided panel handle. It powers on the LCD,
 * installs the LCD driver, configures the bus, and sets up the panel.
 *
 * @return
 *    - ESP_OK: Success
 *    - ESP_FAIL: Failure
 */
esp_err_t app_lcd_init(void);

/**
 * @brief Get the AMOLED panel resolution.
 *
 * @param[out] width Logical display width in pixels.
 * @param[out] height Logical display height in pixels.
 *
 * @return
 *    - ESP_OK: Success
 *    - ESP_ERR_INVALID_ARG: An output pointer is NULL
 *    - ESP_ERR_INVALID_STATE: The display has not been initialized
 */
esp_err_t app_lcd_get_resolution(uint16_t *width, uint16_t *height);

/**
 * @brief 获取内部整屏 RGB565 帧缓冲（DMA 可访问）。
 *
 * 该缓冲平时用于 JPEG 解码输出。OSD 首屏/提示可以在同一个任务里临时借用它，
 * 画完整屏后一次性刷到面板，避免额外申请 257KB 内存。
 *
 * @warning 只能在 transfer_task 中、且当前没有正在解码/刷新的帧时使用。
 *
 * @return 帧缓冲指针；LCD 未初始化时返回 NULL。
 */
uint16_t *app_lcd_get_frame_buffer(void);

/**
 * @brief 解码并显示一帧 JPEG。
 *
 * @return
 *    - ESP_OK: 画面已成功刷到屏幕
 *    - ESP_ERR_INVALID_SIZE: 分辨率与屏幕不一致或 JPEG 头不匹配
 *    - ESP_FAIL: JPEG 解码失败
 *    - 其他: LCD 绘制失败
 */
esp_err_t app_lcd_draw(uint8_t *buf, uint32_t len, uint16_t width, uint16_t height);

#ifdef __cplusplus
}
#endif

#endif
