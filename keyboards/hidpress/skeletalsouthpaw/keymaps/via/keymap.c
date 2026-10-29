
#include QMK_KEYBOARD_H

// Define the keycode, `QK_USER` avoids collisions with existing keycodes
enum keycodes {
  KC_CYCLE_LAYERS = QK_USER,
};

// 1st layer on the cycle
#define LAYER_CYCLE_START 0
// Last layer on the cycle
#define LAYER_CYCLE_END   3

// Add the behaviour of this new keycode
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
  switch (keycode) {
    case KC_CYCLE_LAYERS:
      // Our logic will happen on presses, nothing is done on releases
      if (!record->event.pressed) { 
        // We've already handled the keycode (doing nothing), let QMK know so no further code is run unnecessarily
        return false;
      }

      uint8_t current_layer = get_highest_layer(layer_state);

      // Check if we are within the range, if not quit
      if (current_layer > LAYER_CYCLE_END || current_layer < LAYER_CYCLE_START) {
        return false;
      }

      uint8_t next_layer = current_layer + 1;
      if (next_layer > LAYER_CYCLE_END) {
          next_layer = LAYER_CYCLE_START;
      }
      layer_move(next_layer);
      return false;

    // Process other keycodes normally
    default:
      return true;
  }
}

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
	[0] = LAYOUT(
        KC_MUTE, TO(0),   KC_UP,   TO(1),
        RGB_TOG, KC_LEFT, KC_ESC,  KC_RGHT,
        RGB_TOG, TO(2),   KC_DOWN, TO(3),
                 KC_LGUI,          KC_LALT, KC_CYCLE_LAYERS 
    ),
	[1] = LAYOUT(
        KC_MUTE, KC_TRNS, KC_TRNS,  KC_TRNS,
        KC_MPLY, KC_TRNS, KC_TRNS,  KC_TRNS,
        KC_MPLY, KC_TRNS, KC_TRNS,  KC_TRNS,
                 KC_TRNS,           KC_TRNS, KC_TRNS 
    ),
	[2] = LAYOUT(
        KC_LSFT,  KC_TRNS, KC_TRNS,  KC_TRNS,
        G(KC_0),  KC_TRNS, KC_TRNS,  KC_TRNS,
        C(KC_UP), KC_TRNS, KC_TRNS,  KC_TRNS,
                  KC_TRNS,           KC_TRNS, KC_TRNS 
    ),
	[3] = LAYOUT(
        KC_LSFT, KC_TRNS, KC_TRNS,  KC_TRNS,
        KC_LSFT, KC_TRNS, KC_TRNS,  KC_TRNS,
        KC_LSFT, KC_TRNS, KC_TRNS,  KC_TRNS,
                 KC_TRNS,           KC_TRNS, KC_TRNS  
    ),
};


#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [0] =  { ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(RGB_VAD, RGB_VAI),  ENCODER_CCW_CW(RGB_HUD, RGB_HUI)  },
    [1] =  { ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(KC_MPRV, KC_MNXT),  ENCODER_CCW_CW(KC_MRWD, KC_MFFD)  },
    [2] =  { ENCODER_CCW_CW(KC_WH_U, KC_WH_D), ENCODER_CCW_CW(G(KC_PMNS), G(KC_PPLS)),  ENCODER_CCW_CW(C(S(KC_TAB)), C(KC_TAB))  },
    [3] =  { ENCODER_CCW_CW(KC_UP, KC_DOWN), ENCODER_CCW_CW(G(KC_LEFT), G(KC_RGHT)),  ENCODER_CCW_CW(A(KC_LEFT), A(KC_RGHT))  },
};

#endif