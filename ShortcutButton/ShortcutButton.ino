/*
  ShortcutButton - ESP32-S3-DevKitC-1 (N16R8)

  A 4x4 button matrix that sends programmable keyboard shortcuts via USB HID.
  Connect to the web interface via the native USB port to build and map custom
  shortcuts for each button.

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

#ifndef ARDUINO_USB_MODE
#error This ESP32 board has no native USB interface for HID keyboard
#elif ARDUINO_USB_MODE != 0
#error Set Tools -> USB Mode -> USB-OTG (TinyUSB)
#endif

#include "USB.h"
#include "USBHIDKeyboard.h"
#include <SPI.h>
#include <Preferences.h>

USBHIDKeyboard Keyboard;
Preferences prefs;
bool storageReady = false;

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
#define CYAN       0x07FF
#define SOFTCYAN   0x2D7F
#define TEAL       0x0410
#define BGDARK     0x0208

// Display dimensions
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 320

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

// ST7789 command helpers — identical to Pro Micro original
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
  // Using the SPI2 hardware defaults (MOSI=11, SCK=12, MISO=13).
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);

  // Hardware reset — same timing as Pro Micro original
  digitalWrite(TFT_RST, HIGH);
  delay(50);
  digitalWrite(TFT_RST, LOW);
  delay(50);
  digitalWrite(TFT_RST, HIGH);
  delay(150);

  tftCmd(0x01); delay(150);  // Software reset
  tftCmd(0x11); delay(120);  // Sleep out

  tftCmd(0x3A); tftData(0x55);  // 16-bit color (RGB565)
  tftCmd(0x36); tftData(0x00);  // Memory access: normal orientation
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

bool waitForUSBMount(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (!(bool)USB && (millis() - start < timeoutMs)) {
    delay(5);
  }
  return (bool)USB;
}

void setup() {
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

  // Serial must be initialized before USB.begin() so that the CDC interface
  // is registered with TinyUSB before the stack starts. If Serial.begin() is
  // called after USB.begin(), the host sees only the HID interface (no CDC)
  // and the device may not enumerate at all.
  Serial.begin(115200);

  // Register USB devices then start the TinyUSB stack.
  // Order: Serial (CDC) first, then Keyboard (HID), then USB.begin().
  Keyboard.begin();
  USB.begin();

  // Initialize display immediately so user sees something
  tftInit();
  tftFillRect(0, 0, 240, 320, BLACK);
  tftPrintF(10, 10, F("CONNECTING..."), WHITE);

  // Wait for USB host to enumerate and enable HID (up to 3 seconds).
  // SendReport() silently fails if HID isn't ready, so we must wait here.
  {
    unsigned long t = millis();
    while (!(bool)USB && (millis() - t < 3000)) delay(10);
  }

  // Initialize on-board storage (NVS flash)
  tftFillRect(0, 10, 240, 10, BLACK);
  tftPrintF(10, 10, F("INITIALIZING..."), WHITE);
  storageReady = prefs.begin("shortcuts", false);

  if (storageReady) {
    scanForShortcuts();
    loadAllNames();
  }
  // Print USB mode so we can diagnose HID issues.
  // MODE=0 means TinyUSB/OTG (correct for HID). MODE=1 means HW CDC (no HID).
  Serial.print(F("<USBMODE:USBMODE="));
  Serial.print(ARDUINO_USB_MODE);
  Serial.print(F(",MOUNTED="));
  Serial.print((bool)USB ? "1" : "0");
  Serial.println(F(">"));
  Serial.println(storageReady ? F("<READY>") : F("<READY_NOSD>"));

  // Draw initial grid
  drawButtonGrid();
}

void loop() {
  handleSerial();
  scanMatrix();
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
    Serial.println((bool)USB ? "<KB:OK>" : "<KB:FAIL>");
  } else if (cmd == "KBTEST") {
    // Type "hello" to test if USB HID keyboard is working.
    // 3-second delay gives user time to click into a text editor.
    // USBMODE=0 → TinyUSB/OTG (needed for HID). USBMODE=1 → HW CDC (no HID).
    // If MOUNTED=0, the native USB port may not be connected.
    Serial.print(F("<KBTEST:USBMODE="));
    Serial.print(ARDUINO_USB_MODE);
    Serial.print(F(",MOUNTED="));
    Serial.print((bool)USB ? "1" : "0");
    Serial.println(F(">"));
    delay(3000);
    Keyboard.print("hello");
    delay(200);
    Serial.println(F("<KBTEST:DONE>"));
  } else if (cmd.startsWith("TEST:")) {
    int btnIdx = cmd.substring(5).toInt();
    if (btnIdx >= 0 && btnIdx < NUM_BUTTONS) {
      executeShortcut(btnIdx);
      Serial.println("<TESTED>");
    }
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

void drawButtonGrid() {
  tftFillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, BGDARK);

  tftFillRect(0, 0, DISPLAY_WIDTH, 22, TEAL);
  tftPrintF(93, 8, F("SHORTCUTS"), WHITE);

  int margin = 2;
  int boxW = (DISPLAY_WIDTH - margin * 5) / 4;
  int boxH = (DISPLAY_HEIGHT - 24 - margin * 5) / 4;

  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      int btnIdx = row * 4 + col;
      int x = margin + col * (boxW + margin);
      int y = 24 + margin + row * (boxH + margin);

      if (buttonHasShortcut[btnIdx]) {
        tftFillRect(x, y, boxW, boxH, DARKGRAY);
        tftDrawRect(x, y, boxW, boxH, SOFTCYAN);
      } else {
        tftFillRect(x, y, boxW, boxH, BLACK);
        tftDrawRect(x, y, boxW, boxH, DARKGRAY);
      }

      char num[3];
      if (btnIdx < 9) { num[0] = '0' + btnIdx + 1; num[1] = '\0'; }
      else { num[0] = '1'; num[1] = '0' + ((btnIdx + 1) % 10); num[2] = '\0'; }
      tftPrint(x + 3, y + 3, num, GRAY);

      if (buttonNames[btnIdx][0] != '\0') {
        int nameLen = strlen(buttonNames[btnIdx]);
        if (nameLen <= 8) {
          char trimmed[9];
          strncpy(trimmed, buttonNames[btnIdx], 8);
          trimmed[8] = '\0';
          int len = strlen(trimmed);
          while (len > 0 && trimmed[len-1] == ' ') trimmed[--len] = '\0';
          tftPrint(x + (boxW - len * 6) / 2, y + boxH / 2 - 3, trimmed, WHITE);
        } else {
          char line1[9], line2[9];
          strncpy(line1, buttonNames[btnIdx], 8); line1[8] = '\0';
          strncpy(line2, buttonNames[btnIdx] + 8, 8); line2[8] = '\0';
          int len1 = strlen(line1);
          while (len1 > 0 && line1[len1-1] == ' ') line1[--len1] = '\0';
          int len2 = strlen(line2);
          while (len2 > 0 && line2[len2-1] == ' ') line2[--len2] = '\0';
          tftPrint(x + (boxW - len1 * 6) / 2, y + boxH / 2 - 8, line1, WHITE);
          tftPrint(x + (boxW - len2 * 6) / 2, y + boxH / 2 + 2, line2, WHITE);
        }
      } else if (buttonHasShortcut[btnIdx]) {
        tftFillRect(x + boxW/2 - 12, y + boxH/2 - 5, 24, 11, SOFTCYAN);
        tftPrintF(x + boxW/2 - 9, y + boxH/2 - 3, F("SET"), BLACK);
      } else {
        tftPrintF(x + boxW/2 - 15, y + boxH/2 - 3, F("EMPTY"), DARKGRAY);
      }
    }
  }
}

void updateButtonDisplay(int btnIdx) {
  int margin = 2;
  int boxW = (DISPLAY_WIDTH - margin * 5) / 4;
  int boxH = (DISPLAY_HEIGHT - 24 - margin * 5) / 4;
  int row = btnIdx / 4;
  int col = btnIdx % 4;
  int x = margin + col * (boxW + margin);
  int y = 24 + margin + row * (boxH + margin);

  if (buttonHasShortcut[btnIdx]) {
    tftFillRect(x, y, boxW, boxH, DARKGRAY);
    tftDrawRect(x, y, boxW, boxH, SOFTCYAN);
  } else {
    tftFillRect(x, y, boxW, boxH, BLACK);
    tftDrawRect(x, y, boxW, boxH, DARKGRAY);
  }

  char num[3];
  if (btnIdx < 9) { num[0] = '0' + btnIdx + 1; num[1] = '\0'; }
  else { num[0] = '1'; num[1] = '0' + ((btnIdx + 1) % 10); num[2] = '\0'; }
  tftPrint(x + 3, y + 3, num, GRAY);

  if (buttonNames[btnIdx][0] != '\0') {
    int nameLen = strlen(buttonNames[btnIdx]);
    if (nameLen <= 8) {
      char trimmed[9];
      strncpy(trimmed, buttonNames[btnIdx], 8);
      trimmed[8] = '\0';
      int len = strlen(trimmed);
      while (len > 0 && trimmed[len-1] == ' ') trimmed[--len] = '\0';
      tftPrint(x + (boxW - len * 6) / 2, y + boxH / 2 - 3, trimmed, WHITE);
    } else {
      char line1[9], line2[9];
      strncpy(line1, buttonNames[btnIdx], 8); line1[8] = '\0';
      strncpy(line2, buttonNames[btnIdx] + 8, 8); line2[8] = '\0';
      int len1 = strlen(line1);
      while (len1 > 0 && line1[len1-1] == ' ') line1[--len1] = '\0';
      int len2 = strlen(line2);
      while (len2 > 0 && line2[len2-1] == ' ') line2[--len2] = '\0';
      tftPrint(x + (boxW - len1 * 6) / 2, y + boxH / 2 - 8, line1, WHITE);
      tftPrint(x + (boxW - len2 * 6) / 2, y + boxH / 2 + 2, line2, WHITE);
    }
  } else if (buttonHasShortcut[btnIdx]) {
    tftFillRect(x + boxW/2 - 12, y + boxH/2 - 5, 24, 11, SOFTCYAN);
    tftPrintF(x + boxW/2 - 9, y + boxH/2 - 3, F("SET"), BLACK);
  } else {
    tftPrintF(x + boxW/2 - 15, y + boxH/2 - 3, F("EMPTY"), DARKGRAY);
  }
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
    int b1 = input[i++] & 0xFF;
    int b2 = (i < len) ? (input[i++] & 0xFF) : 0;
    int b3 = (i < len) ? (input[i++] & 0xFF) : 0;
    output += b64chars[(b1 >> 2) & 0x3F];
    output += b64chars[((b1 << 4) | (b2 >> 4)) & 0x3F];
    output += (i > len + 1) ? '=' : b64chars[((b2 << 2) | (b3 >> 6)) & 0x3F];
    output += (i > len) ? '=' : b64chars[b3 & 0x3F];
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
    delay(10);
  }
  Serial.println("<DONE>");
}

void executeShortcut(int btnIdx) {
  if (!loadShortcut(btnIdx)) {
    Serial.print(F("<EXEC_FAIL:"));
    Serial.print(btnIdx);
    Serial.println(F(":NO_FILE>"));
    return;
  }

  if (!waitForUSBMount(1500)) {
    Serial.print(F("<EXEC_FAIL:"));
    Serial.print(btnIdx);
    Serial.println(F(":USB_NOT_MOUNTED>"));
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
        Keyboard.write(currentTextBuffer[textStart + j]);
        delay(10);
      }
      delay(20);
    } else if (action == STEP_PRESS || action == STEP_RELEASE) {
      byte rawHidCode = 0;
      if (keyType == KEY_TYPE_MODIFIER) {
        // Modifiers are stored as left-side modifier bits.
        rawHidCode = modifierBitToRawHid(keyCode);
      } else {
        // Regular keys are stored as USB HID keycodes from the web UI.
        rawHidCode = keyCode;
      }

      if (rawHidCode) {
        if (action == STEP_PRESS) Keyboard.pressRaw(rawHidCode);
        else Keyboard.releaseRaw(rawHidCode);
      }
      delay(20);
    }
  }

  delay(50);
  Keyboard.releaseAll();
}
