

#pragma once

#define MATRIX_ROWS 4
#define MATRIX_COLS 7

#define MATRIX_ROW_PINS {GP25, GP15, GP14, GP10}
#define MATRIX_COL_PINS {GP21, GP20, GP19, GP18, GP11, NO_PIN, GP7}

#define DIODE_DIRECTION COL2ROW

#define OLED_FONT_H "keyboards/hidpress/bipedalsouthpaw/glcdfont.c"
#define WS2812_PIO_USE_PIO1
#define WS2812_DI_PIN GP24
#define RGBLED_NUM 12
#define RGBLIGHT_LED_COUNT RGBLED_NUM 


#define CUSTOM_ENCODER_ENABLE

//This is now defined in the onfo.json
// #define NUMBER_OF_ENCODERS 3
// #define ENCODER_PAD_A_PINS { GP22, GP16, GP13 }
// #define ENCODER_PAD_B_PINS { GP23, GP17, GP12 }

#define ENCODER_RESOLUTION 2

#define TAP_CODE_DELAY 0


#define ANALOG_JOYSTICK_Y_AXIS_PIN GP27
#define ANALOG_JOYSTICK_X_AXIS_PIN GP26
// #define POINTING_DEVICE_INVERT_Y
#define POINTING_DEVICE_INVERT_X
#define ANALOG_JOYSTICK_AUTO_AXIS
// #define ANALOG_JOYSTICK_CLICK_PIN GP7
#define ANALOG_JOYSTICK_WEIGHTS {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100}
#define ANALOG_JOYSTICK_SPEED_REGULATOR 5
#define ANALOG_JOYSTICK_CUTOFF


// // Min 0, max 32
// #define JOYSTICK_BUTTON_COUNT 16
// // Min 0, max 6: X, Y, Z, Rx, Ry, Rz
// #define JOYSTICK_AXIS_COUNT 3
// // Min 8, max 16
// #define JOYSTICK_AXIS_RESOLUTION 10

#define JOYSTICK_BUTTON_COUNT 0
// Max 6: X, Y, Z, Rx, Ry, Rz
#define JOYSTICK_AXIS_COUNT 2

#undef I2C_DRIVER
#define I2C_DRIVER I2CD0
#undef I2C1_SCL_PIN
#define I2C1_SCL_PIN GP1
#undef I2C1_SDA_PIN
#define I2C1_SDA_PIN GP0

#define OLED_DISPLAY_128X32
#define OLED_TIMEOUT 60000 // Increase timeout to 1 minute
