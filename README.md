# CosplayRPG 🎮

> A personalized RPG-style OLED display for cosplayers — a geek gift based on ESP-01S.

![HexLions](https://img.shields.io/badge/by-HexLions-1D9E75?style=flat-square)
![Platform](https://img.shields.io/badge/platform-ESP--01S-blue?style=flat-square)
![Display](https://img.shields.io/badge/display-SSD1306%200.96%22-green?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)

---

## What it does

Each device is personalized with the recipient's name and birthday. On first boot it self-configures via Access Point — no cables, no IDE needed. It then connects to WiFi, syncs the clock via NTP, and automatically fetches upcoming cosplay events in Italy from [cosplayersitaliani.it](https://www.cosplayersitaliani.it).

The display cycles through 4 screens every 5 seconds:

| Screen | Content |
|--------|---------|
| **RPG Card** | Large name, class, level (= age), XP bar |
| **Stats** | STAMINA · CRAFT · STYLE · HYPE with animated bars |
| **Events** | Carousel of events for the current + next 2 months, one every 5s |
| **Clock** | NTP time with blinking colon, date, day of the week |

Stats are generated deterministically from a hash of the name — same name always yields the same stats. They look random but are fully reproducible.

---

## Hardware

```
ESP-01S  ──────────────  SSD1306 0.96" I2C
GPIO0    ──────────────  SDA
GPIO2    ──────────────  SCL
3.3V     ──────────────  VCC
GND      ──────────────  GND
                         addr: 0x3C
```

**Case:** `Mini_ESP01.stl` — ~38 × 36 × 38 mm, print in PLA or PETG.

---

## Repository structure

```
CosplayRPG/
├── CosplayRPG.ino      ← Arduino firmware (single file)
├── Mini_ESP01.stl      ← 3D printed enclosure
└── README.md
```

---

## Configuration

Open `CosplayRPG.ino` and edit **only these two lines** at the top:

```cpp
#define CHAR_NAME      "GIADA"        // recipient's name, max 8 chars UPPERCASE
#define CHAR_BIRTHDAY  "14/03/1998"   // DD/MM/YYYY — used to compute age and stats
```

For each gift, change `CHAR_NAME` and `CHAR_BIRTHDAY`, flash the firmware and you're done.

---

## Required libraries

Install via **Arduino IDE → Library Manager**:

| Library | Version |
|---------|---------|
| Adafruit SSD1306 | ≥ 2.5 |
| Adafruit GFX Library | ≥ 1.11 |
| ArduinoJson | 6.x |
| NTPClient | ≥ 3.2 |

Board package: `ESP8266` — add this URL in Arduino IDE preferences:
`https://arduino.esp8266.com/stable/package_esp8266com_index.json`

Upload settings:
- Board: **Generic ESP8266 Module**
- Flash size: **1MB (FS: none)**
- Upload speed: **115200**

---

## First boot — WiFi setup

1. The ESP starts in **Access Point** mode: network `CosplayRPG-Setup`, password `cosplay123`
2. Connect with your phone or PC
3. The **captive portal** opens automatically (or navigate to `192.168.4.1`)
4. Enter your home network SSID and password
5. Credentials are stored in EEPROM and survive reboots

If the saved network becomes unavailable, the ESP automatically falls back to AP mode.

---

## How the event fetch works

The firmware fetches the monthly pages from cosplayersitaliani.it over HTTPS:

```
https://www.cosplayersitaliani.it/fiere_comics_categoria/fiere-comics-{month}-{year}/
```

The URL is built dynamically from the NTP clock — no manual configuration needed. It downloads the current month and the next two, filters out past events, and displays one event at a time in a carousel with a 5-second slide interval. The fetch repeats every 60 minutes.

---

## Advanced configuration

```cpp
#define AP_SSID          "CosplayRPG-Setup"  // AP network name for setup
#define AP_PASSWORD      "cosplay123"         // AP password (min 8 chars)
#define FETCH_INTERVAL   60                   // minutes between event fetches
```

---

## License

MIT — do whatever you want, a credit to HexLions is always appreciated.

---

*A project by [HexLions](https://github.com/HexLions) — Cosimo Leoni*
