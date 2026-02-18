# ShortcutButton Matrix

A 4x4 programmable keyboard shortcut pad using Arduino Pro Micro. Map custom keyboard shortcuts to each of the 16 buttons via a web interface.

## Hardware Required

- **Arduino Pro Micro** (ATmega32U4)
- **4x4 Button Matrix** (16 buttons)
- **SD Card Module** (SPI interface)
- **ST7789 Display** (240x320, 2-inch TFT LCD)

## Wiring

### Button Matrix Connections

| Matrix Pin | Arduino Pin |
|------------|-------------|
| Row 1      | Pin 9       |
| Row 2      | Pin 8       |
| Row 3      | Pin 7       |
| Row 4      | Pin 6       |
| Col 1      | Pin 5       |
| Col 2      | Pin 4       |
| Col 3      | Pin 3       |
| Col 4      | Pin 2       |

### SD Card Module Connections

| SD Module Pin | Arduino Pin |
|---------------|-------------|
| GND           | GND         |
| 3.3V          | (not connected) |
| 5V            | VCC         |
| CS            | Pin 10      |
| MOSI          | Pin 16      |
| SCK           | Pin 15      |
| MISO          | Pin 14      |
| GND           | GND         |

### ST7789 Display Connections

| Display Pin | Arduino Pin | Notes |
|-------------|-------------|-------|
| GND         | GND         |       |
| VCC         | VCC (5V)    |       |
| SCL         | Pin 15      | Shared with SD card |
| SDA         | Pin 16      | Shared with SD card |
| RES         | A2          |       |
| DC          | A1          |       |
| CS          | A0          |       |
| BLK         | VCC         | Backlight always on |

### SD Card Setup

1. **Format** your SD card as **FAT32** (or FAT16 for cards <2GB)
   - **IMPORTANT: exFAT is NOT supported!** Must be FAT32 or FAT16
   - On Mac: Disk Utility → Erase → Format: "MS-DOS (FAT)"
   - On Windows: Right-click → Format → File system: FAT32
2. Each button's shortcut is saved as `BTN00.DAT`, `BTN01.DAT`, etc. in the root directory

### Button Layout (as seen from front)

```
┌─────┬─────┬─────┬─────┐
│  1  │  2  │  3  │  4  │  Row 1
├─────┼─────┼─────┼─────┤
│  5  │  6  │  7  │  8  │  Row 2
├─────┼─────┼─────┼─────┤
│  9  │ 10  │ 11  │ 12  │  Row 3
├─────┼─────┼─────┼─────┤
│ 13  │ 14  │ 15  │ 16  │  Row 4
└─────┴─────┴─────┴─────┘
 Col1  Col2  Col3  Col4
```

## Arduino Setup

1. Open `ShortcutButton.ino` in the Arduino IDE
2. Select **Board**: `Arduino Leonardo` or `Arduino Micro`
3. Select the correct **Port**
4. Upload the sketch

## Web Interface

Open `web/index.html` in Chrome, Edge, or Opera (requires Web Serial API).

### Features

- **Visual 4x4 Grid**: Click any button to select it for programming
- **Display Name**: Give each shortcut a name (max 8 characters) to show on the LCD
- **Three Action Types**:
  - **Press**: Hold down a key
  - **Release**: Release a key
  - **Type**: Type out a text string
- **Quick Shortcuts**: Pre-made common shortcuts (Cmd+C, Cmd+V, etc.)
- **Validation**: Ensures all keys are released before saving
- **Live Feedback**: See when physical buttons are pressed
- **LCD Display**: Shows a 4x4 grid with each button's name

## Usage

1. **Connect** the Arduino via USB
2. **Click "Connect"** in the web interface
3. **Click a button** in the 4x4 grid to select it
4. **Enter a display name** (max 8 characters) - shows on the LCD
5. **Build your shortcut**:
   - Click **↓ Press** then press a key
   - Click **↑ Release** then press the same key
   - Or click **⌨ Type** to enter text
6. **Click "Save"** to program the button
7. **Press the physical button** to trigger the shortcut

## Example: Programming Cmd+C (Copy)

1. Select button 1 in the grid
2. Click **↓ Press** → Press Command key
3. Click **↓ Press** → Press C key
4. Click **↑ Release** → Press C key
5. Click **↑ Release** → Press Command key
6. Click **Save**

Or just click the **Cmd+C** quick shortcut button!

## Commands

| Web Command | Description |
|-------------|-------------|
| `STEPS:n:name:data` | Set shortcut for button n with display name |
| `GET:n` | Get shortcut for button n |
| `GETALL` | Get all shortcuts |
| `CLEAR:n` | Clear button n |
| `CLEARALL` | Clear all buttons |
| `TEST:n` | Execute button n's shortcut |
| `SDSTATUS` | Check SD card status |

## Storage

All 16 shortcuts are stored on the SD card and persist across power cycles.

- **Max steps per button**: 16
- **Max text length per button**: 20 characters
- **Max name length**: 8 characters
- **Storage location**: `BTN00.DAT` - `BTN15.DAT` in root directory

## License

MIT License
