/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_cdc_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tusb.h"

static vprintf_like_t s_prev_vprintf;
static SemaphoreHandle_t s_lock;
static char s_line[256];
/* 最坏情况是整行都是 '\n'，每个都要补一个 '\r'，所以给两倍 */
static char s_out[sizeof(s_line) * 2];

/*
 * ESP_LOG 会从任意任务调用（包括正在持锁、或者在错误路径里的任务），
 * 所以这里只做非阻塞的一小段拷贝就返回：拿不到锁、或者 CDC 发送缓冲不够，
 * 就直接放弃这一行，绝不等待。日志丢一行无所谓，把调用方卡住才是事故。
 */
static int app_cdc_log_vprintf(const char *fmt, va_list args)
{
    if (s_lock && (xSemaphoreTake(s_lock, 0) == pdTRUE)) {
        va_list args_copy;
        va_copy(args_copy, args);
        int len = vsnprintf(s_line, sizeof(s_line), fmt, args_copy);
        va_end(args_copy);

        if ((len > 0) && tud_cdc_connected()) {
            size_t n = ((size_t)len < sizeof(s_line)) ? (size_t)len : (sizeof(s_line) - 1);
            size_t out_len = 0;

            /* ESP_LOG 只给 LF，Windows 串口终端不做转换的话会打成阶梯状 */
            for (size_t i = 0; i < n; i++) {
                if ((s_line[i] == '\n') && ((i == 0) || (s_line[i - 1] != '\r'))) {
                    s_out[out_len++] = '\r';
                }
                s_out[out_len++] = s_line[i];
            }

            if (tud_cdc_write_available() >= out_len) {
                tud_cdc_write(s_out, out_len);
                tud_cdc_write_flush();
            }
        }

        xSemaphoreGive(s_lock);
    }

    return s_prev_vprintf ? s_prev_vprintf(fmt, args) : vprintf(fmt, args);
}

esp_err_t app_cdc_log_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_prev_vprintf = esp_log_set_vprintf(app_cdc_log_vprintf);
    return ESP_OK;
}
