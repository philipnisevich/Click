/*
  ShortcutButton - Arduino Pro Micro
  
  A 4x4 button matrix that sends programmable keyboard shortcuts.
  Connect to the web interface to build and map custom shortcuts for each button.
  
  Hardware:
  - Arduino Pro Micro (ATmega32U4)
  - 4x4 Button Matrix (Pins 2-9)
  - SD Card Module (SPI - Pins 10, 14-16)
  - ST7789 Display (SPI shared, CS=A0, DC=A1, RES=A2)
  
  The shortcuts are stored on SD card and persist across power cycles.
*/

#include <Keyboard.h>
#include <SPI.h>
#include <SD.h>

// SD Card chip select pin
const int SD_CS_PIN = 10;
bool sdCardReady = false;

// Display pins
#define TFT_CS  A0
#define TFT_DC  A1
#define TFT_RST A2

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
  
  // Initialize SPI
  SPI.begin();
  
  // Hardware reset
  digitalWrite(TFT_RST, HIGH);
  delay(50);
  digitalWrite(TFT_RST, LOW);
  delay(50);
  digitalWrite(TFT_RST, HIGH);
  delay(150);
  
  tftCmd(0x01); delay(150);  // Software reset
  tftCmd(0x11); delay(120);  // Sleep out
  
  tftCmd(0x3A); tftData(0x55); // 16-bit color (RGB565)
  
  tftCmd(0x36); tftData(0x00); // Memory access: normal orientation
  
  tftCmd(0x21); // Display inversion on (needed for many ST7789 displays)
  
  tftCmd(0x13); // Normal display mode
  
  tftCmd(0x29); delay(50);   // Display on
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
    if (c >= 'a' && c <= 'z') c -= 32; // Convert to uppercase
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

// Pin assignments
const int rowPins[ROWS] = {9, 8, 7, 6};
const int colPins[COLS] = {5, 4, 3, 2};

// Debounce settings
const unsigned long DEBOUNCE_DELAY = 50;

// Maximum steps per shortcut (reduced for RAM)
const int MAX_STEPS = 16;

// Maximum text length for type steps (reduced for RAM)
const int MAX_TEXT_LENGTH = 20;

// Maximum name length for display
const int MAX_NAME_LENGTH = 16;

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

// Convert HID key code to Arduino Keyboard library key code
byte hidToArduinoKey(byte hidCode) {
  if (hidCode >= 4 && hidCode <= 29) return 'a' + (hidCode - 4);
  if (hidCode >= 30 && hidCode <= 38) return '1' + (hidCode - 30);
  if (hidCode == 39) return '0';
  
  switch (hidCode) {
    case 40: return KEY_RETURN;
    case 41: return KEY_ESC;
    case 42: return KEY_BACKSPACE;
    case 43: return KEY_TAB;
    case 44: return ' ';
    case 45: return '-';
    case 46: return '=';
    case 47: return '[';
    case 48: return ']';
    case 49: return '\\';
    case 51: return ';';
    case 52: return '\'';
    case 53: return '`';
    case 54: return ',';
    case 55: return '.';
    case 56: return '/';
    case 58: return KEY_F1;
    case 59: return KEY_F2;
    case 60: return KEY_F3;
    case 61: return KEY_F4;
    case 62: return KEY_F5;
    case 63: return KEY_F6;
    case 64: return KEY_F7;
    case 65: return KEY_F8;
    case 66: return KEY_F9;
    case 67: return KEY_F10;
    case 68: return KEY_F11;
    case 69: return KEY_F12;
    case 73: return KEY_INSERT;
    case 74: return KEY_HOME;
    case 75: return KEY_PAGE_UP;
    case 76: return KEY_DELETE;
    case 77: return KEY_END;
    case 78: return KEY_PAGE_DOWN;
    case 79: return KEY_RIGHT_ARROW;
    case 80: return KEY_LEFT_ARROW;
    case 81: return KEY_DOWN_ARROW;
    case 82: return KEY_UP_ARROW;
    default: return 0;
  }
}

byte modifierBitToKey(byte modBit) {
  switch (modBit) {
    case 0x01: return KEY_LEFT_CTRL;
    case 0x02: return KEY_LEFT_SHIFT;
    case 0x04: return KEY_LEFT_ALT;
    case 0x08: return KEY_LEFT_GUI;
    default: return 0;
  }
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
  
  Serial.begin(115200);
  delay(100);
  
  // Initialize display first (shares SPI with SD card)
  tftInit();
  tftFillRect(0, 0, 240, 320, BLACK);
  tftPrintF(10, 10, F("INITIALIZING..."), WHITE);
  
  // Initialize SD card (MUST be FAT16 or FAT32, NOT exFAT!)
  sdCardReady = SD.begin(SD_CS_PIN);
  
  Keyboard.begin();
  
  if (sdCardReady) {
    scanForShortcuts();
    loadAllNames();
    Serial.println(F("<READY>"));
  } else {
    Serial.println(F("<READY_NOSD>"));
  }
  
  // Draw initial grid
  drawButtonGrid();
}

void loop() {
  handleSerial();
  scanMatrix();
}

void scanMatrix() {
  for (int r = 0; r < ROWS; r++) {
    // Set current row LOW
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
            // Button pressed
            Serial.print("<PRESSED:");
            Serial.print(buttonIndex);
            Serial.println(">");
            executeShortcut(buttonIndex);
          }
        }
      }
      
      lastButtonState[buttonIndex] = reading;
    }
    
    // Set row back to HIGH
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
    // Format: STEPS:buttonIndex:name:data
    int firstColon = cmd.indexOf(':', 6);
    if (firstColon == -1) {
      Serial.println(F("<ERROR:NO_COLON>"));
      return;
    }
    int btnIdx = cmd.substring(6, firstColon).toInt();
    int secondColon = cmd.indexOf(':', firstColon + 1);
    if (secondColon == -1) {
      Serial.println(F("<ERROR:NO_SECOND_COLON>"));
      return;
    }
    if (btnIdx < 0 || btnIdx >= NUM_BUTTONS) {
      Serial.println(F("<ERROR:BAD_BTN>"));
      return;
    }
    
    String name = cmd.substring(firstColon + 1, secondColon);
    String data = cmd.substring(secondColon + 1);
    
    // Store the name (truncate if too long)
    currentName[0] = '\0';
    int nameLen = min((int)name.length(), MAX_NAME_LENGTH);
    for (int i = 0; i < nameLen; i++) {
      currentName[i] = name.charAt(i);
    }
    currentName[nameLen] = '\0';
    
    parseSteps(btnIdx, data);
    Serial.println(F("<OK>"));
  } else if (cmd.startsWith("GET:")) {
    int btnIdx = cmd.substring(4).toInt();
    if (btnIdx >= 0 && btnIdx < NUM_BUTTONS) {
      sendShortcut(btnIdx);
    }
  } else if (cmd == "GETALL") {
    sendAllShortcuts();
  } else if (cmd.startsWith("CLEAR:")) {
    int btnIdx = cmd.substring(6).toInt();
    if (btnIdx >= 0 && btnIdx < NUM_BUTTONS) {
      clearShortcut(btnIdx);
      Serial.println("<CLEARED>");
    }
  } else if (cmd == "CLEARALL") {
    for (int i = 0; i < NUM_BUTTONS; i++) {
      clearShortcut(i);
    }
    Serial.println("<CLEAREDALL>");
  } else if (cmd == "PING") {
    Serial.println("<PONG>");
  } else if (cmd == "SDSTATUS") {
    if (sdCardReady) {
      Serial.println("<SD:OK>");
    } else {
      Serial.println("<SD:ERROR>");
    }
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
  int outLen = 0;
  int val = 0;
  int bits = 0;
  
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
  // Clear current buffer
  currentStepCount = 0;
  currentTextLen = 0;
  currentButtonIdx = btnIdx;
  
  int startIdx = 0;
  int semicolonIdx;
  
  while ((semicolonIdx = data.indexOf(';', startIdx)) != -1 && currentStepCount < MAX_STEPS) {
    String stepStr = data.substring(startIdx, semicolonIdx);
    parseStep(stepStr);
    startIdx = semicolonIdx + 1;
  }
  
  if (startIdx < (int)data.length() && currentStepCount < MAX_STEPS) {
    String stepStr = data.substring(startIdx);
    if (stepStr.length() > 0) {
      parseStep(stepStr);
    }
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

void getButtonFilename(int btnIdx, char* filename) {
  // Use 8.3 filename format: BTN00.DAT to BTN15.DAT
  sprintf(filename, "BTN%02d.DAT", btnIdx);
}

void saveShortcut(int btnIdx) {
  if (!sdCardReady) {
    Serial.println(F("<DEBUG:Save failed - no SD>"));
    return;
  }
  
  char filename[13];
  getButtonFilename(btnIdx, filename);
  
  // Remove existing file
  if (SD.exists(filename)) {
    SD.remove(filename);
  }
  
  // Don't create file if no steps
  if (currentStepCount == 0) {
    buttonHasShortcut[btnIdx] = false;
    buttonNames[btnIdx][0] = '\0';
    updateButtonDisplay(btnIdx);
    return;
  }
  
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    return;
  }
  
  // Write header: step count and text length
  file.write((byte)currentStepCount);
  file.write((byte)currentTextLen);
  
  // Write steps
  for (int i = 0; i < currentStepCount; i++) {
    file.write(currentSteps[i].action);
    file.write(currentSteps[i].keyType);
    file.write(currentSteps[i].keyCode);
    file.write(currentSteps[i].textLen);
  }
  
  // Write text buffer
  for (int i = 0; i < currentTextLen; i++) {
    file.write((byte)currentTextBuffer[i]);
  }
  
  // Write name length and name
  int nameLen = strlen(currentName);
  file.write((byte)nameLen);
  for (int i = 0; i < nameLen; i++) {
    file.write((byte)currentName[i]);
  }
  
  file.flush();
  file.close();
  buttonHasShortcut[btnIdx] = true;
  
  // Update button name and display
  strncpy(buttonNames[btnIdx], currentName, MAX_NAME_LENGTH);
  buttonNames[btnIdx][MAX_NAME_LENGTH] = '\0';
  updateButtonDisplay(btnIdx);
}

bool loadShortcut(int btnIdx) {
  currentButtonIdx = btnIdx;
  currentStepCount = 0;
  currentTextLen = 0;
  currentName[0] = '\0';
  
  if (!sdCardReady) return false;
  
  char filename[13];
  getButtonFilename(btnIdx, filename);
  
  if (!SD.exists(filename)) return false;
  
  File file = SD.open(filename, FILE_READ);
  if (!file) return false;
  
  // Read header
  int stepCount = file.read();
  int textLen = file.read();
  
  // Validate
  if (stepCount < 0 || stepCount > MAX_STEPS || textLen < 0 || textLen > MAX_TEXT_LENGTH) {
    file.close();
    return false;
  }
  
  currentStepCount = stepCount;
  currentTextLen = textLen;
  
  // Read steps
  for (int i = 0; i < currentStepCount; i++) {
    currentSteps[i].action = file.read();
    currentSteps[i].keyType = file.read();
    currentSteps[i].keyCode = file.read();
    currentSteps[i].textLen = file.read();
  }
  
  // Read text buffer
  for (int i = 0; i < currentTextLen; i++) {
    currentTextBuffer[i] = file.read();
  }
  
  // Read name
  currentName[0] = '\0';
  int nameLen = file.read();
  if (nameLen > 0 && nameLen <= MAX_NAME_LENGTH) {
    for (int i = 0; i < nameLen; i++) {
      currentName[i] = file.read();
    }
    currentName[nameLen] = '\0';
  }
  
  file.close();
  return currentStepCount > 0;
}

void scanForShortcuts() {
  if (!sdCardReady) return;
  
  char filename[13];
  for (int i = 0; i < NUM_BUTTONS; i++) {
    getButtonFilename(i, filename);
    buttonHasShortcut[i] = SD.exists(filename);
  }
}

void loadAllNames() {
  if (!sdCardReady) return;
  
  char filename[13];
  for (int i = 0; i < NUM_BUTTONS; i++) {
    buttonNames[i][0] = '\0';
    
    if (!buttonHasShortcut[i]) continue;
    
    getButtonFilename(i, filename);
    File file = SD.open(filename, FILE_READ);
    if (!file) continue;
    
    // Read step count and text length
    int stepCount = file.read();
    int textLen = file.read();
    
    // Validate
    if (stepCount < 0 || stepCount > MAX_STEPS || textLen < 0 || textLen > MAX_TEXT_LENGTH) {
      file.close();
      continue;
    }
    
    // Skip to after steps and text: 2 + stepCount*4 + textLen
    long namePos = 2 + (stepCount * 4) + textLen;
    file.seek(namePos);
    
    int nameLen = file.read();
    if (nameLen > 0 && nameLen <= MAX_NAME_LENGTH) {
      for (int j = 0; j < nameLen; j++) {
        buttonNames[i][j] = file.read();
      }
      buttonNames[i][nameLen] = '\0';
    }
    
    file.close();
  }
}

void drawButtonGrid() {
  // Dark background
  tftFillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, BGDARK);
  
  // Header bar with teal color
  tftFillRect(0, 0, DISPLAY_WIDTH, 22, TEAL);
  // "SHORTCUTS" = 9 chars * 6px = 54px, center: (240-54)/2 = 93
  tftPrintF(93, 8, F("SHORTCUTS"), WHITE);
  
  // Draw grid with spacing
  int margin = 2;
  int boxW = (DISPLAY_WIDTH - margin * 5) / 4;  // 57px
  int boxH = (DISPLAY_HEIGHT - 24 - margin * 5) / 4;  // ~72px
  
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      int btnIdx = row * 4 + col;
      int x = margin + col * (boxW + margin);
      int y = 24 + margin + row * (boxH + margin);
      
      // Background fill based on state
      if (buttonHasShortcut[btnIdx]) {
        tftFillRect(x, y, boxW, boxH, DARKGRAY);
        tftDrawRect(x, y, boxW, boxH, SOFTCYAN);
      } else {
        tftFillRect(x, y, boxW, boxH, BLACK);
        tftDrawRect(x, y, boxW, boxH, DARKGRAY);
      }
      
      // Button number in corner with small background
      char num[3];
      if (btnIdx < 9) {
        num[0] = '0' + btnIdx + 1;
        num[1] = '\0';
      } else {
        num[0] = '1';
        num[1] = '0' + ((btnIdx + 1) % 10);
        num[2] = '\0';
      }
      tftPrint(x + 3, y + 3, num, GRAY);
      
      // Name centered (two lines if > 8 chars)
      if (buttonNames[btnIdx][0] != '\0') {
        int nameLen = strlen(buttonNames[btnIdx]);
        if (nameLen <= 8) {
          // Trim trailing spaces for centering
          char trimmed[9];
          strncpy(trimmed, buttonNames[btnIdx], 8);
          trimmed[8] = '\0';
          int len = strlen(trimmed);
          while (len > 0 && trimmed[len-1] == ' ') trimmed[--len] = '\0';
          int textX = x + (boxW - len * 6) / 2;
          int textY = y + boxH / 2 - 3;
          tftPrint(textX, textY, trimmed, WHITE);
        } else {
          // Two lines - trim each for proper centering
          char line1[9], line2[9];
          strncpy(line1, buttonNames[btnIdx], 8);
          line1[8] = '\0';
          strncpy(line2, buttonNames[btnIdx] + 8, 8);
          line2[8] = '\0';
          // Trim trailing spaces
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
  
  // Redraw the entire box
  if (buttonHasShortcut[btnIdx]) {
    tftFillRect(x, y, boxW, boxH, DARKGRAY);
    tftDrawRect(x, y, boxW, boxH, SOFTCYAN);
  } else {
    tftFillRect(x, y, boxW, boxH, BLACK);
    tftDrawRect(x, y, boxW, boxH, DARKGRAY);
  }
  
  // Button number
  char num[3];
  if (btnIdx < 9) {
    num[0] = '0' + btnIdx + 1;
    num[1] = '\0';
  } else {
    num[0] = '1';
    num[1] = '0' + ((btnIdx + 1) % 10);
    num[2] = '\0';
  }
  tftPrint(x + 3, y + 3, num, GRAY);
  
  // Name (two lines if > 8 chars)
  if (buttonNames[btnIdx][0] != '\0') {
    int nameLen = strlen(buttonNames[btnIdx]);
    if (nameLen <= 8) {
      // Trim trailing spaces for centering
      char trimmed[9];
      strncpy(trimmed, buttonNames[btnIdx], 8);
      trimmed[8] = '\0';
      int len = strlen(trimmed);
      while (len > 0 && trimmed[len-1] == ' ') trimmed[--len] = '\0';
      int textX = x + (boxW - len * 6) / 2;
      int textY = y + boxH / 2 - 3;
      tftPrint(textX, textY, trimmed, WHITE);
    } else {
      // Two lines - trim each for proper centering
      char line1[9], line2[9];
      strncpy(line1, buttonNames[btnIdx], 8);
      line1[8] = '\0';
      strncpy(line2, buttonNames[btnIdx] + 8, 8);
      line2[8] = '\0';
      // Trim trailing spaces
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
  if (sdCardReady) {
    char filename[13];
    getButtonFilename(btnIdx, filename);
    if (SD.exists(filename)) {
      SD.remove(filename);
    }
  }
  buttonHasShortcut[btnIdx] = false;
  buttonNames[btnIdx][0] = '\0';
  
  // Clear buffer if this was the current button
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
  // Load shortcut from SD into current buffer
  loadShortcut(btnIdx);
  
  Serial.print(F("<STEPS:"));
  Serial.print(btnIdx);
  Serial.print(F(":"));
  Serial.print(buttonNames[btnIdx]);  // Send name
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
      encodeBase64(currentTextBuffer + currentSteps[i].keyCode, 
                   currentSteps[i].textLen, b64);
      Serial.print(b64);
    }
    
    if (i < currentStepCount - 1) {
      Serial.print(F(";"));
    }
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
  // Load shortcut from SD on-demand
  if (!loadShortcut(btnIdx)) {
    return;
  }
  
  for (int i = 0; i < currentStepCount; i++) {
    byte action = currentSteps[i].action;
    byte keyType = currentSteps[i].keyType;
    byte keyCode = currentSteps[i].keyCode;
    
    if (action == STEP_TYPE) {
      int textStart = keyCode;
      int textLen = currentSteps[i].textLen;
      
      for (int j = 0; j < textLen && (textStart + j) < currentTextLen; j++) {
        Keyboard.write(currentTextBuffer[textStart + j]);
        delay(10);
      }
      delay(20);
    } else if (action == STEP_PRESS || action == STEP_RELEASE) {
      byte arduinoKey = 0;
      
      if (keyType == KEY_TYPE_MODIFIER) {
        arduinoKey = modifierBitToKey(keyCode);
      } else {
        arduinoKey = hidToArduinoKey(keyCode);
      }
      
      if (arduinoKey == 0) continue;
      
      if (action == STEP_PRESS) {
        Keyboard.press(arduinoKey);
      } else {
        Keyboard.release(arduinoKey);
      }
      
      delay(20);
    }
  }
  
  delay(50);
  Keyboard.releaseAll();
}
