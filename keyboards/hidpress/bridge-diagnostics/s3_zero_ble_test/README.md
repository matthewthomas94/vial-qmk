# ESP32-S3-Zero BLE Test

This is a separate USB-powered test, not the USB-host bridge firmware.
It advertises as `Hidpress S3` and never sends keyboard or media reports.
Keep the battery, boost board, and keyboard disconnected. In particular,
do not connect external 5V while the S3-Zero USB-C port is connected to the Mac.

## Target And Backup (2026-10-13)

Confirmed by esptool before writing flash:

- ESP32-S3 revision 0.2, MAC `b8:f8:62:d7:3e:30`.
- 4 MB embedded flash, 2 MB embedded PSRAM.
- Secure Boot and Flash Encryption disabled.
- USB serial `B8:F8:62:D7:3E:30`, port `/dev/cu.usbmodem101` at test time.

Full original flash, including bootloader, partitions and settings:

`../backups/2026-10-13/s3-zero-b8f862d73e30/original-full-flash.bin`

- Size: 4,194,304 bytes from address `0x0`.
- SHA-256: `80bec78911233c364eef13f280641b065455e0959dabba3d1963e715d14fee3f`.
- Verified against all 4 MB of device flash with `esptool verify_flash`:
  `-- verify OK (digest matched)` before any firmware writes.
- Board remained in the bootloader between read and verification.
- Backup directory is private, image permissions are `0600`, and backups
  are Git-ignored. Do not publish the image; it can contain saved credentials.

## Build

Uses the already-installed ESP32 Arduino core 3.1.3 and ESP32 BLE Keyboard
0.3.0. The latter already contains the local Arduino 3.x `String` conversions
in `BleKeyboard.cpp`; no installed libraries were changed for this test.
The advertised battery level is a fixed test value, not a battery measurement.

FQBN:

```text
esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,FlashSize=4M,PSRAM=enabled,PartitionScheme=default,EraseFlash=none
```

Compile from the `hidpress` directory:

```sh
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" compile \
  --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,FlashSize=4M,PSRAM=enabled,PartitionScheme=default,EraseFlash=none" \
  --build-path "$PWD/bridge-diagnostics/build/s3-zero" \
  --build-property "runtime.tools.ctags.path=/tmp/hidpress-arduino-ctags-native" \
  --build-property "runtime.tools.ctags-5.8-arduino11.path=/tmp/hidpress-arduino-ctags-native" \
  "$PWD/bridge-diagnostics/s3_zero_ble_test"
```

The native ctags override reuses the earlier Feather diagnostic's temporary
build; see the parent README if it needs rebuilding. It is not needed when
Arduino's installed ctags runs successfully on the host.

Compiled successfully: 1,014,200 bytes program and 48,104 bytes global RAM.
The existing BLE library emits a deprecated ADC header warning.
Application image (1,014,304 bytes) SHA-256:
`f572bc604aedbe56e7df7797b366d87539240070758fdcc438656640690d5001`.

## Upload And Startup Result

Arduino CLI uploaded only to the same S3-Zero, with esptool confirming MAC
`b8:f8:62:d7:3e:30`. Bootloader, partition table, OTA selection and application
segments each reported `Hash of data verified`. No erase-all was requested.
Neither the Feather nor keyboard QMK firmware was touched.

Serial capture confirmed the application starts and detects 2,097,152 bytes
of PSRAM. Initial capture included:

```text
BLE started: Hidpress S3. No keystrokes will be sent.
BLE waiting for connection uptime=5012 ms free_heap=213288
BLE waiting for connection uptime=30012 ms free_heap=213288
BLE waiting for connection uptime=80012 ms free_heap=213288
BLE connected uptime=106172 ms free_heap=209672
```

No resets were observed during that interval. The user confirmed `Connected`
in macOS Bluetooth settings, and the board independently logged the BLE
connection. It stayed connected through uptime 156172 ms with stable free
heap of 207980 bytes. macOS System Information also recognized `Hidpress S3`
as a keyboard with `HID BLE` services (BLE address `B8:F8:62:D7:3E:31`).

One `BT_GATT: GATT_INSUF_AUTHENTICATION` warning appeared just after initial
connection. The link remained connected, but this sketch does not log the
authentication-complete event or send reports, so successful encrypted HID
delivery is not yet proven. Preserve this observation for the input test.
Keyboard input forwarding, reconnection after a power cycle, USB-host
operation and battery power have not been tested on this board. The serial
monitor was closed cleanly after the capture; the firmware remains running.

## Bluetooth Check

Open macOS System Settings > Bluetooth and connect to `Hidpress S3`.
Serial output at 115200 baud reports connection state and uptime every five
seconds. For CLI monitoring use `baudrate=115200,dtr=on,rts=off`.
This test does not forward inputs or test the keyboard's power requirements.
Forget this temporary pairing before testing firmware with a different HID
report descriptor.

## Restore Original Firmware

These restore instructions are not executed as part of the BLE test.
Close Serial Monitor, recheck the current port and confirm MAC
`b8:f8:62:d7:3e:30`. Never use this image on the Feather or another device.
From the `hidpress` directory, check the backup SHA-256 above, then:

```sh
/Users/matthewthomas/Library/Arduino15/packages/esp32/tools/esptool_py/4.9.dev3/esptool \
  --chip esp32s3 --port /dev/cu.usbmodem101 --baud 460800 --after hard_reset \
  write_flash --flash_mode keep --flash_freq keep --flash_size keep \
  0x0 bridge-diagnostics/backups/2026-10-13/s3-zero-b8f862d73e30/original-full-flash.bin
```

The installed tool directory is labelled `4.9.dev3`; the executable reports
esptool.py v4.8.1. No eFuses were changed.
