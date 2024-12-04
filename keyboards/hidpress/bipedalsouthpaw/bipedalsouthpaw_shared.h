#pragma once

enum pointing_device_mode {
    MODE_MOUSE,
    MODE_SCROLLING,
    MODE_CUSTOM_KEYS, // New mode for WASD keys
    MODE_COUNT // Keeps track of the total number of modes
};

extern enum pointing_device_mode current_mode;


enum layers {
    _DEFAULT,
    _ONE,
    _TWO,
    _THREE
};