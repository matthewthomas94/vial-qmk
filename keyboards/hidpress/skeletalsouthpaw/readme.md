# Skeletal Southpaw

HIDPRESS Skeletal Southpaw is the fully 3D-printed manufacturing variant of
Bipedal Southpaw. Its electronics and firmware behaviour are unchanged. The
canonical firmware target is `hidpress/skeletalsouthpaw`.

This target is an independent copy of `hidpress/bipedalsouthpaw`, including its
RP2040 pin assignments, keymaps, three encoders, thumbstick modes, actuation
settings, OLED graphics and screensaver, RGB lighting, EEPROM storage, and
Bluetooth host-slot controls for the external S3 bridge. The original Southpaw
and Bipedal 65 targets remain available.

## Build

From the root of this Vial QMK checkout with the existing QMK build environment:

```sh
make hidpress/skeletalsouthpaw:vial
```

The firmware output is `hidpress_skeletalsouthpaw_vial.uf2`. This command builds
the firmware; it does not flash a device.

The legacy `via` keymap is preserved for parity with the original project. It
currently fails to compile in shared `quantum/dynamic_keymap.c` because
`VIAL_MACRO_EXT_*` definitions and `vial_keycode_*` declarations are missing.
The same failure occurs for `hidpress/bipedalsouthpaw:via`. Use `vial` for the
Southpaw's current custom controls and Vial configuration.

## Device identity

The USB product name and Vial display name are **Skeletal Southpaw**. USB VID
`0xD812`, PID `0x0020`, device version `0.0.1`, and Vial UID
`B2 02 AA DE 74 A2 47 D8` are deliberately retained from Bipedal Southpaw.
Manufacturing is the only hardware difference, so the matrix, custom-keycode
ordering, configuration format, and device identity remain compatible. The
two targets share these identifiers; this duplicate does not introduce a new
USB or Vial device family.

## Files

- Keyboard/OLED implementation: `skeletalsouthpaw.c`
- Shared declarations: `skeletalsouthpaw_shared.h`
- Hardware settings and OLED font path: `config.h`
- USB product name and hardware metadata: `info.json`
- Vial keymap, definition, and UID: `keymaps/vial/`
- OLED image conversion: `tools/convert_oled_image.py`
- OLED workflow: `.agent/workflows/oled-ui.md`
