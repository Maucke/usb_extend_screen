#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

/*
 * LilyGO T-Display S3 AMOLED 板级配置：只保留屏幕相关的部分
 * （引脚 / 分辨率 / 镜像方向）。引脚取自 nexlink 工程的 main/config.h。
 */

#define LCD_HOST SPI2_HOST

#define DISPLAY_WIDTH   536
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

#define PIN_NUM_LCD_CS    (GPIO_NUM_6)
#define PIN_NUM_LCD_PCLK  (GPIO_NUM_47)
#define PIN_NUM_LCD_DATA0 (GPIO_NUM_18)
#define PIN_NUM_LCD_DATA1 (GPIO_NUM_7)
#define PIN_NUM_LCD_DATA2 (GPIO_NUM_48)
#define PIN_NUM_LCD_DATA3 (GPIO_NUM_5)
#define PIN_NUM_LCD_RST   (GPIO_NUM_17)
#define PIN_NUM_LCD_POWER (GPIO_NUM_38)

#endif // _BOARD_CONFIG_H_
