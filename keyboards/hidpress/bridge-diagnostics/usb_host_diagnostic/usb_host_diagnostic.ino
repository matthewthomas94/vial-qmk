#include <Adafruit_TinyUSB.h>
#include <SPI.h>

constexpr uint8_t HOST_CS = 10;
constexpr uint8_t HOST_IRQ = 9;
Adafruit_USBH_Host USBHost(&SPI, HOST_CS, HOST_IRQ);

bool hostReady = false;
uint8_t mountedInterfaces = 0;
uint32_t reportCount = 0;

uint8_t readSlowRegister(uint8_t reg) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(HOST_CS, LOW);
  SPI.transfer(reg);
  uint8_t value = SPI.transfer(0);
  digitalWrite(HOST_CS, HIGH);
  SPI.endTransaction();
  return value;
}

void printStatus() {
  Serial.printf("STATUS uptime=%lu init=%s HID=%u reports=%lu IRQ=%d\n",
                (unsigned long)millis(), hostReady ? "OK" : "FAILED",
                mountedInterfaces, (unsigned long)reportCount,
                digitalRead(HOST_IRQ));
  Serial.printf("MAX3421E REV=%02X USBIRQ=%02X USBCTL=%02X PINCTL=%02X "
                "HIRQ=%02X HIEN=%02X MODE=%02X HCTL=%02X HRSL=%02X\n",
                USBHost.max3421_readRegister(0x90, false),
                USBHost.max3421_readRegister(0x68, false),
                USBHost.max3421_readRegister(0x78, false),
                USBHost.max3421_readRegister(0x88, false),
                USBHost.max3421_readRegister(0xC8, false),
                USBHost.max3421_readRegister(0xD0, false),
                USBHost.max3421_readRegister(0xD8, false),
                USBHost.max3421_readRegister(0xE8, false),
                USBHost.max3421_readRegister(0xF8, false));
  if (!hostReady) {
    // Initialization failure disables the host interrupt. Retry full-duplex SPI
    // at 1 MHz to distinguish a clock-rate problem from no chip response.
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(HOST_CS, LOW);
    SPI.transfer(0x8A);  // PINCTL write command
    SPI.transfer(0x10);  // FDUPSPI
    digitalWrite(HOST_CS, HIGH);
    SPI.endTransaction();
    Serial.printf("SLOW SPI 1MHz REV=%02X PINCTL=%02X\n",
                  readSlowRegister(0x90), readSlowRegister(0x88));
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }

  // The onboard display shares SPI with the FeatherWing.
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);

  Serial.println("Hidpress USB-only diagnostic v2; BLE disabled");
  Serial.printf("SPI SCK=%u MOSI=%u MISO=%u CS=%u IRQ=%u TFT_CS=%u\n",
                SCK, MOSI, MISO, HOST_CS, HOST_IRQ, TFT_CS);
  Serial.println("Calling USBHost.begin(1)...");
  hostReady = USBHost.begin(1);
  Serial.printf("USBHost.begin(1) returned %s\n", hostReady ? "true" : "false");
  printStatus();
}

void loop() {
  if (hostReady) {
    // A zero timeout keeps status output alive when there are no USB events.
    USBHost.task(0);
  }
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus >= 2000) {
    lastStatus = millis();
    printStatus();
  }
  delay(1);
}

extern "C" {
void tuh_mount_cb(uint8_t address) {
  Serial.printf("USB device mounted addr=%u\n", address);
}

void tuh_umount_cb(uint8_t address) {
  Serial.printf("USB device unmounted addr=%u\n", address);
}

void tuh_hid_mount_cb(uint8_t address, uint8_t instance,
                      uint8_t const *descriptor, uint16_t descriptorLength) {
  (void)descriptor;
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(address, &vid, &pid);
  ++mountedInterfaces;
  Serial.printf("HID mounted addr=%u inst=%u VID=%04X PID=%04X descriptor=%u bytes\n",
                address, instance, vid, pid, descriptorLength);
  if (!tuh_hid_receive_report(address, instance)) {
    Serial.println("ERROR requesting first HID report");
  }
}

void tuh_hid_umount_cb(uint8_t address, uint8_t instance) {
  if (mountedInterfaces) {
    --mountedInterfaces;
  }
  Serial.printf("HID unmounted addr=%u inst=%u\n", address, instance);
}

void tuh_hid_report_received_cb(uint8_t address, uint8_t instance,
                                uint8_t const *report, uint16_t length) {
  ++reportCount;
  Serial.printf("HID report addr=%u inst=%u len=%u:", address, instance, length);
  for (uint16_t i = 0; i < length; ++i) {
    Serial.printf(" %02X", report[i]);
  }
  Serial.println();
  if (!tuh_hid_receive_report(address, instance)) {
    Serial.println("ERROR requesting next HID report");
  }
}
}
