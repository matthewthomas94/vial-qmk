
#include QMK_KEYBOARD_H
#include "analog.h"
#include "joystick.h"
#include "bipedalsouthpaw_shared.h"

enum custom_keycodes {
    TMB_MODE = QK_KB_0, // Start custom keycodes with SAFE_RANGE to avoid conflicts
    CL_FWD,             // Automatically assigned to the next value after TMB_MODE
    CL_BWD,             // Automatically assigned to the next value after KC_CYCLE_LAYERS_FWD
    KC_JOYSTICK_BUTTON  // Custom keycode for joystick button
};

enum pointing_device_mode current_mode = MODE_MOUSE; // Adjusted line

// Read the state of the joystick button
bool is_joystick_button_pressed(void) {
    return readPin(GP7) == 0;
}

// Modify these values to adjust the scrolling speed
#define SCROLL_DIVISOR_H 24.0
#define SCROLL_DIVISOR_V 24.0

// Variables to store accumulated scroll values for scrolling mode
float scroll_accumulated_h = 0;
float scroll_accumulated_v = 0;

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    switch (current_mode) {
        case MODE_MOUSE:
            // Mouse mode doesn't modify the mouse report
            break;

        case MODE_SCROLLING:
            // Implement scrolling logic here based on mouse_report.x and mouse_report.y
            scroll_accumulated_h -= (float)mouse_report.x / SCROLL_DIVISOR_H;
            scroll_accumulated_v += (float)mouse_report.y / SCROLL_DIVISOR_V;

            mouse_report.h = (int8_t)scroll_accumulated_h;
            mouse_report.v = (int8_t)scroll_accumulated_v;

            scroll_accumulated_h -= (int8_t)scroll_accumulated_h;
            scroll_accumulated_v -= (int8_t)scroll_accumulated_v;

            mouse_report.x = 0;
            mouse_report.y = 0;
            break;

        case MODE_CUSTOM_KEYS:
            // In WASD mode, ensure no mouse movement is reported
            mouse_report.x = 0;
            mouse_report.y = 0;
            break;

        case MODE_COUNT:
        default:
            // No action needed for default and MODE_COUNT case
            break;
    }

    // The return statement should only be at the end of the function
    return mouse_report;
}

// 1st layer on the cycle
#define LAYER_CYCLE_START 0
// Last layer on the cycle
#define LAYER_CYCLE_END 3

// Declare a static variable to keep track of the joystick button state
static bool joystick_button_pressed = false;

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case TMB_MODE:
            if (record->event.pressed) {
                current_mode = (current_mode + 1) % MODE_COUNT;
            }
            return false;

        case KC_JOYSTICK_BUTTON:
            if (record->event.pressed) {
                register_code(KC_TRNS); // Register mouse button click
            } else {
                unregister_code(KC_TRNS); // Unregister mouse button click
            }
            return false;

        case CL_FWD:
            if (record->event.pressed) {
                uint8_t current_layer = get_highest_layer(layer_state);
                if (current_layer >= LAYER_CYCLE_START && current_layer <= LAYER_CYCLE_END) {
                    uint8_t next_layer = current_layer + 1;
                    if (next_layer > LAYER_CYCLE_END) {
                        next_layer = LAYER_CYCLE_START;
                    }
                    layer_move(next_layer);
                }
            }
            return false;

        case CL_BWD:
            if (record->event.pressed) {
                uint8_t current_layer = get_highest_layer(layer_state);
                if (current_layer >= LAYER_CYCLE_START && current_layer <= LAYER_CYCLE_END) {
                    int8_t prev_layer = current_layer - 1;
                    if (prev_layer < LAYER_CYCLE_START) {
                        prev_layer = LAYER_CYCLE_END;
                    }
                    layer_move(prev_layer);
                }
            }
            return false;
    }
    return true; // Allow QMK to process other keycodes normally
}

void handle_joystick_button(void) {
    if (is_joystick_button_pressed()) {
        uint16_t joystick_keycode = keymap_key_to_keycode(layer_state, (keypos_t){.row = 3, .col = 6});
        if (joystick_keycode != KC_NO && joystick_keycode != KC_JOYSTICK_BUTTON) {
            if (!joystick_button_pressed) {
                joystick_button_pressed = true;
                register_code(joystick_keycode);
            }
        } else {
            if (!joystick_button_pressed) {
                joystick_button_pressed = true;
                register_code(KC_TRNS);
            }
        }
    } else {
        if (joystick_button_pressed) {
            joystick_button_pressed = false;
            unregister_code(KC_TRNS);
            // Unregister remapped keycode
            uint16_t joystick_keycode = keymap_key_to_keycode(layer_state, (keypos_t){.row = 3, .col = 6});
            if (joystick_keycode != KC_NO && joystick_keycode != KC_JOYSTICK_BUTTON) {
                unregister_code(joystick_keycode);
            }
        }
    }
}

static int actuation = 256; // actuation point for customkeys (0-511)
bool       customkeys[4];

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(TMB_MODE, KC_KP_7, KC_KP_8, KC_KP_9, KC_MUTE, KC_KP_4, KC_KP_5, KC_KP_6, RGB_TOG, KC_KP_1, KC_KP_2, KC_KP_3, KC_KP_0, KC_KP_DOT, KC_ENTER),
    [1] = LAYOUT(TMB_MODE, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS),
    [2] = LAYOUT(TMB_MODE, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS),
    [3] = LAYOUT(TMB_MODE, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS),
};

void matrix_scan_user(void) {
    handle_joystick_button();

    // Execute the joystick-based actions only in MODE_CUSTOM_KEYS
    if (current_mode != MODE_CUSTOM_KEYS) {
        return; // Exit the function early if we're not in MODE_CUSTOM_KEYS
    }

    // Joystick-based custom keys logic continues as before
    // Up
    if (!customkeys[0] && analogReadPin(GP27) - 512 > actuation) {
        customkeys[0]    = true;
        uint16_t keycode = dynamic_keymap_get_keycode(biton32(layer_state), 1, 5);
        register_code16(keycode);
    } else if (customkeys[0] && analogReadPin(GP27) - 512 < actuation) {
        customkeys[0]    = false;
        uint16_t keycode = dynamic_keymap_get_keycode(biton32(layer_state), 1, 5);
        unregister_code16(keycode);
    }
    // Down
    if (!customkeys[1] && analogReadPin(GP27) - 512 < -actuation) {
        customkeys[1]    = true;
        uint16_t keycode = dynamic_keymap_get_keycode(biton32(layer_state), 0, 5);
        register_code16(keycode);
    } else if (customkeys[1] && analogReadPin(GP27) - 512 > -actuation) {
        customkeys[1]    = false;
        uint16_t keycode = dynamic_keymap_get_keycode(biton32(layer_state), 0, 5);
        unregister_code16(keycode);
    }
    // Left
    if (!customkeys[2] && analogReadPin(GP26) - 512 > actuation) {
        customkeys[2]    = true;
        uint16_t keycode = dynamic_keymap_get_keycode(biton32(layer_state), 3, 5);
        register_code16(keycode);
    } else if (customkeys[2] && analogReadPin(GP26) - 512 < actuation) {
        customkeys[2]    = false;
        uint16_t keycode = dynamic_keymap_get_keycode(biton32(layer_state), 3, 5);
        unregister_code16(keycode);
    }
    // Right
    if (!customkeys[3] && analogReadPin(GP26) - 512 < -actuation) {
        customkeys[3]    = true;
        uint16_t keycode = dynamic_keymap_get_keycode(biton32(layer_state), 2, 5);
        register_code16(keycode);
    } else if (customkeys[3] && analogReadPin(GP26) - 512 > -actuation) {
        customkeys[3]    = false;
        uint16_t keycode = dynamic_keymap_get_keycode(biton32(layer_state), 2, 5);
        unregister_code16(keycode);
    }
}

// Joystick config
joystick_config_t joystick_axes[JOYSTICK_AXIS_COUNT] = {[0] = JOYSTICK_AXIS_VIRTUAL, [1] = JOYSTICK_AXIS_VIRTUAL};

#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [0] = {ENCODER_CCW_CW(CL_BWD, CL_FWD), ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(RGB_HUD, RGB_HUI)},
    [1] = {ENCODER_CCW_CW(CL_BWD, CL_FWD), ENCODER_CCW_CW(KC_TRNS, KC_TRNS), ENCODER_CCW_CW(KC_TRNS, KC_TRNS)},
    [2] = {ENCODER_CCW_CW(CL_BWD, CL_FWD), ENCODER_CCW_CW(G(KC_TRNS), G(KC_TRNS)), ENCODER_CCW_CW(KC_TRNS, KC_TRNS)},
    [3] = {ENCODER_CCW_CW(CL_BWD, CL_FWD), ENCODER_CCW_CW(G(KC_TRNS), G(KC_TRNS)), ENCODER_CCW_CW(KC_TRNS, KC_TRNS)},
};

#endif