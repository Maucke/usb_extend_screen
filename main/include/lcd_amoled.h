#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LilyGO AMOLED 屏驱动（SH8601，QSPI 四线）。
 * 引脚 / 分辨率 / 镜像方向都在 include/board_config.h 里，本模块不暴露内部句柄。
 *
 * 坐标约定：左上角 (0,0)，x 向右、y 向下，范围 [0,width) x [0,height)。
 * 与 esp_lcd 一致，draw 的 x2/y2 是开区间。
 */

esp_err_t lcd_amoled_init(void);

int lcd_amoled_width(void);
int lcd_amoled_height(void);

/* 把一块 RGB565 像素（行优先，尺寸 (x2-x1)*(y2-y1)*2 字节）画到屏幕上。
   数据必须 DMA 可访问（内部 RAM 即可）。 */
esp_err_t lcd_amoled_draw(int x1, int y1, int x2, int y2, const void *rgb565);

/* 整屏填充一个 RGB565 颜色 */
esp_err_t lcd_amoled_fill(uint16_t rgb565);

/*
 * 面板电源开关（PIN_NUM_LCD_POWER，配在 board_config.h 里，本板是 GPIO38）。
 * 关掉用来防 AMOLED 烧屏：静态画面挂久了会把像素烧死。
 *
 * 重新打开时会把面板初始化重跑一遍（reset + 初始化序列 + 亮度）—— 断过电以后
 * 面板内部寄存器全丢了，只把电拉高是不会亮的。
 */
esp_err_t lcd_amoled_power(bool on);

#ifdef __cplusplus
}
#endif
