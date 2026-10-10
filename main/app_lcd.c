/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_lcd.h"
#include "board_config.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lcd_amoled.h"

static const char *TAG = "app_lcd";

#define LCD_BYTES_PER_PIXEL 2

static void *s_decode_buffer;
static size_t s_decode_buffer_size;
static uint16_t s_lcd_width;
static uint16_t s_lcd_height;

/*
 * 一帧解码结果：536x240 RGB565 要 257KB。优先内部 RAM（DMA 更快），
 * 放不下再退到 PSRAM —— 两者在 S3 上都可供 SPI DMA 读取。
 */
static void *app_lcd_alloc_decode_buffer(size_t size)
{
    void *buf = heap_caps_aligned_calloc(16, 1, size, MALLOC_CAP_DMA);
    if (buf == NULL) {
        buf = heap_caps_aligned_calloc(16, 1, size, MALLOC_CAP_SPIRAM);
    }
    return buf;
}

static jpeg_error_t app_lcd_decode_jpeg(const uint8_t *input, size_t input_len, uint8_t *output,
                                        size_t output_len, uint16_t width, uint16_t height)
{
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    /* 面板在 lcd_amoled.c 里配的是 LCD_RGB_DATA_ENDIAN_BIG，所以整条链路都走大端 RGB565 */
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;

    jpeg_dec_handle_t decoder = NULL;
    jpeg_error_t ret = jpeg_dec_open(&config, &decoder);
    if (ret != JPEG_ERR_OK) {
        return ret;
    }

    jpeg_dec_io_t io = {
        .inbuf = (uint8_t *)input,
        .inbuf_len = input_len,
        .outbuf = output,
    };
    jpeg_dec_header_info_t header = {0};

    ret = jpeg_dec_parse_header(decoder, &io, &header);
    if (ret != JPEG_ERR_OK) {
        goto exit;
    }
    if (header.width != width || header.height != height) {
        ESP_LOGW(TAG, "Drop JPEG with unexpected size: %ux%u, expected %ux%u",
                 header.width, header.height, width, height);
        ret = JPEG_ERR_INVALID_PARAM;
        goto exit;
    }

    int required_output_len = 0;
    ret = jpeg_dec_get_outbuf_len(decoder, &required_output_len);
    if (ret != JPEG_ERR_OK) {
        goto exit;
    }
    if ((size_t)required_output_len > output_len) {
        ESP_LOGW(TAG, "JPEG output too large: %d > %u", required_output_len, (unsigned)output_len);
        ret = JPEG_ERR_NO_MEM;
        goto exit;
    }

    ret = jpeg_dec_process(decoder, &io);

exit:
    jpeg_dec_close(decoder);
    return ret;
}

void app_lcd_draw(uint8_t *buf, uint32_t len, uint16_t width, uint16_t height)
{
    if (width != s_lcd_width || height != s_lcd_height) {
        ESP_LOGW(TAG, "Drop JPEG with unexpected size: %ux%u, expected %ux%u",
                 width, height, s_lcd_width, s_lcd_height);
        return;
    }

    static int fps_count;
    static int64_t start_time;
    if (start_time == 0) {
        start_time = esp_timer_get_time();
    }
    if (++fps_count == 50) {
        int64_t end_time = esp_timer_get_time();
        ESP_LOGI(TAG, "fps: %f", 1000000.0 / ((end_time - start_time) / 50.0));
        start_time = end_time;
        fps_count = 0;
    }

    jpeg_error_t jpeg_ret = app_lcd_decode_jpeg(buf, len, s_decode_buffer, s_decode_buffer_size,
                                                width, height);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGD(TAG, "JPEG decode failed: %d", jpeg_ret);
        return;
    }

    esp_err_t ret = lcd_amoled_draw(0, 0, width, height, s_decode_buffer);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "LCD draw failed: %s", esp_err_to_name(ret));
    }
}

esp_err_t app_lcd_get_resolution(uint16_t *width, uint16_t *height)
{
    ESP_RETURN_ON_FALSE(width && height, ESP_ERR_INVALID_ARG, TAG, "resolution output is NULL");
    ESP_RETURN_ON_FALSE(s_lcd_width && s_lcd_height, ESP_ERR_INVALID_STATE, TAG,
                        "display is not initialized");
    *width = s_lcd_width;
    *height = s_lcd_height;
    return ESP_OK;
}

esp_err_t app_lcd_init(void)
{
    ESP_RETURN_ON_ERROR(lcd_amoled_init(), TAG, "initialize AMOLED failed");

    s_lcd_width = (uint16_t)lcd_amoled_width();
    s_lcd_height = (uint16_t)lcd_amoled_height();
    ESP_RETURN_ON_FALSE(s_lcd_width && s_lcd_height, ESP_ERR_INVALID_SIZE, TAG,
                        "AMOLED reported an invalid resolution");

    s_decode_buffer_size = (size_t)s_lcd_width * s_lcd_height * LCD_BYTES_PER_PIXEL;
    s_decode_buffer = app_lcd_alloc_decode_buffer(s_decode_buffer_size);
    ESP_RETURN_ON_FALSE(s_decode_buffer != NULL, ESP_ERR_NO_MEM, TAG,
                        "allocate %u-byte decode buffer failed", (unsigned)s_decode_buffer_size);

    ESP_LOGI(TAG, "LCD ready: %ux%u, RGB565, decode buffer %u bytes",
             s_lcd_width, s_lcd_height, (unsigned)s_decode_buffer_size);
    return ESP_OK;
}
