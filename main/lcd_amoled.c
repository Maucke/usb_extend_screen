/*
 * LilyGO AMOLED（SH8601 QSPI 四线）屏驱动。
 *
 * 只管"把像素画上屏"，不涉及任何协议层。应用侧通过 app_lcd.c 调用本模块。
 */

#include "lcd_amoled.h"
#include "board_config.h"

#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_sh8601.h"

static const char *TAG = "lcd";

#define LCD_BIT_PER_PIXEL (16)
#define LCD_OPCODE_WRITE_CMD (0x02ULL)

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_line_buf; /* 一整行像素，DMA 可访问（fill 用） */

/* 面板初始化序列：先 sleep out，再设方向 / 像素格式 / 窗口 / 亮度 */
static const sh8601_lcd_init_cmd_t vendor_specific_init[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x36, (uint8_t[]){0xF0}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0}, // 16bits-RGB565
    {0x2A, (uint8_t[]){0x00, 0x00, 0x02, 0x17}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x00, 0xEF}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

int lcd_amoled_width(void)
{
    return DISPLAY_WIDTH;
}

int lcd_amoled_height(void)
{
    return DISPLAY_HEIGHT;
}

/* QSPI 下命令要带上 opcode（0x02 = write command），亮度 0x51 */
static void lcd_set_brightness(uint8_t percent)
{
    uint8_t level = (uint8_t)((255u * percent) / 100u);
    uint32_t cmd = ((uint32_t)LCD_OPCODE_WRITE_CMD << 24) | (0x51u << 8);

    esp_lcd_panel_io_tx_param(s_io, cmd, &level, 1);
}

esp_err_t lcd_amoled_init(void)
{
    if (PIN_NUM_LCD_POWER != GPIO_NUM_NC)
    {
        ESP_LOGI(TAG, "enable amoled power");
        ESP_RETURN_ON_ERROR(gpio_set_direction(PIN_NUM_LCD_POWER, GPIO_MODE_OUTPUT), TAG, "power gpio");
        ESP_RETURN_ON_ERROR(gpio_set_level(PIN_NUM_LCD_POWER, 1), TAG, "power on");
    }

    ESP_LOGI(TAG, "init qspi bus");
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = PIN_NUM_LCD_PCLK;
    buscfg.data0_io_num = PIN_NUM_LCD_DATA0;
    buscfg.data1_io_num = PIN_NUM_LCD_DATA1;
    buscfg.data2_io_num = PIN_NUM_LCD_DATA2;
    buscfg.data3_io_num = PIN_NUM_LCD_DATA3;
    buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    buscfg.flags = SPICOMMON_BUSFLAG_QUAD;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    ESP_LOGI(TAG, "install panel io");
    esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(PIN_NUM_LCD_CS, NULL, NULL);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &s_io), TAG, "panel io");

    ESP_LOGI(TAG, "install panel driver");
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = &vendor_specific_init[0],
        .init_cmds_size = sizeof(vendor_specific_init) / sizeof(sh8601_lcd_init_cmd_t),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .flags = {
            .reset_active_high = 0,
        },
        .vendor_config = &vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(s_io, &panel_config, &s_panel), TAG, "panel sh8601");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    esp_lcd_panel_invert_color(s_panel, false);
    /* 面板原生就是 536x240，不需要 swap_xy */
    esp_lcd_panel_mirror(s_panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    esp_lcd_panel_disp_on_off(s_panel, true);

    lcd_set_brightness(100);
    lcd_amoled_fill(0x0000); /* 黑屏 */

    s_line_buf = heap_caps_malloc(DISPLAY_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(s_line_buf != NULL, ESP_ERR_NO_MEM, TAG, "line buffer");

    ESP_LOGI(TAG, "amoled %dx%d ready", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    return ESP_OK;
}

esp_err_t lcd_amoled_draw(int x1, int y1, int x2, int y2, const void *rgb565)
{
    if ((s_panel == NULL) || (rgb565 == NULL))
        return ESP_ERR_INVALID_STATE;

    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 > DISPLAY_WIDTH)
        x2 = DISPLAY_WIDTH;
    if (y2 > DISPLAY_HEIGHT)
        y2 = DISPLAY_HEIGHT;

    if ((x1 >= x2) || (y1 >= y2))
        return ESP_ERR_INVALID_ARG;

    return esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, rgb565);
}

esp_err_t lcd_amoled_fill(uint16_t rgb565)
{
    if ((s_panel == NULL) || (s_line_buf == NULL))
        return ESP_ERR_INVALID_STATE;

    for (int i = 0; i < DISPLAY_WIDTH; i++)
        s_line_buf[i] = rgb565;

    for (int y = 0; y < DISPLAY_HEIGHT; y++)
    {
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISPLAY_WIDTH, y + 1, s_line_buf);
        if (err != ESP_OK)
            return err;
    }

    return ESP_OK;
}

esp_err_t lcd_amoled_power(bool on)
{
    if (PIN_NUM_LCD_POWER == GPIO_NUM_NC)
        return ESP_ERR_NOT_SUPPORTED;

    ESP_RETURN_ON_ERROR(gpio_set_direction(PIN_NUM_LCD_POWER, GPIO_MODE_OUTPUT), TAG, "power gpio");
    ESP_RETURN_ON_ERROR(gpio_set_level(PIN_NUM_LCD_POWER, on ? 1 : 0), TAG, "power level");

    if (!on)
    {
        ESP_LOGI(TAG, "amoled power off");
        return ESP_OK;
    }

    if (s_panel == NULL)
        return ESP_ERR_INVALID_STATE;

    /* 断过电后面板内部寄存器全丢了，光把电拉高不会亮，必须重跑一遍初始化：
       等电源稳 → 复位 → 初始化序列(reset+init) → 方向/开显示 → 亮度 */
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    esp_lcd_panel_invert_color(s_panel, false);
    esp_lcd_panel_mirror(s_panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    esp_lcd_panel_disp_on_off(s_panel, true);
    lcd_set_brightness(100);

    ESP_LOGI(TAG, "amoled power on (re-initialized)");
    return ESP_OK;
}
