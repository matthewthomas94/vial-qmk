#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <HWCDC.h>
#include <atomic>
#include "usb/usb_host.h"
#include <UsbHostHid.h>
#include "hid_report_map.h"
#include "report_router.h"

namespace {
constexpr uint8_t bootPin = 0;
Adafruit_NeoPixel led(1, 21, NEO_RGB + NEO_KHZ800);
std::atomic<bool> connected{false}, authenticated{false}, failed{false};
std::atomic<bool> advertiseAgain{false};
// Pairing mode: drop the current host and refuse only that host for the window,
// so another host (new or re-pairing) can take the single connection.
std::atomic<bool> pairing{false}, newHostPaired{false}, rejectHost{false};
uint32_t pairingSince = 0;
constexpr uint32_t pairingHoldMs = 5000, pairingWindowMs = 60000;
esp_bd_addr_t currentPeer = {}, kickedPeer = {};
bool haveKickedPeer = false;
BLEServer *bleServer = nullptr;
std::atomic<uint16_t> connId{0};
BLECharacteristic *inputs[3] = {};
BLE2902 *subscriptions[3] = {};
constexpr uint8_t reportIds[] = {1, 2, 4};
constexpr uint8_t reportSizes[] = {8, 5, 2};
bool ready[3] = {};
bool hostStarted = false;
bool buttonReleased = false;
uint32_t buttonSince = 0;
hid_host_device_handle_t opened[2] = {};

enum class EventKind : uint8_t { Attach, Input, Detach, Error };
struct UsbEvent {
  EventKind kind;
  hid_host_device_handle_t device;
  uint8_t interfaceNumber;
  size_t length;
  uint8_t data[64];
};
QueueHandle_t events = nullptr;

void sendZero(size_t index) {
  uint8_t zero[8] = {};
  inputs[index]->setValue(zero, reportSizes[index]);
  inputs[index]->notify();
}

void releaseAll() {
  for (size_t i = 0; i < 3; ++i) {
    if (ready[i]) sendZero(i);
  }
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
    authenticated = false;
    connected = true;
    connId = param->connect.conn_id;
    esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT);
  }
  void onDisconnect(BLEServer *) override {
    authenticated = false;
    connected = false;
    advertiseAgain = true;
  }
};

class SecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t) override { return false; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    if (result.success && pairing && haveKickedPeer &&
        memcmp(kickedPeer, result.bd_addr, sizeof(esp_bd_addr_t)) == 0) {
      rejectHost = true;  // The host we dropped; keep the slot free for another one.
      return;
    }
    if (result.success) {
      memcpy(currentPeer, result.bd_addr, sizeof(esp_bd_addr_t));
      if (pairing) newHostPaired = true;
    }
    authenticated = result.success;
  }
};

void startPairing() {
  haveKickedPeer = connected && authenticated;
  if (haveKickedPeer) memcpy(kickedPeer, currentPeer, sizeof(esp_bd_addr_t));
  pairingSince = millis();
  newHostPaired = false;
  pairing = true;
  if (connected) {
    releaseAll();
    bleServer->disconnect(connId);  // onDisconnect restarts advertising.
  } else {
    BLEDevice::startAdvertising();
  }
}

void startBle() {
  BLEDevice::init("Hidpress S3 Bridge");
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
  BLEServer *server = bleServer = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEHIDDevice *hid = new BLEHIDDevice(server);
  for (size_t i = 0; i < 3; ++i) {
    inputs[i] = hid->inputReport(reportIds[i]);
    subscriptions[i] = static_cast<BLE2902 *>(
        inputs[i]->getDescriptorByUUID(BLEUUID(uint16_t(0x2902))));
    uint8_t zero[8] = {};
    inputs[i]->setValue(zero, reportSizes[i]);
  }
  hid->outputReport(1);  // LED writes are accepted, not forwarded in this prototype.
  hid->manufacturer()->setValue("Hidpress prototype");
  // Unassigned prototype identity, encoded little-endian (not a production VID).
  uint8_t pnp[] = {2, 0xff, 0xff, 1, 0, 0, 1};
  hid->deviceInfo()->getCharacteristic(BLEUUID(uint16_t(0x2a50)))->setValue(pnp, sizeof(pnp));
  hid->hidInfo(0, 2);  // Normally connectable; no remote wake claim.
  hid->reportMap(const_cast<uint8_t *>(hidReportMap), sizeof(hidReportMap));
  BLESecurity security;
  security.setAuthenticationMode(ESP_LE_AUTH_BOND);
  security.setCapability(ESP_IO_CAP_NONE);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  hid->startServices();
  hid->setBatteryLevel(100);  // Placeholder only: no voltage-sense wiring yet.
  BLEAdvertising *advertising = server->getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
  advertising->addServiceUUID(hid->hidService()->getUUID());
  advertising->setScanResponse(true);
  advertising->start();
}

void enqueue(const UsbEvent &event) {
  if (xQueueSend(events, &event, 0) != pdTRUE) failed = true;
}

void interfaceEvent(hid_host_device_handle_t device,
                    hid_host_interface_event_t kind, void *) {
  UsbEvent event{};
  event.device = device;
  if (kind == HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
    if (failed) return;
    hid_host_dev_params_t params{};
    if (hid_host_device_get_params(device, &params) != ESP_OK ||
        hid_host_device_get_raw_input_report_data(
            device, event.data, sizeof(event.data), &event.length) != ESP_OK) {
      failed = true;
      return;
    }
    event.kind = EventKind::Input;
    event.interfaceNumber = params.iface_num;
  } else if (kind == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
    event.kind = EventKind::Detach;
  } else {
    event.kind = EventKind::Error;
  }
  enqueue(event);
}

void driverEvent(hid_host_device_handle_t device,
                 hid_host_driver_event_t kind, void *) {
  if (kind == HID_HOST_DRIVER_EVENT_CONNECTED) {
    UsbEvent event{};
    event.kind = EventKind::Attach;
    event.device = device;
    enqueue(event);
  }
}

void usbEventsTask(void *) {
  for (;;) {
    uint32_t flags = 0;
    if (usb_host_lib_handle_events(portMAX_DELAY, &flags) != ESP_OK) failed = true;
  }
}

void startHost() {
  // The same PHY serves USB programming and USB host. Never arm on a Mac port.
  if (HWCDC::isPlugged()) {
    Serial.println("Host blocked: disconnect S3 USB from the Mac first.");
    return;
  }
  hostStarted = true;
  Serial.end();
  usb_host_config_t config{};
  config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  if (usb_host_install(&config) != ESP_OK ||
      xTaskCreate(usbEventsTask, "usb-events", 4096, nullptr, 4, nullptr) != pdPASS) {
    failed = true;
    return;
  }
  hid_host_driver_config_t hidConfig{};
  hidConfig.create_background_task = true;
  hidConfig.task_priority = 5;
  hidConfig.stack_size = 4096;
  hidConfig.core_id = tskNO_AFFINITY;
  hidConfig.callback = driverEvent;
  if (hid_host_install(&hidConfig) != ESP_OK) failed = true;
}

void attachDevice(hid_host_device_handle_t device) {
  hid_host_dev_params_t params{};
  hid_host_dev_info_t info{};
  if (hid_host_device_get_params(device, &params) != ESP_OK ||
      hid_host_get_device_info(device, &info) != ESP_OK) return;
  if (!isHidpressDevice(info.VID, info.PID)) return;
  if (params.iface_num != 0 && params.iface_num != 2) return;
  const size_t slot = params.iface_num == 0 ? 0 : 1;
  if (opened[slot]) { failed = true; return; }
  hid_host_device_config_t config{};
  config.callback = interfaceEvent;
  if (hid_host_device_open(device, &config) != ESP_OK) {
    failed = true;
    return;
  }
  opened[slot] = device;
  // Only force boot on the keyboard. Boot mouse protocol would lose scrolling.
  if (slot == 0 &&
      (params.sub_class != HID_SUBCLASS_BOOT_INTERFACE ||
       params.proto != HID_PROTOCOL_KEYBOARD ||
       hid_class_request_set_protocol(device, HID_REPORT_PROTOCOL_BOOT) != ESP_OK)) {
    failed = true;
    return;
  }
  if (hid_host_device_start(device) != ESP_OK) failed = true;
}

void processEvent(const UsbEvent &event) {
  if (event.kind == EventKind::Detach) {
    releaseAll();
    for (auto &device : opened) {
      if (device == event.device) device = nullptr;
    }
    hid_host_device_close(event.device);
  } else if (!failed && event.kind == EventKind::Attach) {
    attachDevice(event.device);
  } else if (!failed && event.kind == EventKind::Input) {
    BleReport report{};
    if (!routeReport(event.interfaceNumber, event.data, event.length, report)) return;
    for (size_t i = 0; i < 3; ++i) {
      if (ready[i] && report.id == reportIds[i]) {
        inputs[i]->setValue(report.data, report.length);
        inputs[i]->notify();
      }
    }
  } else if (event.kind == EventKind::Error) {
    failed = true;
  }
}

void updateLed() {
  uint32_t color;
  if (failed) color = led.Color(32, 0, 0);
  else if (pairing && !newHostPaired) color = (millis() / 250) % 2 ? led.Color(24, 0, 24) : 0;
  else if (!hostStarted) color = led.Color(0, 0, 24);
  else if (!opened[0] || !opened[1]) color = led.Color(28, 10, 0);
  else if (ready[0] && ready[1] && ready[2]) color = led.Color(0, 24, 0);
  else color = led.Color(0, 16, 24);
  if (led.getPixelColor(0) != color) {
    led.setPixelColor(0, color);
    led.show();
  }
}
}  // namespace

void bridgeSetup() {
  pinMode(bootPin, INPUT_PULLUP);
  led.begin();
  updateLed();
  Serial.begin(115200);
  Serial.setTxTimeoutMs(10);
  events = xQueueCreate(64, sizeof(UsbEvent));
  if (!events) failed = true;
  startBle();
  Serial.println("Hidpress S3 native bridge v1: USB host DISARMED.");
  Serial.println("BLE: Hidpress S3 Bridge. BOOT arms host only off the Mac.");
  Serial.println("Hold BOOT 5 s to pair a new host.");
}

void bridgeLoop() {
  if (advertiseAgain.exchange(false)) BLEDevice::startAdvertising();
  if (rejectHost.exchange(false)) bleServer->disconnect(connId);
  if (pairing && millis() - pairingSince >= pairingWindowMs) pairing = false;
  // Hold BOOT for 5 s at any time after boot to pair a new host.
  static bool pairButtonReleased = false;
  static uint32_t pairButtonSince = 0;
  if (digitalRead(bootPin) == HIGH) {
    pairButtonReleased = true;
    pairButtonSince = 0;
  } else if (pairButtonReleased) {
    if (pairButtonSince == 0) pairButtonSince = millis();
    if (millis() - pairButtonSince >= pairingHoldMs) {
      pairButtonReleased = false;
      startPairing();
    }
  }
  for (size_t i = 0; i < 3; ++i) {
    bool nowReady = connected && authenticated && subscriptions[i]->getNotifications();
    if (nowReady && !ready[i]) sendZero(i);
    ready[i] = nowReady;
  }
  static bool failureReleased = false;
  if (failed && !failureReleased) {
    releaseAll();
    failureReleased = true;
  }
  // Re-arm by itself after any reset (e.g. charger plug/unplug dips the 5V rail),
  // but only if no Mac was seen on S3 USB by then.
  static bool autoArmChecked = false;
  if (!autoArmChecked && millis() >= 3000) {
    autoArmChecked = true;
    if (!hostStarted && !failed && !HWCDC::isPlugged()) startHost();
  }
  if (!hostStarted && !failed) {
    if (digitalRead(bootPin) == HIGH) {
      buttonReleased = true;
      buttonSince = 0;
    } else if (buttonReleased) {
      if (buttonSince == 0) buttonSince = millis();
      if (millis() - buttonSince >= 1000) {
        buttonReleased = false;
        startHost();
      }
    }
  }
  UsbEvent event{};
  for (size_t count = 0; events && count < 16 && xQueueReceive(events, &event, 0) == pdTRUE; ++count) {
    processEvent(event);
  }
  static uint32_t lastStatus = 0;
  if (!hostStarted && millis() - lastStatus >= 5000) {
    Serial.printf("Host DISARMED; BLE connected=%d authenticated=%d; Mac USB=%d; heap=%u\r\n",
                  int(connected.load()), int(authenticated.load()),
                  int(HWCDC::isPlugged()), ESP.getFreeHeap());
    lastStatus = millis();
  }
  updateLed();
  delay(1);
}
