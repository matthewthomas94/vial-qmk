# Feather USB-host diagnostic

## Backup (2026-10-10)

Device confirmed using esptool 4.8.1: ESP32-S3 revision 0.2, MAC
`d0:cf:13:09:8c:54`, 4 MB flash, 2 MB PSRAM.

Full original flash (including bootloader, partitions and NVS/pairing data):
`backups/2026-10-10/feather-original-full-flash.bin`

- Size: 4,194,304 bytes, read from address `0x00000000`.
- SHA-256: `f653c5884bdfd20636e51048a31e5d1c3b7175adb0353aa2b88399c57b71ca07`.
- Verified against the device using `esptool verify_flash`; digest matched
  before any flash writes. The device stayed in its bootloader between read
  and verification.
- Owner-only file permissions. Backups, builds and logs are Git-ignored.
- The existing Arduino source was also copied to
  `backups/2026-10-10/hidpress-bridge-source.ino`. Its referenced
  `usbh_helper.h` was absent, so this source copy alone is not a complete
  rebuildable backup. Use the full-flash image for restoration.

## Diagnostic

`usb_host_diagnostic/usb_host_diagnostic.ino` uses the installed ESP32 Arduino
core 3.1.3 and Adafruit TinyUSB Library 3.7.6. It does not run BLE or forward
keystrokes to the Mac. Serial output is 115200 baud.

It logs host initialization, MAX3421E registers, HID mount/unmount and input
reports. If host initialization fails, it additionally retries register
communication at 1 MHz. The normal driver uses 26 MHz. TFT chip select is
held high because the onboard display shares SPI.

Pins: SCK 36, MOSI 35, MISO 37, CS 10, IRQ 9, TFT CS 7. Wing CS/IRQ agree with
[Adafruit's pinout](https://learn.adafruit.com/adafruit-usb-host-featherwing-with-max3421e/pinouts).
The Wing's separate pad labelled `5V` is a boost-enable signal, not a power
injection point. The user confirmed the external Pololu was disconnected.

Board configuration:

```text
esp32:esp32:adafruit_feather_esp32s3_tft:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,PSRAM=enabled,PartitionScheme=tinyuf2,EraseFlash=none
```

The installed Intel-only ctags executable failed on this Mac. For this build
only, a native copy of the official [Arduino ctags 5.8-arduino11 source](https://github.com/arduino/ctags/tree/5.8-arduino11)
was built in `/tmp/hidpress-arduino-ctags-native` (commit
`abc8fca7499f44c725122881cd380a88c37abe0e`). Neither the IDE nor installed
libraries were modified. The CLI build overrides both
`runtime.tools.ctags.path` and `runtime.tools.ctags-5.8-arduino11.path` to that
temporary directory. Building ctags required Xcode's own macOS SDK and
`CFLAGS='-g -O2 -include dirent.h'` for compatibility with modern headers.

Version 2 compiled successfully: 353,980 bytes program, 25,612 bytes global
variables. Its 354,096-byte application image has SHA-256:
`c74c9b54b91785677ee56f7591e34bf0faa6bb1f60065396eb2dc2c31f243f86`.

The original partition table and OTA selection matched the build's TinyUF2
layout and `boot_app0.bin` byte-for-byte (digest comparison). Only the
application at `0x10000` was flashed, with esptool confirming its hash.
Bootloader, partition table, NVS and both keyboards' firmware were untouched.

## Initial Result

See `logs/2026-10-10-initial-diagnostic.txt` and
`logs/2026-10-10-slow-spi-diagnostic.txt`:

```text
STATUS uptime=23003 init=FAILED HID=0 reports=0 IRQ=0
MAX3421E REV=00 USBIRQ=00 USBCTL=00 PINCTL=00 HIRQ=00 HIEN=00 MODE=00 HCTL=00 HRSL=00
SLOW SPI 1MHz REV=00 PINCTL=00
```

The host controller is not returning a valid revision on the configured SPI
connection, even at the lower speed. This failure occurs before keyboard HID
enumeration and without BLE. It does not establish whether the cause is
power/reset, a header/solder connection, the SPI path, or a faulty controller.
The installed TinyUSB driver accepts revision values 0x01, 0x12 and 0x13.

## Cold, Unloaded Result

The user confirmed removing the keyboard from USB-A, disconnecting Feather
USB-C and LiPo for 10 seconds, then reconnecting only USB-C to the Mac.
The keyboard, battery and Pololu remained disconnected.

See `logs/2026-10-10-cold-unloaded-diagnostic.txt`:

```text
STATUS uptime=145006 init=OK HID=0 reports=0 IRQ=1
MAX3421E REV=13 USBIRQ=01 USBCTL=00 PINCTL=10 HIRQ=09 HIEN=E4 MODE=C1 HCTL=02 HRSL=03
```

Eight consecutive status reports showed successful initialization and a
valid controller revision. The Feather-to-Wing SPI connection works in this
cold, unloaded condition. This does not yet distinguish a reset-state issue
from a load/power issue or an intermittent connection, since both the power
state and connected load changed. No keyboard was attached, so HID=0 is
expected; keyboard enumeration and BLE operation are not verified by this
test.

## Southpaw Reconnected Result

The user confirmed connecting the Southpaw to the Wing's USB-A port while
leaving Feather USB-C connected to the Mac, with battery and Pololu
disconnected. No firmware upload or power reset occurred between this test
and the cold, unloaded test.

See `logs/2026-10-10-southpaw-reconnected-diagnostic.txt`:

```text
STATUS uptime=275006 init=OK HID=0 reports=0 IRQ=1
MAX3421E REV=00 USBIRQ=00 USBCTL=00 PINCTL=00 HIRQ=00 HIEN=00 MODE=00 HCTL=00 HRSL=00
```

Eight consecutive status reports returned all-zero controller registers,
with no mounted HID interfaces or input reports. The Feather continued
running. `init=OK` is the cached result of the earlier startup call, not a
live controller-health check. The valid unloaded revision was no longer
readable after the keyboard was connected; this does not by itself identify
a power, reset, connection or controller fault.

## Southpaw Removed Without Reset

The user confirmed unplugging only the Southpaw from the Wing, leaving
Feather USB-C connected and battery and Pololu disconnected. No reset was
performed before this capture.

See `logs/2026-10-10-southpaw-removed-no-reset-diagnostic.txt`:

```text
STATUS uptime=359006 init=OK HID=0 reports=0 IRQ=1
MAX3421E REV=00 USBIRQ=00 USBCTL=00 PINCTL=00 HIRQ=00 HIEN=00 MODE=00 HCTL=00 HRSL=00
```

Eight consecutive status reports remained all zero. Removing the keyboard
did not restore communication during this observation window.

## Unloaded Software Restart

After closing the serial capture, esptool `--no-stub read_mac` confirmed the
same Feather identity (`d0:cf:13:09:8c:54`) and restarted the ESP32 using its
RTC watchdog. No flash was written, and no cable or power source changed.
This is not a full power cycle or a physical reset of the Wing.

See `logs/2026-10-10-unloaded-software-restart-diagnostic.txt`:

```text
STATUS uptime=15006 init=OK HID=0 reports=0 IRQ=1
MAX3421E REV=FF USBIRQ=FF USBCTL=FF PINCTL=FF HIRQ=FF HIEN=FF MODE=FF HCTL=FF HRSL=FF
```

Seven consecutive status reports returned all ones, not valid controller
register values. Initialization had returned success, but stable subsequent
communication was not restored. The read pattern changed from 00 to FF;
neither pattern alone proves hardware damage. The underlying power, reset,
SPI/connection or software cause remains unconfirmed.

Next investigation: establish whether a multimeter is available and inspect
the Wing's power, reset and stacking-header connections. Measure actual
power rails, not the separate `5V` boost-enable pad. Disconnect all power
before separating boards or inspecting/reworking solder joints.

## Restore Original Firmware

These commands are documented, not executed. Close Serial Monitor first.
Confirm the current serial port and that it is this Feather, not another
ESP32. Keep the Pololu disconnected. Run from this directory after deciding
to end the diagnostic:

```sh
shasum -a 256 backups/2026-10-10/feather-original-full-flash.bin
```

Check against the backup hash above. Then restore the exact full image:

```sh
/Users/matthewthomas/Library/Arduino15/packages/esp32/tools/esptool_py/4.9.dev3/esptool \
  --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 460800 --after hard_reset \
  write_flash --flash_mode keep --flash_freq keep --flash_size keep \
  0x0 backups/2026-10-10/feather-original-full-flash.bin
```

This restores all saved flash contents, including settings and pairing data.
Do not use this device-specific backup on a different board.
