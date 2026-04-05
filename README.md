# ⚔️ CosplayRPG

**Your wearable cosplay RPG badge with OLED display — by [HexLions](https://github.com/HexLions)**

> A tiny wearable device based on ESP-01S + 0.96" OLED that generates a personalized RPG character card from your name and shows upcoming Italian cosplay conventions in real time.

---

## ✨ Features

- **🎭 Personal RPG Card** — Class, level (= your age!), XP bar and stats deterministically generated from your name
- **📊 4 Stats** — Stamina, Craft, Style, Hype with animated bars
- **📅 Live Events** — Fetches upcoming Italian cosplay conventions from [cosplayersitaliani.it](https://www.cosplayersitaliani.it)
- **⏰ NTP Clock** — Internet-synced time with date and weekday
- **📶 Browser WiFi Setup** — Captive portal configuration, no app needed
- **💾 Persistent Memory** — WiFi credentials saved to EEPROM
- **🔄 Auto Carousel** — Screens cycle every 5 seconds
- **🔥 HOT! Alerts** — Events within 7 days blink with countdown

---

## 🖥️ Screens

| # | Screen | Description |
|---|--------|-------------|
| 1 | **RPG Card** | Class, level, large name, XP bar, Stamina and Style |
| 2 | **Stats** | All 4 stats with progress bars and numeric values |
| 3 | **Events** | Convention carousel with name, venue, date and countdown |
| 4 | **Clock** | Large time display, date, weekday |

---

## 🔧 Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | **ESP-01S** (ESP8266, built-in WiFi) |
| Display | **SSD1306** OLED 0.96" 128x64 pixels, I2C |
| Power | 3.3V via micro-USB or power bank |

### Wiring

```
ESP-01S          SSD1306
────────         ───────
GPIO0    ───►    SDA
GPIO2    ───►    SCL
3.3V     ───►    VCC
GND      ───►    GND
```

---

## 📦 Required Libraries

Install from Arduino Library Manager:

- `Adafruit SSD1306`
- `Adafruit GFX Library`
- `ArduinoJson` (v6.x)
- `NTPClient`

---

## ⚙️ Customization

Edit these two lines in the `.ino` file:

```cpp
#define CHAR_NAME      "NAME"          // max 8 chars, UPPERCASE
#define CHAR_BIRTHDAY  "01/01/2000"    // DD/MM/YYYY
```

Stats are deterministically generated from your name hash — the same name always produces the same values!

### Available Classes

Class is assigned based on dominant stats:

| Class | Condition |
|-------|-----------|
| COSPLAYER | Style > 90 |
| CRAFTER | Craft > 85 |
| BARD | Hype > 90 |
| WARRIOR, MAGE, ROGUE, PALADIN, RANGER | Assigned by name hash |

---

## 📶 WiFi Setup

1. Power on the device — if no network is saved, it creates an access point
2. Connect to **`CosplayRPG-Setup`** (password: `cosplay123`)
3. Open your browser → **`192.168.4.1`**
4. Enter your WiFi SSID and password
5. The device connects, syncs time and fetches events

> Configuration is saved to EEPROM and remembered even after power off.

---

## 🏗️ Build & Flash

1. Open `CosplayRPG.ino` in Arduino IDE
2. Select board: **Generic ESP8266 Module**
3. Set your name and birthday (see Customization)
4. Flash via USB-serial programmer for ESP-01

---

## 📄 License

MIT — Use, modify and share freely.

---

<p align="center">
  <b>⚔️ CosplayRPG</b> — by <a href="https://github.com/HexLions">HexLions</a><br>
  <a href="https://instagram.com/exibiliar.photography">@exibiliar.photography</a>
</p>
