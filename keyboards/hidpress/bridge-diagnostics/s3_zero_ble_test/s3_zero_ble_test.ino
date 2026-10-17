#include <BleKeyboard.h>

BleKeyboard keyboard("Hidpress S3", "Hidpress prototype", 100);

void setup() {
  Serial.begin(115200);
  const uint32_t started = millis();
  while (!Serial && millis() - started < 3000) {
    delay(10);
  }

  Serial.printf("S3-Zero BLE test: flash=%u PSRAM=%u\r\n",
                ESP.getFlashChipSize(), ESP.getPsramSize());
  keyboard.begin();
  Serial.println("BLE started: Hidpress S3. No keystrokes will be sent.");
}

void loop() {
  static uint32_t lastStatus = 0;
  static bool wasConnected = false;
  const bool connected = keyboard.isConnected();
  const uint32_t now = millis();
  if (connected != wasConnected || now - lastStatus >= 5000) {
    Serial.printf("BLE %s uptime=%lu ms free_heap=%u\r\n",
                  connected ? "connected" : "waiting for connection",
                  static_cast<unsigned long>(now), ESP.getFreeHeap());
    wasConnected = connected;
    lastStatus = now;
  }
  // Intentionally do not send any keyboard or media reports in this test.
  delay(20);
}
