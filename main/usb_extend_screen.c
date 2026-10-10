/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "app_usb.h"
#include "usb_descriptors.h"
#include "esp_log.h"
#include "app_cdc_log.h"
#if CONFIG_HID_TOUCH_ENABLE
#include "app_touch.h"
#endif
#include "app_lcd.h"

static const char *TAG = "usb_extend_screen";

void app_main(void)
{
    /* 尽早装挂钩：USB 枚举完成后的日志会同时出现在 USB CDC 串口上。
       这里不 panic —— 日志镜像起不来不该把设备拖死 */
    if (app_cdc_log_init() != ESP_OK) {
        ESP_LOGW(TAG, "USB CDC log mirror unavailable");
    }
    
    ESP_LOGI(TAG, "USB extend screen example");
    ESP_ERROR_CHECK(app_lcd_init());
    ESP_ERROR_CHECK(app_usb_init());
#if CONFIG_HID_TOUCH_ENABLE
    ESP_ERROR_CHECK(app_touch_init());
#endif
}
