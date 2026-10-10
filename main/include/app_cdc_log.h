/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 把 ESP_LOG 的输出再镜像一份到 USB CDC 串口（UART 那份保留）。
 *
 * 本板 USB-C 接的是 ESP32-S3 原生 USB（GPIO19/20），也就是这个 CDC 接口，
 * 而 UART0 在排针上 —— 调显示的时候不用再额外接一根调试线。
 *
 * 尽量早调用。USB 枚举完成之前 tud_cdc_connected() 为 false，
 * 那段日志只会出现在 UART 上。
 */
esp_err_t app_cdc_log_init(void);

#ifdef __cplusplus
}
#endif
