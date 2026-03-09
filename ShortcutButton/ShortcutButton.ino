/*
  ShortcutButton - ESP32-S3-DevKitC-1 (N16R8)

  A 4x4 button matrix that sends programmable keyboard shortcuts via BLE HID.
  Use the USB serial connection for the web configurator, and Bluetooth for
  keyboard shortcut execution.

  Hardware:
  - ESP32-S3-DevKitC-1 (WROOM-1-N16R8, 16MB flash, 8MB PSRAM)
  - 4x4 Button Matrix (J1 pins 4-11: GPIO4-7 rows, GPIO15-18 cols)
  - ST7789 Display (SPI shared: CS=GPIO9, DC=GPIO8, RST=GPIO14)

  Arduino IDE board settings:
  - Board:            ESP32S3 Dev Module
  - USB Mode:         USB-OTG (TinyUSB)
  - USB CDC On Boot:  Enabled
  - Flash Size:       16MB (128Mb)
  - Partition Scheme: Default 4MB with spiffs (or any)

  First upload: plug into the UART port and upload once.
  Normal use: plug into the native USB port (labeled "USB").
  The shortcuts are stored in ESP32 flash (NVS) and persist across power cycles.
*/

#include <SPI.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#define US_KEYBOARD
#include "HIDKeyboardTypes.h"

#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
#include "USB.h"
#endif

Preferences prefs;
bool storageReady = false;


char bleName[64] = "ShortcutButton";  // loaded from NVS before BLE init
const char *BLE_MANUFACTURER = "Click";
NimBLEHIDDevice      *bleHid          = nullptr;
NimBLECharacteristic *bleKeyboardInput  = nullptr;
NimBLECharacteristic *bleKeyboardOutput = nullptr;
bool bleConnected  = false;
bool bleSubscribed = false;
volatile bool bleDisplayNeedsUpdate = false;
uint16_t bleConnHandle = BLE_HS_CONN_HANDLE_NONE;
bool bleEncrypted = false;

uint8_t bleKeyReport[8] = {0};

// Standard 8-byte keyboard report (raw bytes):
// [0]=modifiers, [1]=reserved, [2..7]=up to 6 simultaneous keys.
const uint8_t bleKeyboardReportMap[] = {
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
  0x81, 0x00,        //   Input (Data,Array)               ; Key array (6 keys)
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

class ShortcutBleInputCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &info, uint16_t subValue) override {
    bleSubscribed = (subValue != 0);
    Serial.printf("<BLE:CCCD:%u:sub=%u>\n", info.getConnHandle(), bleSubscribed ? 1 : 0);
  }
  void onStatus(NimBLECharacteristic *, NimBLEConnInfo &info, int code) override {
    Serial.printf("<BLE:NOTIFY_STATUS:%u:code=%d>\n", info.getConnHandle(), code);
  }
};

class ShortcutBleOutputCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    const std::string val = chr->getValue();
    if (!val.empty()) {
      Serial.printf("<BLE:LED:0x%02X>\n", (uint8_t)val[0]);
    }
  }
};

class ShortcutBleServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    bleConnected  = true;
    bleSubscribed = false;
    bleConnHandle = info.getConnHandle();
    bleEncrypted = info.isEncrypted();
    bleDisplayNeedsUpdate = true;
    Serial.printf("<BLE:CONNECTED:conn=%u:bond=%u:enc=%u>\n",
      bleConnHandle, info.isBonded() ? 1 : 0, bleEncrypted ? 1 : 0);

    if (!bleEncrypted) {
      int secRc = 0;
      bool secStarted = NimBLEDevice::startSecurity(bleConnHandle, &secRc);
      Serial.printf("<BLE:SECURITY_REQ:conn=%u:started=%u:rc=%d>\n",
        bleConnHandle, secStarted ? 1 : 0, secRc);
    }
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &info, int reason) override {
    bleConnected  = false;
    bleSubscribed = false;
    bleConnHandle = BLE_HS_CONN_HANDLE_NONE;
    bleEncrypted = false;
    bleDisplayNeedsUpdate = true;
    memset(bleKeyReport, 0, sizeof(bleKeyReport));
    Serial.printf("<BLE:DISCONNECTED:conn=%u:reason=%d>\n", info.getConnHandle(), reason);
    NimBLEDevice::startAdvertising();
  }
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    bleEncrypted = info.isEncrypted();
    Serial.printf("<BLE:AUTH_DONE:conn=%u:enc=%u:bond=%u>\n",
      info.getConnHandle(),
      bleEncrypted ? 1 : 0,
      info.isBonded() ? 1 : 0);

    if (!bleEncrypted) {
      Serial.printf("<BLE:AUTH_FAIL:conn=%u>\n", info.getConnHandle());
      NimBLEDevice::getServer()->disconnect(info.getConnHandle());
    }
  }
};

bool bleReadyForReports() {
  return bleConnected &&
    bleEncrypted &&
    bleConnHandle != BLE_HS_CONN_HANDLE_NONE &&
    bleKeyboardInput != nullptr;
}

bool bleSendReport() {
  if (!bleReadyForReports()) return false;

  // Send directly to the active connection so we don't depend on a fresh
  // onSubscribe callback after host-side reconnect caching behavior.
  bool sent = bleKeyboardInput->notify(bleKeyReport, sizeof(bleKeyReport), bleConnHandle);
  if (!sent) {
    // Fallback to the standard subscribed-client path.
    bleKeyboardInput->setValue(bleKeyReport, sizeof(bleKeyReport));
    sent = bleKeyboardInput->notify();
  }

  if (!sent) {
    Serial.printf("<BLE:NOTIFY_FAIL:conn=%u:enc=%u:sub=%u>\n",
      bleConnHandle, bleEncrypted ? 1 : 0, bleSubscribed ? 1 : 0);
  }
  return sent;
}

void bleReleaseAll() {
  bool hadKeys = false;
  for (size_t i = 0; i < sizeof(bleKeyReport); i++) {
    if (bleKeyReport[i] != 0) {
      hadKeys = true;
      break;
    }
  }
  memset(bleKeyReport, 0, sizeof(bleKeyReport));
  if (hadKeys) bleSendReport();
}

bool blePressRaw(uint8_t rawHidCode) {
  if (!bleReadyForReports() || rawHidCode == 0) return false;
  bool changed = false;

  if (rawHidCode >= 0xE0 && rawHidCode <= 0xE7) {
    uint8_t bit = 1 << (rawHidCode - 0xE0);
    if ((bleKeyReport[0] & bit) == 0) {
      bleKeyReport[0] |= bit;
      changed = true;
    }
  } else {
    for (int i = 2; i < 8; i++) {
      if (bleKeyReport[i] == rawHidCode) return false;
    }
    for (int i = 2; i < 8; i++) {
      if (bleKeyReport[i] == 0) {
        bleKeyReport[i] = rawHidCode;
        changed = true;
        break;
      }
    }
  }

  if (changed) bleSendReport();
  return changed;
}

bool bleReleaseRaw(uint8_t rawHidCode) {
  if (!bleReadyForReports() || rawHidCode == 0) return false;
  bool changed = false;

  if (rawHidCode >= 0xE0 && rawHidCode <= 0xE7) {
    uint8_t bit = 1 << (rawHidCode - 0xE0);
    if (bleKeyReport[0] & bit) {
      bleKeyReport[0] &= ~bit;
      changed = true;
    }
  } else {
    for (int i = 2; i < 8; i++) {
      if (bleKeyReport[i] == rawHidCode) {
        bleKeyReport[i] = 0;
        changed = true;
      }
    }
  }

  if (changed) bleSendReport();
  return changed;
}

bool bleTypeChar(char c) {
  if (!bleReadyForReports()) return false;

  const uint8_t idx = static_cast<uint8_t>(c);
  if (idx >= KEYMAP_SIZE) return false;
  const KEYMAP &entry = keymap[idx];
  if (entry.usage == 0) return false;

  uint8_t savedReport[8];
  memcpy(savedReport, bleKeyReport, sizeof(savedReport));

  bleKeyReport[0] = savedReport[0] | entry.modifier;
  for (int i = 2; i < 8; i++) bleKeyReport[i] = 0;
  bleKeyReport[2] = entry.usage;

  bleSendReport();
  delay(8);

  memcpy(bleKeyReport, savedReport, sizeof(bleKeyReport));
  bleSendReport();
  delay(4);
  return true;
}

bool waitForBleConnection(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (!(bleConnected && bleEncrypted) && (millis() - start < timeoutMs)) {
    delay(5);
  }
  return bleConnected && bleEncrypted;
}

void configureRandomStaticAddress() {
  const uint64_t efuse = ESP.getEfuseMac();
  uint8_t addr[6] = {
    (uint8_t)(efuse >> 0),  (uint8_t)(efuse >> 8),
    (uint8_t)(efuse >> 16), (uint8_t)(efuse >> 24),
    (uint8_t)(efuse >> 32), (uint8_t)(efuse >> 40),
  };
  // Top two MSBs must be 1 for static-random address type.
  addr[5] = (uint8_t)((addr[5] & 0x3F) | 0xC0);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  NimBLEDevice::setOwnAddr(addr);
}

void setupBleKeyboard() {
  NimBLEDevice::init(bleName);
  configureRandomStaticAddress();

  // Just Works bonding — no passkey, no MITM, no Secure Connections.
  NimBLEDevice::setSecurityAuth(true, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ShortcutBleServerCallbacks());

  bleHid = new NimBLEHIDDevice(server);
  bleHid->setReportMap((uint8_t *)bleKeyboardReportMap, (uint16_t)sizeof(bleKeyboardReportMap));
  bleHid->setManufacturer(BLE_MANUFACTURER);
  bleHid->setPnp(0x02, 0xe502, 0xa111, 0x0210);
  bleHid->setHidInfo(0x00, 0x02);

  bleKeyboardInput  = bleHid->getInputReport(REPORT_ID_KEYBOARD);
  bleKeyboardOutput = bleHid->getOutputReport(REPORT_ID_KEYBOARD);
  bleKeyboardInput->setCallbacks(new ShortcutBleInputCallbacks());
  bleKeyboardOutput->setCallbacks(new ShortcutBleOutputCallbacks());

  bleHid->startServices();
  bleHid->setBatteryLevel(100);

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setMinInterval(32);   // 20 ms — fast advertising so host reconnects quickly
  adv->setMaxInterval(64);   // 40 ms
  adv->setAppearance(0x03C1);  // HID Keyboard
  adv->addServiceUUID(bleHid->getHidService()->getUUID());
  adv->addServiceUUID(bleHid->getDeviceInfoService()->getUUID());
  adv->addServiceUUID(bleHid->getBatteryService()->getUUID());
  adv->setPreferredParams(0x0006, 0x0012);
  // Advertising is started at the END of setup(), after full initialization,
  // so the host can only connect to a fully-ready device.
}

// Display pins (J1 left header)
#define TFT_CS   9   // J1 pin 15
#define TFT_DC   8   // J1 pin 12
#define TFT_RST 14   // J1 pin 20

// SPI pins — must be explicit on ESP32-S3
// MOSI=11 (J1 pin 17), SCK=12 (J1 pin 18), MISO=13 (J1 pin 19)
#define SPI_MOSI 11
#define SPI_SCK  12
#define SPI_MISO 13

// Colors (RGB565)
#define BLACK      0x0000
#define WHITE      0xFFFF
#define GRAY       0x7BEF
#define DARKGRAY   0x2104
#define LIGHTGRAY  0xC618
#define GREEN      0x07E0
#define CYAN       0x07FF
// Dynamic theme colors (loaded from NVS or set via serial)
// Index: 0=bg, 1=hdr, 2=cardOff, 3=cardOn, 4=bordOff, 5=bordOn, 6=numDim, 7=txOn, 8=txOff
#define THEME_SLOTS 9
uint16_t themeColor[THEME_SLOTS] = {
  0xCDD3, 0x7B49, 0xDE97, 0xBD0E, 0x9C4D, 0x8B42, 0xA490, 0x3963, 0x9C4D
};
// Convenience accessors
#define BGDARK     themeColor[0]
#define HDR_BG     themeColor[1]
#define CARD_OFF   themeColor[2]
#define CARD_ON    themeColor[3]
#define BORD_OFF   themeColor[4]
#define BORD_ON    themeColor[5]
#define NUM_DIM    themeColor[6]
#define TXON       themeColor[7]   // text on active buttons
#define TXOFF      themeColor[8]   // text on empty buttons
#define SIG_ADV    0xDD24

// Per-button custom colors (0 = use theme default)
// Each button: [0]=cardOff, [1]=cardOn, [2]=bordOff, [3]=bordOn, [4]=txOn, [5]=txOff
#define BTN_COLOR_SLOTS 6
uint16_t btnCustom[16][BTN_COLOR_SLOTS] = {{0}};

// Preset themes (PROGMEM): {bg, hdr, cardOff, cardOn, bordOff, bordOn, numDim, txOn, txOff}
const PROGMEM uint16_t presetThemes[][THEME_SLOTS] = {
  {0xCDD3,0x7B49,0xDE97,0xBD0E,0x9C4D,0x8B42,0xA490,0x3963,0x9C4D}, //  0: Beige
  {0x0863,0x08C5,0x10C5,0x0928,0x3A4E,0x06BF,0x4228,0xFFFF,0x3A4E}, //  1: Dark
  {0x0000,0x2104,0x1082,0x2104,0x7BEF,0xFFFF,0x5AEB,0xFFFF,0x7BEF}, //  2: Mono
  {0x0208,0x0410,0x0208,0x0430,0x2D7F,0x07FF,0x3A4E,0xFFFF,0x2D7F}, //  3: Ocean
  {0x0260,0x2320,0x0260,0x03A0,0x3CC5,0x07E0,0x4C85,0xFFFF,0x3CC5}, //  4: Forest
  {0xF71B,0x8808,0xF6BA,0xD34E,0xB24B,0xD8CD,0xB38E,0x3906,0xB24B}, //  5: Rose
  {0xFDA0,0x8200,0xFE20,0xDC40,0xB420,0xFA00,0xCC40,0x3000,0xB420}, //  6: Sunset
  {0x1084,0x2889,0x1084,0x308C,0x5190,0x897F,0x4A8C,0xFFFF,0x5190}, //  7: Lavender
  {0x1082,0x2124,0x18C3,0x2965,0x4208,0x6B4D,0x4A49,0xC618,0x4208}, //  8: Slate
  {0xFED4,0x9300,0xFEB2,0xDD00,0xAB60,0xC3A0,0xAB80,0x3000,0xAB60}, //  9: Amber
  {0x0400,0x0240,0x0420,0x0560,0x2D24,0x47E9,0x3C85,0xFFFF,0x2D24}, // 10: Mint
  {0xC65A,0x630C,0xDEDB,0xAD75,0x8C51,0x6B4D,0x9492,0x3186,0x8C51}, // 11: Stone
  {0x1083,0x4808,0x1083,0x4008,0x8810,0xF800,0x5808,0xFFFF,0x8810}, // 12: Cherry
  {0xE73C,0x8C71,0xF79E,0xCE59,0xAD55,0x7BCF,0xAD55,0x2104,0xAD55}, // 13: Cloud
  {0x0000,0x0000,0x0841,0x18E3,0x4A49,0xFDA0,0x39C7,0xFFFF,0x4A49}, // 14: Midnight
};
#define NUM_PRESETS 15
uint8_t currentThemeId = 0; // 0-14 = preset, 255 = custom
bool showButtonNumbers = true;

// Display dimensions
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 320

// Grid layout constants
#define HEADER_H    27    // header bar height (26px bar + 1px rule line)
#define BTN_SIZE    54    // square button side length
#define BTN_RADIUS   7    // corner radius for rounded buttons
#define H_GAP        4    // horizontal gap between buttons
#define V_GAP        4    // vertical gap between buttons
// Auto-derived margins (centered grid):
//   H_MARGIN = (240 - 4*54 - 3*4) / 2 = 6
//   V_MARGIN = (320 - 27 - 4*54 - 3*4) / 2 = 32
#define H_MARGIN    ((DISPLAY_WIDTH  - 4*BTN_SIZE - 3*H_GAP) / 2)
#define V_MARGIN    ((DISPLAY_HEIGHT - HEADER_H - 4*BTN_SIZE - 3*V_GAP) / 2)

// Minimal 5x7 font (ASCII 32-90, stored in PROGMEM)
const PROGMEM uint8_t font5x7[] = {
  0x00,0x00,0x00,0x00,0x00, // space
  0x00,0x00,0x5F,0x00,0x00, // !
  0x00,0x07,0x00,0x07,0x00, // "
  0x14,0x7F,0x14,0x7F,0x14, // #
  0x24,0x2A,0x7F,0x2A,0x12, // $
  0x23,0x13,0x08,0x64,0x62, // %
  0x36,0x49,0x55,0x22,0x50, // &
  0x00,0x05,0x03,0x00,0x00, // '
  0x00,0x1C,0x22,0x41,0x00, // (
  0x00,0x41,0x22,0x1C,0x00, // )
  0x08,0x2A,0x1C,0x2A,0x08, // *
  0x08,0x08,0x3E,0x08,0x08, // +
  0x00,0x50,0x30,0x00,0x00, // ,
  0x08,0x08,0x08,0x08,0x08, // -
  0x00,0x60,0x60,0x00,0x00, // .
  0x20,0x10,0x08,0x04,0x02, // /
  0x3E,0x51,0x49,0x45,0x3E, // 0
  0x00,0x42,0x7F,0x40,0x00, // 1
  0x42,0x61,0x51,0x49,0x46, // 2
  0x21,0x41,0x45,0x4B,0x31, // 3
  0x18,0x14,0x12,0x7F,0x10, // 4
  0x27,0x45,0x45,0x45,0x39, // 5
  0x3C,0x4A,0x49,0x49,0x30, // 6
  0x01,0x71,0x09,0x05,0x03, // 7
  0x36,0x49,0x49,0x49,0x36, // 8
  0x06,0x49,0x49,0x29,0x1E, // 9
  0x00,0x36,0x36,0x00,0x00, // :
  0x00,0x56,0x36,0x00,0x00, // ;
  0x00,0x08,0x14,0x22,0x41, // <
  0x14,0x14,0x14,0x14,0x14, // =
  0x41,0x22,0x14,0x08,0x00, // >
  0x02,0x01,0x51,0x09,0x06, // ?
  0x32,0x49,0x79,0x41,0x3E, // @
  0x7E,0x11,0x11,0x11,0x7E, // A
  0x7F,0x49,0x49,0x49,0x36, // B
  0x3E,0x41,0x41,0x41,0x22, // C
  0x7F,0x41,0x41,0x22,0x1C, // D
  0x7F,0x49,0x49,0x49,0x41, // E
  0x7F,0x09,0x09,0x01,0x01, // F
  0x3E,0x41,0x41,0x51,0x32, // G
  0x7F,0x08,0x08,0x08,0x7F, // H
  0x00,0x41,0x7F,0x41,0x00, // I
  0x20,0x40,0x41,0x3F,0x01, // J
  0x7F,0x08,0x14,0x22,0x41, // K
  0x7F,0x40,0x40,0x40,0x40, // L
  0x7F,0x02,0x04,0x02,0x7F, // M
  0x7F,0x04,0x08,0x10,0x7F, // N
  0x3E,0x41,0x41,0x41,0x3E, // O
  0x7F,0x09,0x09,0x09,0x06, // P
  0x3E,0x41,0x51,0x21,0x5E, // Q
  0x7F,0x09,0x19,0x29,0x46, // R
  0x46,0x49,0x49,0x49,0x31, // S
  0x01,0x01,0x7F,0x01,0x01, // T
  0x3F,0x40,0x40,0x40,0x3F, // U
  0x1F,0x20,0x40,0x20,0x1F, // V
  0x7F,0x20,0x18,0x20,0x7F, // W
  0x63,0x14,0x08,0x14,0x63, // X
  0x03,0x04,0x78,0x04,0x03, // Y
  0x61,0x51,0x49,0x45,0x43, // Z
};

// ST7789 command helpers
void tftCmd(uint8_t cmd) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(TFT_DC, LOW);
  digitalWrite(TFT_CS, LOW);
  SPI.transfer(cmd);
  digitalWrite(TFT_CS, HIGH);
  SPI.endTransaction();
}

void tftData(uint8_t data) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_CS, LOW);
  SPI.transfer(data);
  digitalWrite(TFT_CS, HIGH);
  SPI.endTransaction();
}

void tftInit() {
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_RST, OUTPUT);

  digitalWrite(TFT_CS, HIGH);  // Deselect display

  // On ESP32-S3, SPI pins must be specified explicitly.
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);

  // Hardware reset
  digitalWrite(TFT_RST, HIGH);
  delay(50);
  digitalWrite(TFT_RST, LOW);
  delay(50);
  digitalWrite(TFT_RST, HIGH);
  delay(150);

  tftCmd(0x01); delay(150);  // Software reset
  tftCmd(0x11); delay(120);  // Sleep out

  tftCmd(0x3A); tftData(0x55);  // 16-bit color (RGB565)
  tftCmd(0x36); tftData(0xC0);  // Memory access: 180° rotation (MY+MX)
  tftCmd(0x21);                 // Display inversion on (needed for many ST7789 displays)
  tftCmd(0x13);                 // Normal display mode
  tftCmd(0x29); delay(50);      // Display on
}

void tftSetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  tftCmd(0x2A);
  tftData(x0 >> 8); tftData(x0 & 0xFF);
  tftData(x1 >> 8); tftData(x1 & 0xFF);
  tftCmd(0x2B);
  tftData(y0 >> 8); tftData(y0 & 0xFF);
  tftData(y1 >> 8); tftData(y1 & 0xFF);
  tftCmd(0x2C);
}

void tftFillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (w == 0 || h == 0) return;
  tftSetWindow(x, y, x + w - 1, y + h - 1);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_CS, LOW);
  uint8_t hi = color >> 8, lo = color & 0xFF;
  for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
    SPI.transfer(hi);
    SPI.transfer(lo);
  }
  digitalWrite(TFT_CS, HIGH);
  SPI.endTransaction();
}

void tftDrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  tftFillRect(x, y, w, 1, color);
  tftFillRect(x, y + h - 1, w, 1, color);
  tftFillRect(x, y, 1, h, color);
  tftFillRect(x + w - 1, y, 1, h, color);
}

// Filled rounded rectangle using row-scan with integer sqrt at corners.
// sqrtf is called only for the 2*r corner rows, so it's fast in practice.
void tftFillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  for (int16_t i = 0; i < h; i++) {
    int16_t xo = 0;
    if (i < r) {
      int16_t dy = r - 1 - i;
      xo = r - (int16_t)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    } else if (i >= h - r) {
      int16_t dy = i - (h - r);
      xo = r - (int16_t)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    }
    if (w - 2 * xo > 0)
      tftFillRect(x + xo, y + i, w - 2 * xo, 1, color);
  }
}

void tftDrawChar(uint16_t x, uint16_t y, char c, uint16_t color) {
  if (c < 32 || c > 90) c = ' ';
  const uint8_t* glyph = font5x7 + (c - 32) * 5;
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(glyph + col);
    for (uint8_t row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        tftFillRect(x + col, y + row, 1, 1, color);
      }
    }
  }
}

void tftPrint(uint16_t x, uint16_t y, const char* str, uint16_t color) {
  while (*str) {
    char c = *str++;
    if (c >= 'a' && c <= 'z') c -= 32;
    tftDrawChar(x, y, c, color);
    x += 6;
  }
}

void tftPrintF(uint16_t x, uint16_t y, const __FlashStringHelper* str, uint16_t color) {
  const char* p = (const char*)str;
  while (char c = pgm_read_byte(p++)) {
    if (c >= 'a' && c <= 'z') c -= 32;
    tftDrawChar(x, y, c, color);
    x += 6;
  }
}

// 2x scaled character (10x14 pixels) for header title.
void tftDrawChar2x(uint16_t x, uint16_t y, char c, uint16_t color) {
  if (c < 32 || c > 90) c = ' ';
  const uint8_t* glyph = font5x7 + (c - 32) * 5;
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(glyph + col);
    for (uint8_t row = 0; row < 7; row++) {
      if (line & (1 << row))
        tftFillRect(x + col * 2, y + row * 2, 2, 2, color);
    }
  }
}

void tftPrintF2x(uint16_t x, uint16_t y, const __FlashStringHelper* str, uint16_t color) {
  const char* p = (const char*)str;
  while (char c = pgm_read_byte(p++)) {
    if (c >= 'a' && c <= 'z') c -= 32;
    tftDrawChar2x(x, y, c, color);
    x += 12;  // 10px char + 2px spacing at 2x scale
  }
}

// 3-bar signal strength icon (8x7 pixels).
// Represents BLE connectivity — color indicates state.
void drawSignalIcon(uint16_t x, uint16_t y, uint16_t color) {
  tftFillRect(x,     y + 4, 2, 3, color);  // short bar
  tftFillRect(x + 3, y + 2, 2, 5, color);  // medium bar
  tftFillRect(x + 6, y,     2, 7, color);  // tall bar
}

// Matrix configuration
const int ROWS = 4;
const int COLS = 4;
const int NUM_BUTTONS = ROWS * COLS;  // 16 buttons

// Pin assignments — 8 consecutive physical pins on J1 (left header, pins 4-11)
// J1 pin:  4      5      6      7
const int rowPins[ROWS] = {4, 5, 6, 7};
// J1 pin:  8      9      10     11
const int colPins[COLS] = {15, 16, 17, 18};

// Debounce settings
const unsigned long DEBOUNCE_DELAY = 50;

// Maximum steps per shortcut (reduced for RAM)
const int MAX_STEPS = 16;

// Maximum text length for type steps (reduced for RAM)
const int MAX_TEXT_LENGTH = 20;

// Maximum name length for display
const int MAX_NAME_LENGTH = 16;
constexpr size_t SHORTCUT_BLOB_MAX = 2 + (MAX_STEPS * 4) + MAX_TEXT_LENGTH + 1 + MAX_NAME_LENGTH;

// Step types
const byte STEP_PRESS = 1;
const byte STEP_RELEASE = 2;
const byte STEP_TYPE = 3;
// STEP_DELAY: fixed delay. delay_ms = (keyType << 8) | keyCode  (0–65535 ms)
const byte STEP_DELAY = 4;
// STEP_DELAY_DYNAMIC: random delay each execution.
//   min_ms = keyType * 20,  max_ms = keyCode * 20  (0–5100 ms each, 20 ms resolution)
const byte STEP_DELAY_DYNAMIC = 5;

// Key types
const byte KEY_TYPE_REGULAR = 0x00;
const byte KEY_TYPE_MODIFIER = 0x80;

// Structure to hold a step
struct Step {
  byte action;
  byte keyType;
  byte keyCode;
  byte textLen;
};

// Single working buffer for current shortcut (on-demand loading saves RAM)
Step currentSteps[MAX_STEPS];
int currentStepCount = 0;
char currentTextBuffer[MAX_TEXT_LENGTH];
int currentTextLen = 0;
int currentButtonIdx = -1;
char currentName[MAX_NAME_LENGTH + 1];

// Track which buttons have shortcuts (loaded at startup)
bool buttonHasShortcut[NUM_BUTTONS];

// Button names for display (stored in RAM for quick access)
char buttonNames[NUM_BUTTONS][MAX_NAME_LENGTH + 1];  // +1 for null terminator

// Matrix state tracking
byte lastButtonState[NUM_BUTTONS];
byte buttonState[NUM_BUTTONS];
unsigned long lastDebounceTime[NUM_BUTTONS];

// Serial communication
const char CMD_START = '<';
const char CMD_END = '>';
String serialBuffer = "";
bool receiving = false;

void getStorageKey(int btnIdx, char *keyOut, size_t keyOutLen) {
  snprintf(keyOut, keyOutLen, "b%02d", btnIdx);
}

bool serializeCurrentShortcut(uint8_t *outBuf, size_t outBufLen, size_t &outLen) {
  outLen = 0;
  if (currentStepCount <= 0) return true;
  if (currentStepCount > MAX_STEPS || currentTextLen < 0 || currentTextLen > MAX_TEXT_LENGTH) return false;
  if (outBufLen < SHORTCUT_BLOB_MAX) return false;

  size_t idx = 0;
  outBuf[idx++] = (uint8_t)currentStepCount;
  outBuf[idx++] = (uint8_t)currentTextLen;

  for (int i = 0; i < currentStepCount; i++) {
    outBuf[idx++] = currentSteps[i].action;
    outBuf[idx++] = currentSteps[i].keyType;
    outBuf[idx++] = currentSteps[i].keyCode;
    outBuf[idx++] = currentSteps[i].textLen;
  }

  for (int i = 0; i < currentTextLen; i++) {
    outBuf[idx++] = (uint8_t)currentTextBuffer[i];
  }

  int nameLen = strnlen(currentName, MAX_NAME_LENGTH);
  outBuf[idx++] = (uint8_t)nameLen;
  for (int i = 0; i < nameLen; i++) {
    outBuf[idx++] = (uint8_t)currentName[i];
  }

  outLen = idx;
  return true;
}

bool deserializeShortcutToCurrent(const uint8_t *buf, size_t len) {
  currentStepCount = 0;
  currentTextLen = 0;
  currentName[0] = '\0';

  if (len < 3) return false;

  const int stepCount = buf[0];
  const int textLen = buf[1];
  if (stepCount <= 0 || stepCount > MAX_STEPS || textLen < 0 || textLen > MAX_TEXT_LENGTH) return false;

  size_t idx = 2;
  size_t stepBytes = (size_t)stepCount * 4;
  if (len < idx + stepBytes + (size_t)textLen + 1) return false;

  currentStepCount = stepCount;
  currentTextLen = textLen;

  for (int i = 0; i < currentStepCount; i++) {
    currentSteps[i].action = buf[idx++];
    currentSteps[i].keyType = buf[idx++];
    currentSteps[i].keyCode = buf[idx++];
    currentSteps[i].textLen = buf[idx++];
  }

  for (int i = 0; i < currentTextLen; i++) {
    currentTextBuffer[i] = (char)buf[idx++];
  }

  int nameLen = buf[idx++];
  if (nameLen < 0 || nameLen > MAX_NAME_LENGTH || len < idx + (size_t)nameLen) return false;
  for (int i = 0; i < nameLen; i++) {
    currentName[i] = (char)buf[idx++];
  }
  currentName[nameLen] = '\0';

  return true;
}

bool loadShortcutMetadata(int btnIdx, bool &hasShortcut, char *nameOut, size_t nameOutLen) {
  hasShortcut = false;
  if (nameOutLen > 0) nameOut[0] = '\0';
  if (!storageReady) return false;

  char key[8];
  getStorageKey(btnIdx, key, sizeof(key));
  size_t blobLen = prefs.getBytesLength(key);
  if (blobLen == 0 || blobLen > SHORTCUT_BLOB_MAX) return false;

  uint8_t blob[SHORTCUT_BLOB_MAX];
  if (prefs.getBytes(key, blob, blobLen) != blobLen) return false;

  int stepCount = blob[0];
  int textLen = blob[1];
  if (stepCount <= 0 || stepCount > MAX_STEPS || textLen < 0 || textLen > MAX_TEXT_LENGTH) return false;

  size_t idx = 2 + (size_t)stepCount * 4 + (size_t)textLen;
  if (idx >= blobLen) return false;

  int nameLen = blob[idx++];
  if (nameLen < 0 || nameLen > MAX_NAME_LENGTH || idx + (size_t)nameLen > blobLen) return false;

  hasShortcut = true;
  if (nameOutLen > 0) {
    size_t copyLen = min((size_t)nameLen, nameOutLen - 1);
    memcpy(nameOut, blob + idx, copyLen);
    nameOut[copyLen] = '\0';
  }
  return true;
}

byte modifierBitToRawHid(byte modBit) {
  switch (modBit) {
    case 0x01: return 0xE0;  // Left Ctrl
    case 0x02: return 0xE1;  // Left Shift
    case 0x04: return 0xE2;  // Left Alt
    case 0x08: return 0xE3;  // Left GUI (Cmd/Win)
    default: return 0;
  }
}

void setup() {
  // On a cold power-on the USB boot sequence interferes with BLE radio init.
  // A single soft-reset makes every cold boot behave like a button-press reset.
  if (esp_reset_reason() == ESP_RST_POWERON) {
    ESP.restart();
  }

  // Initialize row pins as outputs (directly driven)
  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }

  // Initialize column pins as inputs with pull-ups
  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  // Initialize button states and names
  for (int i = 0; i < NUM_BUTTONS; i++) {
    lastButtonState[i] = HIGH;
    buttonState[i] = HIGH;
    lastDebounceTime[i] = 0;
    buttonHasShortcut[i] = false;
    buttonNames[i][0] = '\0';
  }

  // Initialize display first — SPI only, no conflict with BLE or USB.
  tftInit();

  // Open storage early so we can load the theme for the splash screen.
  storageReady = prefs.begin("shortcuts", false);
  if (storageReady) {
    loadThemeFromNVS();
  }

  // Splash screen: theme background + centered "SHORTCUTBUTTON" at 2x scale.
  // Determine text color from background brightness.
  {
    uint16_t bgR = (BGDARK >> 11) & 0x1F;
    uint16_t bgG = (BGDARK >> 5)  & 0x3F;
    uint16_t splashTxt = (bgR * 8 + bgG * 4 > 300) ? BLACK : WHITE;
    tftFillRect(0, 0, 240, 320, BGDARK);
    // "SHORTCUTBUTTON" = 14 chars × 12px = 166px wide at 2x, 14px tall
    tftPrintF2x((DISPLAY_WIDTH - 166) / 2, (DISPLAY_HEIGHT - 14) / 2, F("SHORTCUTBUTTON"), splashTxt);
  }

  // Load BLE device name from NVS before BLE init so the right name advertises.
  {
    Preferences devicePrefs;
    if (devicePrefs.begin("device", true)) {
      devicePrefs.getString("name", bleName, sizeof(bleName));
      devicePrefs.end();
    }
  }

  // Init BLE stack (radio + services) but do NOT start advertising yet.
  setupBleKeyboard();

  // USB / Serial
  Serial.begin(115200);
#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
  USB.begin();
#endif

  // Load remaining data from NVS
  if (storageReady) {
    scanForShortcuts();
    loadAllNames();
    loadAllBtnColorsFromNVS();
  }

  Serial.println(storageReady ? F("<READY>") : F("<READY_NOSD>"));

  // Draw initial grid
  drawButtonGrid();

  // Sync button state from actual pin readings.
  // lastDebounceTime was initialized to 0, but millis() is now ~2+ seconds,
  // so without this every button would immediately pass the debounce check and
  // any button reading LOW on the first scanMatrix() call would fire spuriously.
  for (int r = 0; r < ROWS; r++) {
    digitalWrite(rowPins[r], LOW);
    delayMicroseconds(10);
    for (int c = 0; c < COLS; c++) {
      int idx = r * COLS + c;
      byte reading = (byte)digitalRead(colPins[c]);
      lastButtonState[idx] = reading;
      buttonState[idx]      = reading;
      lastDebounceTime[idx] = millis();
    }
    digitalWrite(rowPins[r], HIGH);
  }

  // Everything is initialized — now allow the host to connect.
  // Starting advertising here ensures any incoming connection sees a
  // fully-ready device (storage loaded, display drawn, buttons synced).
  NimBLEDevice::startAdvertising();
  Serial.printf("<BLE:ADV_START:reason=%d:ms=%lu>\n",
    (int)esp_reset_reason(), millis());
}

void loop() {
  handleSerial();
  scanMatrix();

  // Update BLE status in the display header whenever connection state changes.
  if (bleDisplayNeedsUpdate) {
    updateBleStatusDisplay();
  }

  // If BLE advertising stopped for any reason, restart it every second until connected.
  static unsigned long lastAdvCheck = 0;
  if (!bleConnected && millis() - lastAdvCheck > 1000) {
    lastAdvCheck = millis();
    if (!NimBLEDevice::getAdvertising()->isAdvertising()) {
      NimBLEDevice::startAdvertising();
    }
  }
}

void scanMatrix() {
  for (int r = 0; r < ROWS; r++) {
    digitalWrite(rowPins[r], LOW);
    delayMicroseconds(10);

    for (int c = 0; c < COLS; c++) {
      int buttonIndex = r * COLS + c;
      int reading = digitalRead(colPins[c]);

      if (reading != lastButtonState[buttonIndex]) {
        lastDebounceTime[buttonIndex] = millis();
      }

      if ((millis() - lastDebounceTime[buttonIndex]) > DEBOUNCE_DELAY) {
        if (reading != buttonState[buttonIndex]) {
          buttonState[buttonIndex] = reading;

          if (buttonState[buttonIndex] == LOW) {
            Serial.print("<PRESSED:");
            Serial.print(buttonIndex);
            Serial.println(">");
            executeShortcut(buttonIndex);
          }
        }
      }

      lastButtonState[buttonIndex] = reading;
    }

    digitalWrite(rowPins[r], HIGH);
  }
}

// ── Theme/color NVS helpers ──

void saveThemeToNVS() {
  Preferences dp;
  if (!dp.begin("display", false)) return;
  dp.putUChar("tid", currentThemeId);
  dp.putBool("nums", showButtonNumbers);
  if (currentThemeId == 255) {
    dp.putBytes("cols", (uint8_t *)themeColor, THEME_SLOTS * 2);
  }
  dp.end();
}

void loadThemeFromNVS() {
  Preferences dp;
  if (!dp.begin("display", true)) return;
  currentThemeId = dp.getUChar("tid", 0);
  showButtonNumbers = dp.getBool("nums", true);
  if (currentThemeId < NUM_PRESETS) {
    for (int i = 0; i < THEME_SLOTS; i++)
      themeColor[i] = pgm_read_word(&presetThemes[currentThemeId][i]);
  } else if (currentThemeId == 255) {
    size_t len = dp.getBytesLength("cols");
    if (len == THEME_SLOTS * 2) {
      dp.getBytes("cols", (uint8_t *)themeColor, THEME_SLOTS * 2);
    }
  }
  dp.end();
}

void saveBtnColorToNVS(int idx) {
  Preferences dp;
  if (!dp.begin("display", false)) return;
  char key[6];
  snprintf(key, sizeof(key), "bc%02d", idx);
  uint8_t buf[BTN_COLOR_SLOTS * 2];
  for (int i = 0; i < BTN_COLOR_SLOTS; i++) {
    buf[i*2]   = btnCustom[idx][i] >> 8;
    buf[i*2+1] = btnCustom[idx][i] & 0xFF;
  }
  dp.putBytes(key, buf, sizeof(buf));
  dp.end();
}

void loadAllBtnColorsFromNVS() {
  Preferences dp;
  if (!dp.begin("display", true)) return;
  const size_t expected = BTN_COLOR_SLOTS * 2;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    char key[6];
    snprintf(key, sizeof(key), "bc%02d", i);
    uint8_t buf[BTN_COLOR_SLOTS * 2];
    size_t len = dp.getBytesLength(key);
    if (len == expected && dp.getBytes(key, buf, expected) == expected) {
      for (int j = 0; j < BTN_COLOR_SLOTS; j++)
        btnCustom[i][j] = ((uint16_t)buf[j*2] << 8) | buf[j*2+1];
    } else if (len == 4 && dp.getBytes(key, buf, 4) == 4) {
      // Migrate old 4-byte format: border, fill → bordOn, cardOn
      btnCustom[i][3] = ((uint16_t)buf[0] << 8) | buf[1]; // bordOn
      btnCustom[i][1] = ((uint16_t)buf[2] << 8) | buf[3]; // cardOn
    }
  }
  dp.end();
}

void swapButtons(int a, int b) {
  // Swap NVS shortcut blobs
  char keyA[8], keyB[8];
  getStorageKey(a, keyA, sizeof(keyA));
  getStorageKey(b, keyB, sizeof(keyB));

  uint8_t blobA[SHORTCUT_BLOB_MAX], blobB[SHORTCUT_BLOB_MAX];
  size_t lenA = prefs.getBytesLength(keyA);
  size_t lenB = prefs.getBytesLength(keyB);
  if (lenA > 0 && lenA <= SHORTCUT_BLOB_MAX) prefs.getBytes(keyA, blobA, lenA); else lenA = 0;
  if (lenB > 0 && lenB <= SHORTCUT_BLOB_MAX) prefs.getBytes(keyB, blobB, lenB); else lenB = 0;

  // Write swapped
  if (lenB > 0) prefs.putBytes(keyA, blobB, lenB); else prefs.remove(keyA);
  if (lenA > 0) prefs.putBytes(keyB, blobA, lenA); else prefs.remove(keyB);

  // Swap RAM state
  bool tmpHas = buttonHasShortcut[a];
  buttonHasShortcut[a] = buttonHasShortcut[b];
  buttonHasShortcut[b] = tmpHas;

  char tmpName[MAX_NAME_LENGTH + 1];
  strncpy(tmpName, buttonNames[a], MAX_NAME_LENGTH + 1);
  strncpy(buttonNames[a], buttonNames[b], MAX_NAME_LENGTH + 1);
  strncpy(buttonNames[b], tmpName, MAX_NAME_LENGTH + 1);

  uint16_t tmpColor[BTN_COLOR_SLOTS];
  memcpy(tmpColor, btnCustom[a], sizeof(tmpColor));
  memcpy(btnCustom[a], btnCustom[b], sizeof(tmpColor));
  memcpy(btnCustom[b], tmpColor, sizeof(tmpColor));
  saveBtnColorToNVS(a);
  saveBtnColorToNVS(b);

  updateButtonDisplay(a);
  updateButtonDisplay(b);
}

void handleSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == CMD_START) {
      receiving = true;
      serialBuffer = "";
    } else if (c == CMD_END && receiving) {
      receiving = false;
      processCommand(serialBuffer);
      serialBuffer = "";
    } else if (receiving) {
      serialBuffer += c;
      if (serialBuffer.length() > 512) {
        // Malformed packet — no closing '>' after 512 chars; discard to prevent OOM.
        receiving = false;
        serialBuffer = "";
      }
    }
  }
}

void processCommand(String cmd) {
  if (cmd.startsWith("STEPS:")) {
    int firstColon = cmd.indexOf(':', 6);
    if (firstColon == -1) { Serial.println(F("<ERROR:NO_COLON>")); return; }
    int btnIdx = cmd.substring(6, firstColon).toInt();
    int secondColon = cmd.indexOf(':', firstColon + 1);
    if (secondColon == -1) { Serial.println(F("<ERROR:NO_SECOND_COLON>")); return; }
    if (btnIdx < 0 || btnIdx >= NUM_BUTTONS) { Serial.println(F("<ERROR:BAD_BTN>")); return; }

    String name = cmd.substring(firstColon + 1, secondColon);
    String data = cmd.substring(secondColon + 1);

    currentName[0] = '\0';
    int nameLen = min((int)name.length(), MAX_NAME_LENGTH);
    for (int i = 0; i < nameLen; i++) currentName[i] = name.charAt(i);
    currentName[nameLen] = '\0';

    parseSteps(btnIdx, data);
    Serial.println(F("<OK>"));
  } else if (cmd.startsWith("GET:")) {
    int btnIdx = cmd.substring(4).toInt();
    if (btnIdx >= 0 && btnIdx < NUM_BUTTONS) sendShortcut(btnIdx);
  } else if (cmd == "GETALL") {
    sendAllShortcuts();
  } else if (cmd.startsWith("CLEAR:")) {
    int btnIdx = cmd.substring(6).toInt();
    if (btnIdx >= 0 && btnIdx < NUM_BUTTONS) {
      clearShortcut(btnIdx);
      Serial.println("<CLEARED>");
    }
  } else if (cmd == "CLEARALL") {
    for (int i = 0; i < NUM_BUTTONS; i++) clearShortcut(i);
    Serial.println("<CLEAREDALL>");
  } else if (cmd == "PING") {
    Serial.println("<PONG>");
  } else if (cmd == "SDSTATUS") {
    Serial.println(storageReady ? "<SD:OK>" : "<SD:ERROR>");
    Serial.println(bleReadyForReports() ? "<KB:OK>" : "<KB:FAIL>");
  } else if (cmd == "KBTEST") {
    Serial.print(F("<KBTEST:BLE="));
    Serial.print(bleReadyForReports() ? "1" : "0");
    Serial.println(F(">"));

    if (!waitForBleConnection(1500)) {
      Serial.println(F("<KBTEST:NO_BLE>"));
      return;
    }

    delay(3000);  // Time to focus a text field on host
    const char *hello = "hello";
    for (int i = 0; hello[i] != '\0'; i++) {
      bleTypeChar(hello[i]);
    }
    Serial.println(F("<KBTEST:DONE>"));
  } else if (cmd.startsWith("TEST:")) {
    int btnIdx = cmd.substring(5).toInt();
    if (btnIdx >= 0 && btnIdx < NUM_BUTTONS) {
      executeShortcut(btnIdx);
      Serial.println("<TESTED>");
    }
  } else if (cmd == "GETNAME") {
    Serial.print(F("<NAME:"));
    Serial.print(bleName);
    Serial.println(F(">"));
  } else if (cmd.startsWith("SETNAME:")) {
    String newName = cmd.substring(8);
    newName.trim();
    if (newName.length() > 0 && newName.length() <= 32) {
      Preferences devicePrefs;
      if (devicePrefs.begin("device", false)) {
        devicePrefs.putString("name", newName.c_str());
        devicePrefs.end();
      }
      Serial.println(F("<NAME_SET>"));
      delay(100);
      ESP.restart();
    } else {
      Serial.println(F("<ERROR:BAD_NAME>"));
    }
  } else if (cmd == "CLEARBONDS") {
    NimBLEDevice::deleteAllBonds();
    Serial.println(F("<BONDS_CLEARED>"));
  } else if (cmd.startsWith("SETTHEME:")) {
    int id = cmd.substring(9).toInt();
    if (id >= 0 && id < NUM_PRESETS) {
      currentThemeId = id;
      for (int i = 0; i < THEME_SLOTS; i++)
        themeColor[i] = pgm_read_word(&presetThemes[id][i]);
      saveThemeToNVS();
      drawButtonGrid();
      Serial.println(F("<THEME_SET>"));
    } else {
      Serial.println(F("<ERROR:BAD_THEME>"));
    }
  } else if (cmd.startsWith("SETCOLORS:")) {
    // Format: SETCOLORS:bg,hdr,cardOff,cardOn,bordOff,bordOn,numDim,txtDark
    String data = cmd.substring(10);
    uint16_t cols[THEME_SLOTS];
    int idx = 0;
    int start = 0;
    for (int i = 0; i <= (int)data.length() && idx < THEME_SLOTS; i++) {
      if (i == (int)data.length() || data.charAt(i) == ',') {
        cols[idx++] = (uint16_t)data.substring(start, i).toInt();
        start = i + 1;
      }
    }
    if (idx == THEME_SLOTS) {
      currentThemeId = 255; // custom
      for (int i = 0; i < THEME_SLOTS; i++) themeColor[i] = cols[i];
      saveThemeToNVS();
      drawButtonGrid();
      Serial.println(F("<COLORS_SET>"));
    } else {
      Serial.println(F("<ERROR:BAD_COLORS>"));
    }
  } else if (cmd.startsWith("BTNCOLOR:")) {
    // Format: BTNCOLOR:idx:cOff,cOn,bOff,bOn,tOn,tOff (6 values)
    int c1 = cmd.indexOf(':', 9);
    if (c1 != -1) {
      int idx = cmd.substring(9, c1).toInt();
      if (idx >= 0 && idx < NUM_BUTTONS) {
        String vals = cmd.substring(c1 + 1);
        uint16_t parsed[BTN_COLOR_SLOTS] = {0};
        int vi = 0, vs = 0;
        for (int i = 0; i <= (int)vals.length() && vi < BTN_COLOR_SLOTS; i++) {
          if (i == (int)vals.length() || vals.charAt(i) == ',') {
            parsed[vi++] = (uint16_t)vals.substring(vs, i).toInt();
            vs = i + 1;
          }
        }
        if (vi == BTN_COLOR_SLOTS) {
          for (int i = 0; i < BTN_COLOR_SLOTS; i++) btnCustom[idx][i] = parsed[i];
          saveBtnColorToNVS(idx);
          updateButtonDisplay(idx);
          Serial.println(F("<BTNCOLOR_SET>"));
        }
      }
    }
  } else if (cmd.startsWith("SETNUMS:")) {
    int v = cmd.substring(8).toInt();
    showButtonNumbers = (v != 0);
    saveThemeToNVS();
    drawButtonGrid();
    Serial.println(F("<NUMS_SET>"));
  } else if (cmd == "GETNUMS") {
    Serial.print(F("<NUMS:"));
    Serial.print(showButtonNumbers ? 1 : 0);
    Serial.println(F(">"));
  } else if (cmd.startsWith("SWAP:")) {
    int comma = cmd.indexOf(',', 5);
    if (comma != -1) {
      int a = cmd.substring(5, comma).toInt();
      int b = cmd.substring(comma + 1).toInt();
      if (a >= 0 && a < NUM_BUTTONS && b >= 0 && b < NUM_BUTTONS && a != b) {
        swapButtons(a, b);
        Serial.println(F("<SWAPPED>"));
      }
    }
  } else if (cmd == "GETTHEME") {
    Serial.print(F("<THEME:"));
    Serial.print(currentThemeId);
    for (int i = 0; i < THEME_SLOTS; i++) {
      Serial.print(',');
      Serial.print(themeColor[i]);
    }
    Serial.println(F(">"));
    // Also send per-button colors (6 values each)
    for (int i = 0; i < NUM_BUTTONS; i++) {
      bool hasCustom = false;
      for (int j = 0; j < BTN_COLOR_SLOTS; j++) if (btnCustom[i][j]) { hasCustom = true; break; }
      if (hasCustom) {
        Serial.print(F("<BTNCOLOR:"));
        Serial.print(i);
        Serial.print(':');
        for (int j = 0; j < BTN_COLOR_SLOTS; j++) {
          if (j) Serial.print(',');
          Serial.print(btnCustom[i][j]);
        }
        Serial.println(F(">"));
      }
    }
    Serial.print(F("<NUMS:"));
    Serial.print(showButtonNumbers ? 1 : 0);
    Serial.println(F(">"));
  }
}

// Base64 decoding
const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Index(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

int decodeBase64(const String& input, char* output, int maxLen) {
  int outLen = 0, val = 0, bits = 0;
  for (unsigned int i = 0; i < input.length() && outLen < maxLen - 1; i++) {
    char c = input[i];
    if (c == '=') break;
    int idx = b64Index(c);
    if (idx < 0) continue;
    val = (val << 6) | idx;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output[outLen++] = (val >> bits) & 0xFF;
    }
  }
  output[outLen] = '\0';
  return outLen;
}

void parseSteps(int btnIdx, String data) {
  currentStepCount = 0;
  currentTextLen = 0;
  currentButtonIdx = btnIdx;

  int startIdx = 0, semicolonIdx;
  while ((semicolonIdx = data.indexOf(';', startIdx)) != -1 && currentStepCount < MAX_STEPS) {
    parseStep(data.substring(startIdx, semicolonIdx));
    startIdx = semicolonIdx + 1;
  }
  if (startIdx < (int)data.length() && currentStepCount < MAX_STEPS) {
    String stepStr = data.substring(startIdx);
    if (stepStr.length() > 0) parseStep(stepStr);
  }

  saveShortcut(btnIdx);
}

void parseStep(String stepStr) {
  int comma1 = stepStr.indexOf(',');
  int comma2 = stepStr.indexOf(',', comma1 + 1);
  int comma3 = stepStr.indexOf(',', comma2 + 1);
  if (comma1 == -1 || comma2 == -1) return;

  byte action = stepStr.substring(0, comma1).toInt();
  int stepIdx = currentStepCount;

  if (action == STEP_TYPE && comma3 != -1) {
    String b64text = stepStr.substring(comma3 + 1);
    int textStart = currentTextLen;
    int decoded = decodeBase64(b64text, currentTextBuffer + textStart, MAX_TEXT_LENGTH - textStart);
    if (decoded > 0) {
      currentSteps[stepIdx].action = STEP_TYPE;
      currentSteps[stepIdx].keyType = 0;
      currentSteps[stepIdx].keyCode = textStart;
      currentSteps[stepIdx].textLen = decoded;
      currentTextLen += decoded + 1;
      currentStepCount++;
    }
  } else {
    currentSteps[stepIdx].action = action;
    currentSteps[stepIdx].keyType = stepStr.substring(comma1 + 1, comma2).toInt();
    currentSteps[stepIdx].keyCode = stepStr.substring(comma2 + 1).toInt();
    currentSteps[stepIdx].textLen = 0;
    currentStepCount++;
  }
}

void saveShortcut(int btnIdx) {
  if (!storageReady) {
    Serial.println(F("<DEBUG:Save failed - no storage>"));
    return;
  }

  char key[8];
  getStorageKey(btnIdx, key, sizeof(key));

  if (currentStepCount == 0) {
    prefs.remove(key);
    buttonHasShortcut[btnIdx] = false;
    buttonNames[btnIdx][0] = '\0';
    updateButtonDisplay(btnIdx);
    return;
  }

  uint8_t blob[SHORTCUT_BLOB_MAX];
  size_t blobLen = 0;
  if (!serializeCurrentShortcut(blob, sizeof(blob), blobLen)) {
    Serial.println(F("<DEBUG:Save failed - serialize error>"));
    return;
  }
  if (prefs.putBytes(key, blob, blobLen) != blobLen) {
    Serial.println(F("<DEBUG:Save failed - write error>"));
    return;
  }

  buttonHasShortcut[btnIdx] = true;

  strncpy(buttonNames[btnIdx], currentName, MAX_NAME_LENGTH);
  buttonNames[btnIdx][MAX_NAME_LENGTH] = '\0';
  updateButtonDisplay(btnIdx);
}

bool loadShortcut(int btnIdx) {
  currentButtonIdx = btnIdx;
  currentStepCount = 0;
  currentTextLen = 0;
  currentName[0] = '\0';

  if (!storageReady) return false;

  char key[8];
  getStorageKey(btnIdx, key, sizeof(key));
  size_t blobLen = prefs.getBytesLength(key);
  if (blobLen == 0 || blobLen > SHORTCUT_BLOB_MAX) return false;

  uint8_t blob[SHORTCUT_BLOB_MAX];
  if (prefs.getBytes(key, blob, blobLen) != blobLen) return false;

  return deserializeShortcutToCurrent(blob, blobLen);
}

void scanForShortcuts() {
  if (!storageReady) return;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    bool hasShortcut = false;
    char ignoredName[MAX_NAME_LENGTH + 1];
    buttonHasShortcut[i] = loadShortcutMetadata(i, hasShortcut, ignoredName, sizeof(ignoredName)) && hasShortcut;
  }
}

void loadAllNames() {
  if (!storageReady) return;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    buttonNames[i][0] = '\0';
    bool hasShortcut = false;
    if (loadShortcutMetadata(i, hasShortcut, buttonNames[i], sizeof(buttonNames[i])) && hasShortcut) {
      buttonHasShortcut[i] = true;
    } else {
      buttonHasShortcut[i] = false;
    }
  }
}

void updateBleStatusDisplay() {
  bleDisplayNeedsUpdate = false;
  // Clear signal icon area and redraw with current state color.
  tftFillRect(222, 0, 18, HEADER_H - 1, HDR_BG);
  drawSignalIcon(224, 10, bleConnected ? GREEN : SIG_ADV);
}

// Draws a single button tile at its grid position.
// Used by both drawButtonGrid() and updateButtonDisplay().
void drawSingleButton(int btnIdx) {
  int col = btnIdx % 4;
  int row = btnIdx / 4;
  int x = H_MARGIN + col * (BTN_SIZE + H_GAP);
  int y = HEADER_H + V_MARGIN + row * (BTN_SIZE + V_GAP);

  // Clear bounding box with background so rounded corners are clean.
  tftFillRect(x, y, BTN_SIZE, BTN_SIZE, BGDARK);

  // Resolve colors. Per-button custom colors only apply in custom theme (id=255).
  uint16_t borderClr, fillClr, txtClr;
  bool isCustom = (currentThemeId == 255);
  if (buttonHasShortcut[btnIdx]) {
    fillClr   = (isCustom && btnCustom[btnIdx][1]) ? btnCustom[btnIdx][1] : CARD_ON;
    borderClr = (isCustom && btnCustom[btnIdx][3]) ? btnCustom[btnIdx][3] : BORD_ON;
    txtClr    = (isCustom && btnCustom[btnIdx][4]) ? btnCustom[btnIdx][4] : TXON;
  } else {
    fillClr   = (isCustom && btnCustom[btnIdx][0]) ? btnCustom[btnIdx][0] : CARD_OFF;
    borderClr = (isCustom && btnCustom[btnIdx][2]) ? btnCustom[btnIdx][2] : BORD_OFF;
    txtClr    = (isCustom && btnCustom[btnIdx][5]) ? btnCustom[btnIdx][5] : TXOFF;
  }
  tftFillRoundRect(x,     y,     BTN_SIZE,     BTN_SIZE,     BTN_RADIUS,     borderClr);
  tftFillRoundRect(x + 1, y + 1, BTN_SIZE - 2, BTN_SIZE - 2, BTN_RADIUS - 1, fillClr);

  // Small muted number in top-left corner (optional).
  if (showButtonNumbers) {
    char num[3];
    if (btnIdx < 9) { num[0] = '0' + btnIdx + 1; num[1] = '\0'; }
    else { num[0] = '1'; num[1] = '0' + ((btnIdx + 1) % 10); num[2] = '\0'; }
    tftPrint(x + 4, y + 5, num, NUM_DIM);
  }

  // Center content using resolved text color.
  if (buttonNames[btnIdx][0] != '\0') {
    int nameLen = strlen(buttonNames[btnIdx]);
    if (nameLen <= 8) {
      char trimmed[9];
      strncpy(trimmed, buttonNames[btnIdx], 8);
      trimmed[8] = '\0';
      int len = strlen(trimmed);
      while (len > 0 && trimmed[len - 1] == ' ') trimmed[--len] = '\0';
      tftPrint(x + (BTN_SIZE - len * 6) / 2, y + BTN_SIZE / 2 - 3, trimmed, txtClr);
    } else {
      char line1[9], line2[9];
      strncpy(line1, buttonNames[btnIdx],     8); line1[8] = '\0';
      strncpy(line2, buttonNames[btnIdx] + 8, 8); line2[8] = '\0';
      int len1 = strlen(line1);
      while (len1 > 0 && line1[len1 - 1] == ' ') line1[--len1] = '\0';
      int len2 = strlen(line2);
      while (len2 > 0 && line2[len2 - 1] == ' ') line2[--len2] = '\0';
      tftPrint(x + (BTN_SIZE - len1 * 6) / 2, y + BTN_SIZE / 2 - 9, line1, txtClr);
      tftPrint(x + (BTN_SIZE - len2 * 6) / 2, y + BTN_SIZE / 2 + 2, line2, txtClr);
    }
  } else if (buttonHasShortcut[btnIdx]) {
    tftPrintF(x + (BTN_SIZE - 18) / 2, y + BTN_SIZE / 2 - 3, F("SET"), txtClr);
  } else {
    tftPrintF(x + (BTN_SIZE - 30) / 2, y + BTN_SIZE / 2 - 3, F("EMPTY"), txtClr);
  }
}

void drawButtonGrid() {
  tftFillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, BGDARK);

  // Header bar (26px) + 1px accent rule line.
  tftFillRect(0, 0, DISPLAY_WIDTH, HEADER_H - 1, HDR_BG);
  tftFillRect(0, HEADER_H - 1, DISPLAY_WIDTH, 1, BORD_ON);

  // "SHORTCUTS" centered at 2x scale (9 chars × 12px = 106px wide, 14px tall).
  tftPrintF2x((DISPLAY_WIDTH - 106) / 2, (HEADER_H - 1 - 14) / 2, F("SHORTCUTS"), WHITE);

  // Signal icon (right side) — green=connected, amber=advertising.
  drawSignalIcon(224, 10, bleConnected ? GREEN : SIG_ADV);

  for (int i = 0; i < NUM_BUTTONS; i++) {
    drawSingleButton(i);
  }
}

void updateButtonDisplay(int btnIdx) {
  drawSingleButton(btnIdx);
}

void clearShortcut(int btnIdx) {
  if (storageReady) {
    char key[8];
    getStorageKey(btnIdx, key, sizeof(key));
    prefs.remove(key);
  }
  buttonHasShortcut[btnIdx] = false;
  buttonNames[btnIdx][0] = '\0';

  if (currentButtonIdx == btnIdx) {
    currentStepCount = 0;
    currentTextLen = 0;
    currentName[0] = '\0';
  }

  updateButtonDisplay(btnIdx);
}

void encodeBase64(const char* input, int len, String& output) {
  output = "";
  int i = 0;
  while (i < len) {
    int remaining = len - i;
    uint8_t b1 = (uint8_t)input[i++];
    uint8_t b2 = (remaining > 1) ? (uint8_t)input[i++] : 0;
    uint8_t b3 = (remaining > 2) ? (uint8_t)input[i++] : 0;

    output += b64chars[(b1 >> 2) & 0x3F];
    output += b64chars[((b1 & 0x03) << 4) | ((b2 >> 4) & 0x0F)];
    output += (remaining > 1) ? b64chars[((b2 & 0x0F) << 2) | ((b3 >> 6) & 0x03)] : '=';
    output += (remaining > 2) ? b64chars[b3 & 0x3F] : '=';
  }
}

void sendShortcut(int btnIdx) {
  loadShortcut(btnIdx);

  Serial.print(F("<STEPS:"));
  Serial.print(btnIdx);
  Serial.print(F(":"));
  Serial.print(buttonNames[btnIdx]);
  Serial.print(F(":"));

  for (int i = 0; i < currentStepCount; i++) {
    Serial.print(currentSteps[i].action);
    Serial.print(F(","));
    Serial.print(currentSteps[i].keyType);
    Serial.print(F(","));
    Serial.print(currentSteps[i].keyCode);

    if (currentSteps[i].action == STEP_TYPE) {
      Serial.print(F(","));
      String b64;
      encodeBase64(currentTextBuffer + currentSteps[i].keyCode, currentSteps[i].textLen, b64);
      Serial.print(b64);
    }

    if (i < currentStepCount - 1) Serial.print(F(";"));
  }
  Serial.println(F(">"));
}

void sendAllShortcuts() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    sendShortcut(i);
    delay(20);
    yield();
  }
  Serial.flush();
  Serial.println("<DONE>");
}

void executeShortcut(int btnIdx) {
  if (!loadShortcut(btnIdx)) {
    Serial.print(F("<EXEC_FAIL:"));
    Serial.print(btnIdx);
    Serial.println(F(":NO_FILE>"));
    return;
  }

  if (!waitForBleConnection(1500)) {
    Serial.print(F("<EXEC_FAIL:"));
    Serial.print(btnIdx);
    Serial.println(F(":BLE_NOT_CONNECTED>"));
    return;
  }

  Serial.print(F("<EXEC:"));
  Serial.print(btnIdx);
  Serial.print(F(":"));
  Serial.print(currentStepCount);
  Serial.println(F(">"));

  for (int i = 0; i < currentStepCount; i++) {
    byte action = currentSteps[i].action;
    byte keyType = currentSteps[i].keyType;
    byte keyCode = currentSteps[i].keyCode;

    Serial.print(F("<STEP:"));
    Serial.print(i);
    Serial.print(F(",a="));
    Serial.print(action);
    Serial.print(F(",t="));
    Serial.print(keyType);
    Serial.print(F(",k="));
    Serial.print(keyCode);
    Serial.println(F(">"));

    if (action == STEP_TYPE) {
      int textStart = keyCode;
      int textLen = currentSteps[i].textLen;
      for (int j = 0; j < textLen && (textStart + j) < currentTextLen; j++) {
        bleTypeChar(currentTextBuffer[textStart + j]);
        delay(10);
      }
      delay(20);
    } else if (action == STEP_PRESS || action == STEP_RELEASE) {
      byte rawHidCode = 0;
      if (keyType == KEY_TYPE_MODIFIER) {
        rawHidCode = modifierBitToRawHid(keyCode);
      } else {
        rawHidCode = keyCode;
      }

      if (rawHidCode) {
        if (action == STEP_PRESS) blePressRaw(rawHidCode);
        else bleReleaseRaw(rawHidCode);
      }
      delay(20);
    } else if (action == STEP_DELAY) {
      uint16_t ms = ((uint16_t)keyType << 8) | keyCode;
      delay(ms);
    } else if (action == STEP_DELAY_DYNAMIC) {
      uint16_t minMs = (uint16_t)keyType * 20;
      uint16_t maxMs = (uint16_t)keyCode * 20;
      if (maxMs <= minMs) maxMs = minMs + 20;
      delay((uint16_t)random(minMs, maxMs));
    }
  }

  delay(50);
  bleReleaseAll();
}
