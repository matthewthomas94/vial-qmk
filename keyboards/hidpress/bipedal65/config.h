/* Copyright 2022 DeskDaily
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#define OLED_FONT_H "keyboards/hidpress/bipedal65/glcdfont.c"
#define WS2812_PIO_USE_PIO1
#define WS2812_DI_PIN GP29
#define RGBLED_NUM 65
#define RGB_MATRIX_LED_COUNT RGBLED_NUM

#define ENCODERS 3
#define ENCODERS_PAD_A \
    { GP12, GP16, GP7 }
#define ENCODERS_PAD_B \
    { GP13, GP17, GP6 }
#define TAP_CODE_DELAY 10

#undef I2C_DRIVER
#define I2C_DRIVER I2CD0
#undef I2C1_SCL_PIN
#define I2C1_SCL_PIN GP1
#undef I2C1_SDA_PIN
#define I2C1_SDA_PIN GP0

#define OLED_DISPLAY_128X32
#define OLED_TIMEOUT 60000 // Increase timeout to 1 minute