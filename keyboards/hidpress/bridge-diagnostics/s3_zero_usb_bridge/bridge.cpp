#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <HWCDC.h>
#include <Preferences.h>
#include <atomic>
#include <esp_bt_device.h>
#include "usb/usb_host.h"
#include <UsbHostHid.h>
#include "hid_report_map.h"
#include "report_router.h"
#include "host_slots.h"

namespace {
constexpr uint8_t bootPin = 0;
Adafruit_NeoPixel led(1, 21, NEO_RGB + NEO_KHZ800);
std::atomic<bool> connected{false}, authenticated{false}, failed{false};
std::atomic<bool> advertiseAgain{false};
// Three host slots, saved in flash. The keyboard's BT_PREV/BT_NEXT keys send
// reserved consumer usage 0x0011 + slot to pick one. Each slot advertises its
// own address (see host_slots.h).
constexpr uint16_t slotUsageBase = 0x0011;
HostSlots hosts = {};
uint8_t publicAddress[6] = {};
std::atomic<int> selected{0};
std::atomic<bool> slotsDirty{false};
Preferences prefs;
// Picking a slot locks advertising to its address until another is picked.
// Until then (after boot) advertising roams over the saved slots.
std::atomic<bool> locked{false};
std::atomic<int> advertised{0}, connectedSlot{0};
uint32_t advertisedSince = 0;
constexpr uint32_t roamDwellMs = 3000;
esp_ble_adv_params_t advParams = {0x20, 0x40, ADV_TYPE_IND, BLE_ADDR_TYPE_PUBLIC, {},
                                  BLE_ADDR_TYPE_PUBLIC, ADV_CHNL_ALL,
                                  ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY};
// Pairing mode: the selected slot was emptied and waits for a new host.
std::atomic<bool> pairing{false}, rejectHost{false};
uint32_t pairingSince = 0;
constexpr uint32_t pairingHoldMs = 5000, pairingWindowMs = 60000;
// Bonds are removed from the loop, not from Bluetooth callbacks.
esp_bd_addr_t staleBond = {};
std::atomic<bool> removeStaleBond{false};
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

enum class EventKind : uint8_t { Attach, Input, Detach };
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
    connectedSlot = advertised.load();
    esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT);
  }
  void onDisconnect(BLEServer *) override {
    authenticated = false;
    connected = false;
    advertiseAgain = true;
  }
};

// Bluedroid keeps a bond under the address the host paired from (a macOS
// private address) and reports that address on every reconnect, also
// after a restart. If the host pairs again it gets a new bond address, so
// hosts are also matched on the identity address from their PID key.
void identityOf(const uint8_t *bond, uint8_t *identity) {
  static esp_ble_bond_dev_t list[CONFIG_BT_SMP_MAX_BONDS];
  memcpy(identity, bond, 6);
  int count = CONFIG_BT_SMP_MAX_BONDS;
  if (esp_ble_get_bond_device_list(&count, list) != ESP_OK) return;
  for (int i = 0; i < count; ++i) {
    if (memcmp(list[i].bd_addr, bond, 6) == 0 &&
        (list[i].bond_key.key_mask & ESP_LE_KEY_PID)) {
      memcpy(identity, list[i].bond_key.pid_key.static_addr, 6);
    }
  }
}

class SecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t) override { return false; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    if (!result.success) {
      authenticated = false;
      return;
    }
    uint8_t identity[6];
    identityOf(result.bd_addr, identity);
    const int slot = connectedSlot;
    const AuthAction action = decideAuth(hosts, slot, result.bd_addr, identity);
    if (action == AuthAction::RejectAndForget) {
      memcpy(staleBond, result.bd_addr, sizeof(esp_bd_addr_t));
      removeStaleBond = true;
    }
    if (action != AuthAction::Accept) {
      rejectHost = true;  // Keep the connection free.
      return;
    }
    if (!hosts.used[slot] || memcmp(hosts.bond[slot], result.bd_addr, 6) != 0 ||
        memcmp(hosts.identity[slot], identity, 6) != 0) {
      assignSlot(hosts, slot, result.bd_addr, identity);
      slotsDirty = true;
    }
    selected = slot;
    pairing = false;
    authenticated = true;
  }
};

void saveSlots() {
  uint8_t data[slotCount * 7] = {};
  for (int i = 0; i < slotCount; ++i) {
    data[i * 7] = hosts.used[i];
    memcpy(data + i * 7 + 1, hosts.bond[i], sizeof(esp_bd_addr_t));
  }
  prefs.putBytes("slots", data, sizeof(data));
  prefs.putBytes("ids", hosts.identity, sizeof(hosts.identity));
  prefs.putBytes("gens", hosts.generation, sizeof(hosts.generation));
}

// Needs Bluetooth started: an upgrade reads and removes bonds.
void loadSlots() {
  prefs.begin("hosts");
  uint8_t data[slotCount * 7] = {};
  if (prefs.getBytes("slots", data, sizeof(data)) == sizeof(data)) {
    for (int i = 0; i < slotCount; ++i) {
      hosts.used[i] = data[i * 7];
      memcpy(hosts.bond[i], data + i * 7 + 1, sizeof(esp_bd_addr_t));
    }
  }
  if (prefs.isKey("gens")) {
    prefs.getBytes("ids", hosts.identity, sizeof(hosts.identity));
    prefs.getBytes("gens", hosts.generation, sizeof(hosts.generation));
    return;
  }
  // Before per-slot addresses every slot shared the public address, which
  // slot 1 keeps. Hosts saved in slots 2 and 3 have to pair again.
  for (int i = 0; i < slotCount; ++i) {
    if (!hosts.used[i]) continue;
    if (i == 0) {
      identityOf(hosts.bond[0], hosts.identity[0]);
    } else {
      esp_ble_remove_bond_device(hosts.bond[i]);
      clearSlot(hosts, i);
    }
  }
  saveSlots();
}

// Stop, move to the slot's address, start again. Bluedroid refuses a new
// random address while advertising; its calls run in order.
void advertise(int slot) {
  uint8_t address[6];
  const bool random = slotAddress(publicAddress, slot, hosts.generation[slot], address);
  advertised = slot;
  advertisedSince = millis();
  esp_ble_gap_stop_advertising();
  if (random) esp_ble_gap_set_rand_addr(address);
  advParams.own_addr_type = random ? BLE_ADDR_TYPE_RANDOM : BLE_ADDR_TYPE_PUBLIC;
  esp_ble_gap_start_advertising(&advParams);
}

void dropConnection() {
  if (connected) {
    releaseAll();
    bleServer->disconnect(connId);  // onDisconnect restarts advertising.
  } else {
    advertise(selected);
  }
}

// Empty the selected slot (forgetting its host) and wait for a new host.
void startPairing() {
  const int slot = selected;
  if (hosts.used[slot]) {
    esp_ble_remove_bond_device(hosts.bond[slot]);
    clearSlot(hosts, slot);
    saveSlots();
  }
  pairingSince = millis();
  pairing = true;
  locked = true;
  dropConnection();
}

void selectSlot(int slot) {
  selected = slot;
  locked = true;
  if (!hosts.used[slot]) return startPairing();  // Empty slot: pair a host into it.
  pairing = false;
  if (connected && connectedSlot == slot) return;  // Already on this slot's address.
  dropConnection();
}

void startBle() {
  BLEDevice::init("Hidpress S3 Bridge");
  memcpy(publicAddress, esp_bt_dev_get_address(), sizeof(publicAddress));
  loadSlots();
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
  // Advertising data is set once here; advertise() only moves the address.
  uint8_t address[6];
  if (slotAddress(publicAddress, 0, hosts.generation[0], address)) {
    advertising->setDeviceAddress(address, BLE_ADDR_TYPE_RANDOM);
  }
  advertising->start();
  advertisedSince = millis();
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
  } else {
    // Unplugging shows up as transfer errors as well as a disconnect.
    event.kind = EventKind::Detach;
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

// Close an interface we opened. A transfer error and its disconnect both
// arrive here, so only the first one closes it.
void closeDevice(hid_host_device_handle_t device) {
  for (auto &handle : opened) {
    if (device && handle == device) {
      handle = nullptr;
      hid_host_device_close(device);
    }
  }
}

// Failures here close the interface and leave the LED amber; replugging the
// keyboard tries again.
void attachDevice(hid_host_device_handle_t device) {
  hid_host_dev_params_t params{};
  hid_host_dev_info_t info{};
  if (hid_host_device_get_params(device, &params) != ESP_OK ||
      hid_host_get_device_info(device, &info) != ESP_OK) return;
  if (!isHidpressDevice(info.VID, info.PID)) return;
  if (params.iface_num != 0 && params.iface_num != 2) return;
  const size_t slot = params.iface_num == 0 ? 0 : 1;
  closeDevice(opened[slot]);  // Left over from a detach that never arrived.
  hid_host_device_config_t config{};
  config.callback = interfaceEvent;
  if (hid_host_device_open(device, &config) != ESP_OK) return;
  opened[slot] = device;
  // Only force boot on the keyboard. Boot mouse protocol would lose scrolling.
  if (slot == 0 &&
      (params.sub_class != HID_SUBCLASS_BOOT_INTERFACE ||
       params.proto != HID_PROTOCOL_KEYBOARD ||
       hid_class_request_set_protocol(device, HID_REPORT_PROTOCOL_BOOT) != ESP_OK)) {
    return closeDevice(device);
  }
  if (hid_host_device_start(device) != ESP_OK) closeDevice(device);
}

void processEvent(const UsbEvent &event) {
  if (event.kind == EventKind::Detach) {
    releaseAll();
    closeDevice(event.device);
  } else if (!failed && event.kind == EventKind::Attach) {
    attachDevice(event.device);
  } else if (!failed && event.kind == EventKind::Input) {
    BleReport report{};
    if (!routeReport(event.interfaceNumber, event.data, event.length, report)) return;
    if (report.id == 4) {
      const uint16_t usage = report.data[0] | report.data[1] << 8;
      if (usage >= slotUsageBase && usage < slotUsageBase + slotCount) {
        selectSlot(usage - slotUsageBase);
        return;
      }
    }
    for (size_t i = 0; i < 3; ++i) {
      if (ready[i] && report.id == reportIds[i]) {
        inputs[i]->setValue(report.data, report.length);
        inputs[i]->notify();
      }
    }
  }
}

void updateLed() {
  uint32_t color;
  if (failed) color = led.Color(32, 0, 0);
  else if (pairing) color = (millis() / 250) % 2 ? led.Color(24, 0, 24) : 0;
  else if (locked && hosts.used[selected] && !(connected && authenticated))
    color = (millis() / 250) % 2 ? led.Color(16, 16, 16) : 0;
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
  Serial.println("Hold BOOT 5 s to pair a new host into the selected slot.");
}

void bridgeLoop() {
  if (advertiseAgain.exchange(false)) advertise(selected);
  if (rejectHost.exchange(false)) bleServer->disconnect(connId);
  // The window only times the LED; the empty slot keeps taking a new host.
  if (pairing && millis() - pairingSince >= pairingWindowMs) pairing = false;
  // No slot picked since boot: give each saved slot's address a turn.
  if (!locked && !connected && millis() - advertisedSince >= roamDwellMs) {
    const int next = nextRoamingSlot(hosts, advertised);
    if (next != advertised) advertise(next);
  }
  if (slotsDirty.exchange(false)) saveSlots();
  if (removeStaleBond.exchange(false)) esp_ble_remove_bond_device(staleBond);
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
    Serial.printf("Host DISARMED; BLE connected=%d authenticated=%d slot=%d; Mac USB=%d; heap=%u\r\n",
                  int(connected.load()), int(authenticated.load()), selected.load() + 1,
                  int(HWCDC::isPlugged()), ESP.getFreeHeap());
    lastStatus = millis();
  }
  updateLed();
  delay(1);
}
