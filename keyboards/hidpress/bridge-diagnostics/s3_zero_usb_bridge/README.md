# S3-Zero Native USB to BLE Prototype

Target: ESP32-S3-Zero, 4 MB flash / 2 MB PSRAM, MAC `b8:f8:62:d7:3e:30`.
Uses the native USB controller, not the old Feather/MAX3421E. QMK is unchanged.

## Power And USB Roles

- Programming: S3 USB-C to Mac. Disconnect LiPo, disconnect the boost board's
  5V wire from S3, disconnect keyboard, and leave charger USB disconnected.
- Battery operation: Mac cable removed first. BQ25185 `5V` to S3 `5V`, `GND`
  to `GND`; battery plugs into BQ25185. No external power goes to S3 `3V3`.
- Keyboard: S3 USB-C to a USB-C male / USB-A female adapter, then a USB-A to
  USB-C data cable to the keyboard master. Join the split halves with TRRS
  while all power is off. Do not hot-plug TRRS.
- The S3 5V pad is connected to its USB VBUS. Never connect S3 USB to the
  Mac while the boost output is connected, even with boost disabled.
- BQ25185 USB-C is a charging input, not the keyboard host connection.
  Charging is deferred until the 1100 mAh battery's permitted charge current
  and the board's 1 A / 500 mA jumper setting are confirmed.
- Fixed Type-C sink resistors on this S3 board mean a normal C-to-C cable
  is not a supported host power connection. An adapter does not add power
  isolation, current limiting, or compliant Type-C source-role switching.

Power references:

- https://docs.waveshare.com/ESP32-S3-Zero
- https://files.waveshare.com/wiki/ESP32-S3-Zero/ESP32-S3-Zero-Sch.pdf
- https://learn.adafruit.com/adafruit-bq25185-usb-dc-solar-charger-with-5v-boost-board/pinouts

## Startup And LEDs

Every boot starts in maintenance mode. Three seconds after boot, USB host
arms automatically unless USB Serial/JTAG has detected the Mac, so the bridge
recovers by itself from resets such as the 5V dip when the charger is plugged
in or removed. BOOT held for one second still arms it immediately. Only press
BOOT after boot; holding it during reset enters the ROM downloader instead.
If auto-arm ever takes the port while on the Mac, upload via the ROM downloader.

The firmware also refuses the BOOT request while USB Serial/JTAG detects
an attached USB host. This software guard is not a replacement for the
physical power isolation above, nor protection against later misconnection.
Once armed, USB serial disappears because the USB PHY is now used for host.

| LED | Meaning |
| --- | --- |
| Blue | Maintenance mode, host disarmed |
| Amber | Host armed, waiting for both supported USB interfaces |
| Cyan | USB interfaces opened, BLE not fully authenticated/subscribed |
| Green | USB interfaces opened and all three BLE reports subscribed |
| Flashing magenta | Empty slot selected, pairing a new host (60 s) |
| Flashing white | Saved slot selected, waiting for that slot's host |
| Red | USB host stack, report read or event queue error; forwarding stopped |

Green indicates readiness, not proof of end-to-end input delivery.
Unplugging the keyboard returns the LED to amber and replugging it to
cyan/green without a restart; a USB open or transfer error does the same.
Red needs a restart: disconnect power and inspect wiring before trying again.

The S3-Zero LED uses RGB byte order, as documented in the
[Waveshare FAQ](https://docs.waveshare.com/ESP32-S3-Zero/FAQ). The initial
uploaded build used GRB: its waiting-for-BLE cyan appeared purple, and its
red/green indications were swapped. The table above applies after uploading
the RGB correction; changing the source does not update the running board.

BLE name: `Hidpress S3 Bridge`. Forget the previous `Hidpress S3` pairing
before pairing this firmware: the same radio address now has a different
HID descriptor. Pair through macOS Bluetooth settings. No automatic test
keystrokes are generated. The battery percentage is a fixed placeholder,
not a measurement. PnP uses an unassigned prototype VID, not a production ID.

## Host Slots

The bridge remembers three hosts, one per slot, and holds one BLE
connection at a time. Pick a slot on the keyboard by holding the encoder's
push and turning it: `Enc Mod` on the Ambi, encoder 1's `Thmbstk` push on
the Southpaw (tapping it still changes thumbstick mode). Vial also offers
`BT Prev` / `BT Next` custom keycodes for any key or encoder direction.
Each step moves the slot 1 > 2 > 3 (wrapping), shows `BT` and the slot on the
OLED for a second, and sends reserved consumer usage 0x0010 + slot
(0x0011-0x0013). The bridge consumes it and does not forward it; a
computer on direct USB ignores it.

Each slot advertises from its own local BLE address: slot 1 keeps the
board's public address, slots 2 and 3 use static random addresses derived
from it. A host only knows the address it paired with, so it only ever
sees and reconnects to its own slot; other hosts never connect, so there
is no connect-then-kick. Every host appears as `Hidpress S3 Bridge`.

Why per-slot addresses (as Logitech Easy-Switch does): when pairing from a
static random address, Bluedroid sends that address as the bridge's
identity (`smp_build_id_addr_cmd`, ESP-IDF v5.3.2), so each host stores a
different device. An advertising accept list would also need to match
macOS RPAs, which needs controller address resolution, and an open
pairing slot cannot use one at all. Directed advertising likewise has to
target a host's changing RPA and cannot pair a new host. Neither stops a
host seeing every slot as the same device.

- Slot with a host: the current host is dropped and the bridge advertises
  only that slot's address, with no timeout. The LED flashes white until
  that host reconnects by itself (no clicking Connect).
- Empty slot: pairing mode. The LED flashes magenta for 60 seconds; pair
  the new computer from its Bluetooth settings and it takes the slot. The
  slot keeps accepting a new host after the flashing stops.
- Hold BOOT 5 s (after boot, never during reset): forget the selected
  slot's host and pair a new one into it. The slot gets a new address, so
  the forgotten host no longer sees it; remove its old `Hidpress S3 Bridge`
  entry on that host.

After boot no slot is chosen: the bridge starts on slot 1 and, while
nothing connects, moves every 3 seconds through the saved slots, so any
saved host can reconnect. Turning the dial locks onto a slot as above. The
keyboard's slot counter starts at 01 on power-up and is not told which
host the bridge reconnected to, so the OLED number may not match until the
dial is turned.

Edge cases:

- A host connecting in the instant the roaming address changes is refused
  once and retries on its own address.
- A host keeps one slot. A saved host showing up on another slot's
  address (connecting as it changes, or paired there on purpose as a
  second `Hidpress S3 Bridge` entry) is refused after encryption and keeps
  its own slot; to move a host, re-pair its old slot first. A new host
  pairing on an occupied slot is refused and its bond removed. Bluedroid itself deletes a bond
  when encryption fails, so if a host ever says the bridge is unavailable,
  forget it on that host and pair again.
- macOS connects with resolvable private addresses (RPAs). Bluedroid stores
  each bond under the address used at pairing and reports that same
  address on every reconnect, so slots stay matched across reconnects and
  reboots. Slots also store the host's identity address, so a host that
  pairs again keeps its slot.

Upgrading from the previous slot firmware: the slot 1 pairing (normally
the Mac) survives. Hosts saved in slots 2 and 3 are cleared on first boot
because those slots now have new addresses: on each, forget
`Hidpress S3 Bridge`, select the slot on the keyboard and pair again.

## Supported Inputs

Only HIDPRESS VID `d812`, PID `0020` (Southpaw) or `0021` (Ambi) are opened:

- Interface 0: force USB boot keyboard protocol; forward eight-byte reports
  as BLE report 1 (six simultaneous non-modifier keys plus modifiers).
- Interface 2, USB report 2: forward the five-byte mouse payload as BLE
  report 2, including buttons, movement, vertical wheel and horizontal pan.
- Interface 2, USB report 4: forward the two-byte consumer usage as BLE
  report 4, including volume up/down, mute and release.

This matches the captured Southpaw descriptors and the current Ambi QMK
source. Different interface ordering or extended 16-bit mouse reports need
an explicit update; this is not a universal HID parser. Vial raw HID,
gamepad reports, system-control keys and NKRO reports are not forwarded.
Keyboard LED output is accepted over BLE but not sent back to QMK yet.

USB callbacks copy events to a queue; blocking class requests and BLE
notifications run outside the USB callback. USB disconnect releases all
inputs; a fresh authenticated BLE subscription starts with released inputs.
Queue overflow stops forwarding and attempts release instead of silently
dropping a key-up. A USB open or transfer error closes the interface and
waits for the keyboard to be replugged; no reset is needed.

## Build And Test

ESP32 Arduino core 3.1.3, bundled BLE library, Adafruit NeoPixel 1.15.4 and
the locally packaged, unmodified Espressif `usb_host_hid` 1.0.3 driver.
See `../libraries/UsbHostHid/UPSTREAM.md` for its source pin and license.

From the `hidpress` directory:

```sh
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" compile \
  --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,FlashSize=4M,PSRAM=enabled,PartitionScheme=default,EraseFlash=none" \
  --build-path "$PWD/bridge-diagnostics/build/s3-zero-usb-bridge" \
  --build-property "runtime.tools.ctags.path=/tmp/hidpress-arduino-ctags-native" \
  --build-property "runtime.tools.ctags-5.8-arduino11.path=/tmp/hidpress-arduino-ctags-native" \
  --libraries "$PWD/bridge-diagnostics/libraries" \
  "$PWD/bridge-diagnostics/s3_zero_usb_bridge"

env -u SDKROOT clang++ \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.2.sdk \
  -std=c++17 -Wall -Wextra -Werror \
  bridge-diagnostics/tests/report_router_test.cpp \
  -o /tmp/hidpress-report-router-test-plain
/tmp/hidpress-report-router-test-plain

env -u SDKROOT clang++ \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.2.sdk \
  -std=c++17 -Wall -Wextra -Werror \
  bridge-diagnostics/tests/host_slots_test.cpp \
  -o /tmp/hidpress-host-slots-test
/tmp/hidpress-host-slots-test
```

The local SDK override avoids the host's clang / macOS 27 SDK mismatch;
it is not a firmware dependency. The ctags override reuses the earlier
diagnostic's native ctags build (see parent README).

## Manual Host Slot Test

Not yet run on hardware. With the Mac in slot 1 and a second device:

1. Upgrade: after upload, the Mac reconnects on slot 1 without pairing.
2. Empty-slot pairing: select slot 2; the LED flashes magenta and the Mac
   drops. Pair the second device from its Bluetooth settings; LED green.
   The Mac does not connect while slot 2 is selected.
3. Switching: turn 1 > 2 > 1 several times. Each time the LED flashes
   white, then only that slot's host reconnects by itself without
   clicking Connect, and input goes only to it.
4. Re-pair: hold BOOT 5 s on slot 2; magenta. Forget the bridge on the
   second device and pair it again into slot 2.
5. Keyboard unplug/replug while connected: amber, then cyan/green with
   input working, no restart, no red.
6. Power-cycle the bridge: any saved host reconnects without the dial.

## Backup Before Installation

On 2026-10-13 the S3 identity and 4 MB flash were rechecked. A new full-flash
backup of the BLE-only test was read and verified against the device:

`../backups/2026-10-13/s3-zero-b8f862d73e30/before-native-host-full-flash.bin`

- Size: 4,194,304 bytes.
- SHA-256: `eb769f519d780c6e0c8840000b9e8543a9cc3c3a6ba424c2d1d9fa8cd6113974`.
- `esptool verify_flash`: `verify OK (digest matched)`.
- Private directory, mode 0600 file, Git-ignored. Do not publish flash
  images, which can contain pairing keys and other private state.
- Original factory/full-flash backup is also retained in the same folder.

For recovery, disconnect external 5V and the battery before using Mac USB.
Hold BOOT, press and release RESET, then release BOOT to enter the ROM
downloader. Reidentify MAC and port before any restore. The full backup
belongs only to this S3, not the Feather or either keyboard half.

## Validation Boundary

Compiled and uploaded on 2026-10-13 to the reidentified S3-Zero only:

- Program: 1,064,788 bytes (81%); static RAM: 48,176 bytes (14%).
- Application image: 1,064,896 bytes, SHA-256
  `359b254f1162cdf94e174a74e84a627ec63f8f7a2d87d05c4de7670bd8304728`.
- Upload identified MAC `b8:f8:62:d7:3e:30`; each written segment reported
  `Hash of data verified`. No erase-all, eFuse or QMK changes.
- Serial repeatedly reported:
  `Host DISARMED; BLE connected=1 authenticated=1; Mac USB=1; heap=198628`.
- Thus the new application boots, detects the Mac and authenticates a BLE
  connection. USB host was deliberately not armed on the Mac. Cached host
  HID descriptors still need a fresh pairing before the input test.
- The serial monitor was closed after verification.

The user subsequently confirmed key input from both halves over TRRS on the
battery-powered native bridge. Trackpad and rotary encoder input did not
respond. A fresh BLE pairing is the next diagnostic test; the cause of those
missing inputs is not yet established. Charging remains unvalidated.
The new report mapper passed local tests for key presses/releases, signed
mouse/scroll bytes, volume/mute/release and rejection of malformed or
unsupported reports. Full mouse/media delivery, sustained battery operation
and unplug/reconnect behavior still require physical tests.
An AddressSanitizer test invocation stalled in the local environment and was
stopped; the plain host test completed successfully. Sanitizer validation is
not claimed.
