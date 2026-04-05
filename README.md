# ⚔️ CosplayRPG

**Il tuo badge cosplay RPG con schermo OLED — by [HexLions](https://github.com/HexLions)**

> Un mini-dispositivo indossabile basato su ESP-01S + OLED 0.96" che genera una scheda RPG personalizzata dal tuo nome, mostra le tue stats e scarica in tempo reale le prossime fiere cosplay italiane.

---

## ✨ Features

- **🎭 Scheda RPG personale** — Classe, livello (= la tua età!), barra XP e stats generate deterministicamente dal tuo nome
- **📊 4 Statistiche** — Stamina, Craft, Style, Hype con barre animate
- **📅 Eventi in tempo reale** — Scarica le fiere cosplay italiane dei prossimi 3 mesi da [cosplayersitaliani.it](https://www.cosplayersitaliani.it)
- **⏰ Orologio NTP** — Ora sincronizzata via internet con data e giorno
- **📶 Setup WiFi via browser** — Captive portal per configurazione senza app
- **💾 Memoria persistente** — La rete WiFi viene salvata in EEPROM
- **🔄 Carousel automatico** — Le schermate ciclano ogni 5 secondi
- **🔥 Alert HOT!** — Gli eventi entro 7 giorni lampeggiano

---

## 🖥️ Le Schermate

| # | Schermata | Descrizione |
|---|-----------|-------------|
| 1 | **RPG Card** | Classe, livello, nome grande, barra XP, Stamina e Style |
| 2 | **Stats** | Tutte e 4 le statistiche con barre e valori numerici |
| 3 | **Eventi** | Carousel fiere cosplay con nome, luogo, data e countdown |
| 4 | **Orologio** | Ora grande, data, giorno della settimana |

---

## 🔧 Hardware

| Componente | Dettagli |
|------------|----------|
| Microcontrollore | **ESP-01S** (ESP8266, WiFi integrato) |
| Display | **SSD1306** OLED 0.96" 128x64 pixel, I2C |
| Alimentazione | 3.3V via micro-USB o power bank |

### Connessioni

```
ESP-01S          SSD1306
────────         ───────
GPIO0    ───►    SDA
GPIO2    ───►    SCL
3.3V     ───►    VCC
GND      ───►    GND
```

---

## 📦 Librerie richieste

Installale da Arduino Library Manager:

- `Adafruit SSD1306`
- `Adafruit GFX Library`
- `ArduinoJson` (v6.x)
- `NTPClient`

---

## ⚙️ Personalizzazione

Modifica queste due righe nel file `.ino`:

```cpp
#define CHAR_NAME      "NOME"          // max 8 caratteri, MAIUSCOLO
#define CHAR_BIRTHDAY  "01/01/2000"    // GG/MM/AAAA
```

Le stats vengono generate deterministicamente dall'hash del nome — lo stesso nome produce sempre gli stessi valori!

### Classi disponibili

La classe viene assegnata in base alle stats dominanti:

| Classe | Condizione |
|--------|-----------|
| COSPLAYER | Style > 90 |
| CRAFTER | Craft > 85 |
| BARD | Hype > 90 |
| WARRIOR, MAGE, ROGUE, PALADIN, RANGER | Assegnata dall'hash del nome |

---

## 📶 Configurazione WiFi

1. Accendi il dispositivo — se non ha una rete salvata, crea un access point
2. Connettiti alla rete **`CosplayRPG-Setup`** (password: `cosplay123`)
3. Apri il browser → **`192.168.4.1`**
4. Inserisci SSID e password della tua rete WiFi
5. Il dispositivo si connette, sincronizza l'ora e scarica gli eventi

> La configurazione viene salvata in EEPROM e ricordata anche dopo lo spegnimento.

---

## 🏗️ Build & Flash

1. Apri `CosplayRPG.ino` in Arduino IDE
2. Seleziona scheda: **Generic ESP8266 Module**
3. Configura nome e data di nascita (vedi Personalizzazione)
4. Flash via programmatore USB-seriale per ESP-01

---

## 📄 Licenza

MIT — Usa, modifica e condividi liberamente.

---

<p align="center">
  <b>⚔️ CosplayRPG</b> — by <a href="https://github.com/HexLions">HexLions</a><br>
  <a href="https://instagram.com/exibiliar.photography">@exibiliar.photography</a>
</p>
