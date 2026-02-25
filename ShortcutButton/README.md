# ShortcutButton Matrix

A 4x4 programmable keyboard shortcut pad for **ESP32-S3**. Program shortcuts in the web UI, store them on SD card, and execute them as USB HID keyboard actions.

## Hardware Required

- **ESP32-S3-DevKitC-1** (WROOM-1-N16R8, 16MB flash, 8MB PSRAM)
- **4x4 Button Matrix** (16 buttons)
- **ST7789 Display** (240x320, 2-inch TFT LCD)
- **MicroSD Card Module** (SPI)
- **SD card formatted FAT16/FAT32** (not exFAT)

## Wiring

### Button Matrix Connections

The 8 matrix pins are physically consecutive on J1 (left header, pins 4-11), so an 8-pin ribbon cable connects directly.

| Matrix Pin | GPIO | J1 Header Pin |
|------------|------|---------------|
| Row 1      | 4    | J1 pin 4      |
| Row 2      | 5    | J1 pin 5      |
| Row 3      | 6    | J1 pin 6      |
| Row 4      | 7    | J1 pin 7      |
| Col 1      | 15   | J1 pin 8      |
| Col 2      | 16   | J1 pin 9      |
| Col 3      | 17   | J1 pin 10     |
| Col 4      | 18   | J1 pin 11     |

### ST7789 Display Connections

Shares SPI bus with SD card.

| Display Pin | GPIO | J1 Header Pin | Notes               |
|-------------|------|---------------|---------------------|
| GND         | -    | GND           |                     |
| VCC         | -    | 3.3V          |                     |
| SCL (SCK)   | 12   | J1 pin 18     | SPI clock           |
| SDA (MOSI)  | 11   | J1 pin 17     | SPI data            |
| RES (RST)   | 14   | J1 pin 20     | Hardware reset      |
| DC          | 8    | J1 pin 12     | Data/command select |
| CS          | 9    | J1 pin 15     | Chip select         |
| BLK         | -    | 3.3V          | Backlight always on |

### SD Card Module Connections

Shares SPI bus with display.

| SD Pin | GPIO | J1 Header Pin | Notes            |
|--------|------|---------------|------------------|
| GND    | -    | GND           |                  |
| VCC    | -    | 3.3V          |                  |
| SCK    | 12   | J1 pin 18     | Shared SPI clock |
| MOSI   | 11   | J1 pin 17     | Shared SPI data  |
| MISO   | 13   | J1 pin 19     | SPI data return  |
| CS     | 10   | J1 pin 16     | Chip select      |

## Port Roles (Dual USB-C Boards)

- **COM port**: Best for flashing and Serial Monitor.
- **USB port**: Native ESP32-S3 USB (HID keyboard output).

Recommended setup while testing:
1. Keep **COM** connected for upload/logs.
2. Keep **USB** connected to the target computer for keyboard output.

## Arduino IDE Settings (Required)

Use **Board: ESP32S3 Dev Module** and set:

| Setting | Value |
|--------|-------|
| Upload Speed | 921600 (or 460800 if unstable) |
| USB Mode | **USB-OTG (TinyUSB)** |
| USB CDC On Boot | Enabled |
| USB Firmware MSC On Boot | **Disabled** |
| USB DFU On Boot | Disabled |
| Upload Mode | UART0 / Hardware CDC (when flashing via COM) |
| CPU Frequency | 240MHz (WiFi) |
| Flash Mode | QIO 80MHz |
| Flash Size | **16MB (128Mb)** |
| Partition Scheme | Default 4MB with spiffs |
| Core Debug Level | None |
| PSRAM | **OPI PSRAM** |
| Erase All Flash Before Sketch Upload | Disabled |

Important:
- If `USB Mode` is not `USB-OTG (TinyUSB)`, this firmware now fails compile on purpose.
- HID keyboard output requires the cable on the **USB** (native) port.

## Upload Instructions

### Method A (recommended): flash via COM

1. Connect board to **COM** port.
2. Set `Upload Mode = UART0 / Hardware CDC`.
3. Upload sketch.
4. Connect board to **USB** port for HID keyboard behavior.

### Method B: flash via USB port

1. Connect board to **USB** port.
2. Set `Upload Mode = USB-OTG CDC (TinyUSB)`.
3. Upload sketch.

If upload fails, hold **BOOT**, tap **RESET**, then upload again.

## arduino-cli

From repo root:

```bash
# Compile only
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,FlashSize=16M,PSRAM=opi,PartitionScheme=default" \
  ShortcutButton

# Upload via COM port (recommended)
arduino-cli compile --upload \
  -p /dev/cu.usbserial-XXXX \
  --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,FlashSize=16M,PSRAM=opi,PartitionScheme=default" \
  ShortcutButton

# Upload via native USB port
arduino-cli compile --upload \
  -p /dev/cu.usbmodemXXXX \
  --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=default" \
  ShortcutButton
```

Find ports with:

```bash
arduino-cli board list
```

## Web Interface

Open `ShortcutButton/web/index.html` in Chrome, Edge, or Opera.

Quick validation path:
1. Connect in web UI.
2. Click **Test HID**.
3. Focus a text editor.
4. Confirm `hello` is typed.

Then program buttons and press physical keys to execute shortcuts.

## Storage

Shortcut files are saved on SD card root:
- `BTN00.DAT` ... `BTN15.DAT`

Limits:
- Max steps per button: 16
- Max text length: 20
- Max display name length: 16

Without SD card, keyboard can still run, but save/load will fail.

## Troubleshooting

- **Compiles with USB mode error**:
  Set `USB Mode = USB-OTG (TinyUSB)`.
- **Web UI connects but shortcuts do nothing**:
  Make sure USB cable is on native **USB** port, not COM-only port.
- **No keyboard output**:
  Verify `USB CDC On Boot = Enabled`, `MSC On Boot = Disabled`, and native USB is connected.
- **Shortcuts not persistent**:
  Reformat SD to FAT32/FAT16 and reinsert before boot.

## License

MIT License
