#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// ── Constants ─────────────────────────────────────────────────────────────────

namespace {

constexpr const char *kDeviceName   = "ShortcutButton Hello";
constexpr uint32_t kSendIntervalMs  = 5000;
constexpr uint32_t kSubTimeoutMs    = 30000;

constexpr uint8_t REPORT_ID_KEYBOARD = 0x01;
constexpr uint8_t HID_KEY_H = 0x0B;
constexpr uint8_t HID_KEY_E = 0x08;
constexpr uint8_t HID_KEY_L = 0x0F;
constexpr uint8_t HID_KEY_O = 0x12;

// Keyboard appearance code (BT SIG Assigned Numbers)
constexpr uint16_t kAppearanceKeyboard = 0x03C1;

struct KeyReport {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keys[6];
};

// ── HID Report Descriptor ─────────────────────────────────────────────────────

const uint8_t kHidReportDescriptor[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x19, 0xE0,        //   Usage Minimum (Keyboard LeftControl)
  0x29, 0xE7,        //   Usage Maximum (Keyboard Right GUI)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data,Variable,Absolute)  ; Modifier byte
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x01,        //   Input (Constant)                ; Reserved byte
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x65,        //   Logical Maximum (101)
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x19, 0x00,        //   Usage Minimum (Reserved)
  0x29, 0x65,        //   Usage Maximum (Keyboard Application)
  0x81, 0x00,        //   Input (Data,Array)               ; Key array
  0x95, 0x05,        //   Report Count (5)
  0x75, 0x01,        //   Report Size (1)
  0x05, 0x08,        //   Usage Page (LEDs)
  0x19, 0x01,        //   Usage Minimum (Num Lock)
  0x29, 0x05,        //   Usage Maximum (Kana)
  0x91, 0x02,        //   Output (Data,Variable,Absolute)  ; LED bits
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x03,        //   Report Size (3)
  0x91, 0x01,        //   Output (Constant)                ; LED padding
  0xC0               // End Collection
};

// ── State ─────────────────────────────────────────────────────────────────────

NimBLEHIDDevice      *hid         = nullptr;
NimBLECharacteristic *inputReport = nullptr;
NimBLECharacteristic *outReport   = nullptr;

bool     isConnected    = false;
bool     inputSubscribed = false;
uint32_t connectedAtMs  = 0;
uint32_t nextSendMs     = 0;
uint32_t lastWaitLogMs  = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────

void logMarker(const char *msg) {
  Serial0.print("<HELLO:");
  Serial0.print(msg);
  Serial0.println(">");
}

void sendReport(const KeyReport &report) {
  if (!inputReport) return;
  inputReport->setValue((uint8_t *)&report, sizeof(report));
  inputReport->notify();
}

void sendKeyStroke(uint8_t hidCode, uint8_t modifiers = 0) {
  KeyReport press = {};
  press.modifiers = modifiers;
  press.keys[0]   = hidCode;
  sendReport(press);
  delay(10);

  const KeyReport release = {};
  sendReport(release);
  delay(10);
}

void typeHello() {
  sendKeyStroke(HID_KEY_H);
  sendKeyStroke(HID_KEY_E);
  sendKeyStroke(HID_KEY_L);
  sendKeyStroke(HID_KEY_L);
  sendKeyStroke(HID_KEY_O);
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

class InputCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &info, uint16_t subValue) override {
    inputSubscribed = (subValue != 0);
    Serial0.printf(
      "<HELLO:CCCD_WRITE:conn=%u:value=0x%04X:subscribed=%u>\n",
      info.getConnHandle(), subValue, inputSubscribed ? 1 : 0
    );
  }

  void onStatus(NimBLECharacteristic *, NimBLEConnInfo &info, int code) override {
    Serial0.printf(
      "<HELLO:NOTIFY_STATUS:conn=%u:code=%d>\n",
      info.getConnHandle(), code
    );
  }
};

class OutputCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    const std::string val = chr->getValue();
    if (!val.empty()) {
      Serial0.printf("<HELLO:LED:0x%02X>\n", (uint8_t)val[0]);
    }
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    isConnected     = true;
    inputSubscribed = false;
    connectedAtMs   = millis();
    nextSendMs      = millis() + 3000;
    lastWaitLogMs   = 0;
    Serial0.printf(
      "<HELLO:CONNECTED:conn=%u:bond=%u:enc=%u>\n",
      info.getConnHandle(),
      info.isBonded()    ? 1 : 0,
      info.isEncrypted() ? 1 : 0
    );
  }

  void onDisconnect(NimBLEServer *, NimBLEConnInfo &info, int reason) override {
    isConnected     = false;
    inputSubscribed = false;
    Serial0.printf(
      "<HELLO:DISCONNECTED:conn=%u:reason=%d>\n",
      info.getConnHandle(), reason
    );
    NimBLEDevice::startAdvertising();
    logMarker("ADVERTISING");
  }

  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    Serial0.printf(
      "<HELLO:AUTH_DONE:conn=%u:enc=%u:bond=%u:mitm=%u>\n",
      info.getConnHandle(),
      info.isEncrypted()    ? 1 : 0,
      info.isBonded()       ? 1 : 0,
      info.isAuthenticated() ? 1 : 0
    );
  }
};

// ── Setup ─────────────────────────────────────────────────────────────────────

void configureRandomStaticAddress() {
  const uint64_t efuse = ESP.getEfuseMac();
  uint8_t addr[6] = {
    (uint8_t)(efuse >> 0),  (uint8_t)(efuse >> 8),
    (uint8_t)(efuse >> 16), (uint8_t)(efuse >> 24),
    (uint8_t)(efuse >> 32), (uint8_t)(efuse >> 40),
  };
  // Top two MSBs must be 1 for static-random address type.
  addr[5] = (uint8_t)((addr[5] & 0x3F) | 0xC0);

  const bool typeOk = NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  const bool addrOk = NimBLEDevice::setOwnAddr(addr);
  Serial0.printf(
    "<HELLO:ADDR_SET:typeOk=%u:addrOk=%u>\n",
    typeOk ? 1 : 0, addrOk ? 1 : 0
  );
}

void setupBleKeyboard() {
  NimBLEDevice::init(kDeviceName);

  configureRandomStaticAddress();

  // Just Works bonding — no passkey, no MITM, no Secure Connections.
  NimBLEDevice::setSecurityAuth(true, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  hid = new NimBLEHIDDevice(server);
  hid->setReportMap((uint8_t *)kHidReportDescriptor,
                    (uint16_t)sizeof(kHidReportDescriptor));
  hid->setManufacturer("Espressif");
  hid->setPnp(0x02, 0xe502, 0xa111, 0x0210);
  hid->setHidInfo(0x00, 0x02);

  inputReport = hid->getInputReport(REPORT_ID_KEYBOARD);
  outReport   = hid->getOutputReport(REPORT_ID_KEYBOARD);
  inputReport->setCallbacks(new InputCallbacks());
  outReport  ->setCallbacks(new OutputCallbacks());

  hid->startServices();
  hid->setBatteryLevel(100);

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(kAppearanceKeyboard);
  adv->addServiceUUID(hid->getHidService()        ->getUUID());
  adv->addServiceUUID(hid->getDeviceInfoService() ->getUUID());
  adv->addServiceUUID(hid->getBatteryService()    ->getUUID());
  adv->setPreferredParams(0x0006, 0x0012);
  NimBLEDevice::startAdvertising();

  Serial0.printf("<HELLO:ADDR:%s>\n",
                 NimBLEDevice::getAddress().toString().c_str());
  logMarker("ADVERTISING");
}

}  // namespace

// ── Arduino entry points ──────────────────────────────────────────────────────

void setup() {
  Serial0.begin(115200);
  delay(250);
  logMarker("BOOT");
  setupBleKeyboard();
}

void loop() {
  if (!isConnected) {
    delay(20);
    return;
  }

  // Wait for macOS to write 0x0001 to the input report's CCCD.
  // With fresh bonding (deleteAllBonds on boot + Forget on Mac) this always
  // fires via onSubscribe within a few seconds of AUTH_DONE.
  if (!inputSubscribed) {
    const uint32_t elapsed = millis() - connectedAtMs;
    if (elapsed < kSubTimeoutMs) {
      if (millis() - lastWaitLogMs > 1000) {
        lastWaitLogMs = millis();
        Serial0.printf("<HELLO:WAITING_CCCD:%lums>\n", (unsigned long)elapsed);
      }
      delay(20);
      return;
    }
    logMarker("CCCD_TIMEOUT:SENDING_ANYWAY");
  }

  if ((int32_t)(millis() - nextSendMs) >= 0) {
    nextSendMs += kSendIntervalMs;
    typeHello();
    logMarker("SENT_HELLO");
  }

  delay(2);
}
