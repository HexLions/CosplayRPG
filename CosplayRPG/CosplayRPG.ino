/*
 * ╔══════════════════════════════════════════════════════╗
 * ║            CosplayRPG — by HexLions  v3              ║
 * ║   ESP-01S + SSD1306 0.96" I2C 128x64                 ║
 * ║                                                      ║
 * ║   • Display ruotato 180° (montaggio sottosopra)      ║
 * ║   • Primo avvio: AP captive portal per WiFi          ║
 * ║   • Stats generate dal nome (deterministiche)        ║
 * ║   • Eventi prossimo mese da cosplayersitaliani.it    ║
 * ║   • Carousel eventi: un evento ogni 5s               ║
 * ╚══════════════════════════════════════════════════════╝
 *
 * Librerie richieste (Arduino Library Manager):
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   - ArduinoJson  (v6.x)
 *   - NTPClient
 *
 * Connessioni ESP-01S → SSD1306:
 *   GPIO0 → SDA  |  GPIO2 → SCL
 *   3.3V  → VCC  |  GND   → GND
 *
 * ══════════════════════════════════════════════════════
 *  ⚙️  PERSONALIZZA QUI — solo nome e data di nascita
 * ══════════════════════════════════════════════════════
 */

#define CHAR_NAME      "GIADA"        // max 8 caratteri, MAIUSCOLO
#define CHAR_BIRTHDAY  "14/03/1998"   // GG/MM/AAAA

#define AP_SSID        "CosplayRPG-Setup"
#define AP_PASSWORD    "cosplay123"

#define FETCH_INTERVAL 60   // minuti tra un aggiornamento eventi e l'altro

// ══════════════════════════════════════════════════════

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUDP.h>
#include <time.h>

#define SDA_PIN   0
#define SCL_PIN   2
#define OLED_ADDR 0x3C

#define EE_MAGIC_ADDR 0
#define EE_SSID_ADDR  1
#define EE_PASS_ADDR  33
#define EE_MAGIC_VAL  0xAB

// ── Oggetti principali ────────────────────────────────
Adafruit_SSD1306 display(128, 64, &Wire, -1);
ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);

// ══════════════════════════════════════════════════════
//  FORWARD DECLARATIONS + DRAW HELPERS
//  (definite qui perché usate prima della loro posizione)
// ══════════════════════════════════════════════════════
voidvoid drawBoot(const char* msg, int pct);
void drawAPScreen();
void drawConnecting(int tick);

// Linea orizzontale separatore
inline void sep(int y) { display.drawFastHLine(0, y, 128, WHITE); }

// Barra progress generica
inline void drawBar(int x, int y, int w, int h, int val) {
  display.drawRect(x, y, w, h, WHITE);
  int fill = map(constrain(val, 0, 100), 0, 100, 0, w - 2);
  if (fill > 0) display.fillRect(x + 1, y + 1, fill, h - 2, WHITE);
}

// ── Stats personaggio ─────────────────────────────────
int   statStamina, statCraft, statStyle, statHype;
int   charLevel;
String charClass;
int   xpCurrent, xpMax;

// ── Lista eventi (max 20 del prossimo mese) ───────────
#define MAX_EVENTS 30
struct Event {
  String name;
  String city;
  String dateFmt;   // "GG/MM/AAAA"
  int    daysTo;
};
Event  events[MAX_EVENTS];
int    eventCount    = 0;
int    currentEvent  = 0;           // indice evento mostrato
unsigned long eventSlideTimer = 0;  // timer cambio evento (5s)

// ── Stato app ─────────────────────────────────────────
enum AppState { ST_AP, ST_RUNNING };
AppState appState = ST_AP;

// ── Schermate ─────────────────────────────────────────
// SCR_EVENT può stare molti cicli (uno per evento)
enum Screen { SCR_CARD, SCR_STATS, SCR_EVENTS, SCR_CLOCK, SCR_COUNT };
Screen currentScreen = SCR_CARD;
unsigned long screenTimer = 0;
// ── Blink e scroll ────────────────────────────────────
bool blinkState = true;
unsigned long blinkTimer = 0;

// ══════════════════════════════════════════════════════
//  HASH + GENERAZIONE STATS
// ══════════════════════════════════════════════════════
uint32_t nameHash(const char* s) {
  uint32_t h = 5381;
  while (*s) h = ((h << 5) + h) ^ (uint8_t)(*s++);
  return h;
}
int statFrom(uint32_t seed, int offset, int lo, int hi) {
  uint32_t v = seed * 1664525UL + 1013904223UL + (uint32_t)offset * 22695477UL;
  return lo + (int)((v >> 24) % (hi - lo + 1));
}
int calcAge() {
  // Richiede NTP già sincronizzato
  time_t rawtime = (time_t)timeClient.getEpochTime();
  struct tm* ti  = gmtime(&rawtime);
  String bd = CHAR_BIRTHDAY;
  int bd_d = bd.substring(0,2).toInt();
  int bd_m = bd.substring(3,5).toInt();
  int bd_y = bd.substring(6,10).toInt();
  int age  = (ti->tm_year + 1900) - bd_y;
  if ((ti->tm_mon + 1) < bd_m ||
      ((ti->tm_mon + 1) == bd_m && ti->tm_mday < bd_d)) age--;
  return max(1, age);
}
const char* classes[] = {
  "COSPLAYER","CRAFTER","WARRIOR","MAGE","ROGUE","BARD","PALADIN","RANGER"
};
void generateStats() {
  uint32_t h  = nameHash(CHAR_NAME);
  statStamina = statFrom(h, 1, 20, 99);
  statCraft   = statFrom(h, 2, 20, 99);
  statStyle   = statFrom(h, 3, 20, 99);
  statHype    = statFrom(h, 4, 20, 99);
  charLevel   = calcAge();
  xpMax       = charLevel * 100;
  xpCurrent   = statFrom(h, 6, xpMax / 10, (xpMax * 9) / 10);
  if      (statStyle  > 90) charClass = "COSPLAYER";
  else if (statCraft  > 85) charClass = "CRAFTER";
  else if (statHype   > 90) charClass = "BARD";
  else                      charClass = classes[h % 8];
}

// ══════════════════════════════════════════════════════
//  EEPROM
// ══════════════════════════════════════════════════════
void eeWriteStr(int addr, const String& s, int maxLen) {
  for (int i = 0; i < maxLen; i++)
    EEPROM.write(addr + i, i < (int)s.length() ? s[i] : 0);
}
String eeReadStr(int addr, int maxLen) {
  String s = "";
  for (int i = 0; i < maxLen; i++) {
    char c = (char)EEPROM.read(addr + i);
    if (!c) break;
    s += c;
  }
  return s;
}
bool hasSavedWifi()                           { return EEPROM.read(EE_MAGIC_ADDR) == EE_MAGIC_VAL; }
void saveWifi(const String& s, const String& p) {
  EEPROM.write(EE_MAGIC_ADDR, EE_MAGIC_VAL);
  eeWriteStr(EE_SSID_ADDR, s, 32);
  eeWriteStr(EE_PASS_ADDR, p, 64);
  EEPROM.commit();
}
void clearWifi() { EEPROM.write(EE_MAGIC_ADDR, 0); EEPROM.commit(); }

// ══════════════════════════════════════════════════════
//  FETCH EVENTI — solo prossimo mese
// ══════════════════════════════════════════════════════
void fetchEvents() {
  eventCount   = 0;
  currentEvent = 0;

  time_t rawNow = (time_t)timeClient.getEpochTime();
  struct tm* ti = gmtime(&rawNow);

  // Oggi per confronto
  struct tm nowTm = {};
  nowTm.tm_year = ti->tm_year; nowTm.tm_mon = ti->tm_mon; nowTm.tm_mday = ti->tm_mday;
  time_t nowEpoch = mktime(&nowTm);

  // Nomi mesi italiani per costruire gli URL
  const char* monthNames[] = {
    "", "gennaio","febbraio","marzo","aprile","maggio","giugno",
    "luglio","agosto","settembre","ottobre","novembre","dicembre"
  };

  // Scarica questo mese e il prossimo
  for (int offset = 0; offset <= 2 && eventCount < MAX_EVENTS; offset++) {
    int m = ti->tm_mon + 1 + offset;  // mese corrente = +1 (tm_mon 0-based)
    int y = ti->tm_year + 1900;
    if (m > 12) { m = 1; y++; }

    // URL tipo: /fiere_comics_categoria/fiere-comics-aprile-2026/
    String url = String("https://www.cosplayersitaliani.it/fiere_comics_categoria/fiere-comics-")
               + monthNames[m] + "-" + String(y) + "/";

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20);

    HTTPClient http;
    http.setTimeout(20000);
    // User-Agent da browser normale — il sito blocca bot
    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    http.addHeader("Accept", "text/html,application/xhtml+xml");
    http.addHeader("Accept-Language", "it-IT,it;q=0.9");

    if (!http.begin(client, url)) {
      http.end();
      continue;
    }

    int code = http.GET();

    if (code != HTTP_CODE_OK) {
      http.end();
      continue;
    }

    // Parsing HTML in streaming — cerca pattern:
    // <h2>TITOLO</h2>  poi  <li>GG Mese AAAA</li>  poi  <li>luogo</li>  <li>regione</li>
    // Leggo riga per riga per non esaurire la RAM

    WiFiClient* stream = http.getStreamPtr();
    String line;
    String pendingTitle = "";
    String pendingDate  = "";
    String pendingCity  = "";
    int    liCount = 0;   // conta i <li> dopo l'<h2>

    unsigned long fetchStart = millis();

    while (http.connected() && (millis() - fetchStart < 15000)) {
      if (!stream->available()) { delay(1); continue; }

      char ch = stream->read();
      if (ch == '\n') {
        line.trim();

        // Trovato <h2> con link evento
        // Pattern: <h2><a href="...">TITOLO</a></h2>  o  ## TITOLO
        if (line.startsWith("<h2>") || line.indexOf("<h2>") >= 0) {
          // Estrai testo tra >...<
          int s = line.indexOf('>');
          // Cerca ultimo > prima di </h2>
          int e2 = line.indexOf("</h2>");
          if (s >= 0 && e2 > s) {
            String inner = line.substring(s+1, e2);
            // Se c'è un <a>, prendi il testo dentro
            int as = inner.lastIndexOf('>');
            int ae = inner.indexOf("</a>");
            if (as >= 0 && ae > as) inner = inner.substring(as+1, ae);
            inner.trim();
            if (inner.length() > 3 && inner.length() < 60 &&
                !inner.startsWith("Fiere") && !inner.startsWith("Event") &&
                inner.indexOf("cosplayersitaliani") < 0) {
              pendingTitle = inner;
              pendingDate  = "";
              pendingCity  = "";
              liCount      = 0;
            }
          }
        }

        // Raccogli <li> dopo un titolo
        if (pendingTitle.length() > 0 && line.indexOf("<li>") >= 0) {
          int s = line.indexOf("<li>") + 4;
          int e2 = line.indexOf("</li>", s);
          if (e2 > s) {
            String val = line.substring(s, e2);
            val.trim();
            liCount++;
            if (liCount == 1 && pendingDate.length() == 0) {
              // Primo <li> = data inizio  "03 Gennaio 2026"
              pendingDate = val;
            } else if (liCount == 3 && pendingCity.length() == 0) {
              // Terzo <li> = città/luogo (secondo è data fine)
              pendingCity = val;
            } else if (liCount == 4 && pendingTitle.length() > 0) {
              // Quarto <li> = regione — abbiamo tutto, salviamo
              // Parsa data "03 Gennaio 2026"
              const char* mNames[] = {
                "gennaio","febbraio","marzo","aprile","maggio","giugno",
                "luglio","agosto","settembre","ottobre","novembre","dicembre"
              };
              pendingDate.toLowerCase();
              int ed2 = -1, em2 = -1, ey2 = -1;
              // Cerca mese nel testo
              for (int mi = 0; mi < 12; mi++) {
                if (pendingDate.indexOf(mNames[mi]) >= 0) {
                  em2 = mi + 1;
                  // Estrai giorno (numero prima del nome mese)
                  int mPos = pendingDate.indexOf(mNames[mi]);
                  String pre = pendingDate.substring(0, mPos);
                  pre.trim();
                  // Ultimo numero in pre
                  int numEnd = pre.length();
                  while (numEnd > 0 && !isDigit(pre[numEnd-1])) numEnd--;
                  int numStart = numEnd;
                  while (numStart > 0 && isDigit(pre[numStart-1])) numStart--;
                  ed2 = pre.substring(numStart, numEnd).toInt();
                  // Anno dopo il nome mese
                  String post = pendingDate.substring(mPos + strlen(mNames[mi]));
                  post.trim();
                  ey2 = post.toInt();
                  break;
                }
              }

              if (ed2 > 0 && em2 > 0 && ey2 > 2020 && eventCount < MAX_EVENTS) {
                struct tm evTm = {};
                evTm.tm_year = ey2-1900; evTm.tm_mon = em2-1; evTm.tm_mday = ed2;
                time_t evEpoch = mktime(&evTm);
                int dt = (int)((evEpoch - nowEpoch) / 86400);
                if (dt >= 0) {
                  char buf[11];
                  sprintf(buf, "%02d/%02d/%04d", ed2, em2, ey2);
                  // Pulisci HTML entities dal titolo
                  pendingTitle.replace("&amp;", "&");
                  pendingTitle.replace("&#8211;", "-");
                  pendingTitle.replace("&#8217;", "'");
                  pendingTitle.replace("&ndash;", "-");
                  events[eventCount] = { pendingTitle, pendingCity, String(buf), dt };
                  eventCount++;
                }
              }
              // Reset per prossimo evento
              pendingTitle = "";
              pendingDate  = "";
              pendingCity  = "";
              liCount      = 0;
            }
          }
        }

        line = "";
      } else if (ch != '\r') {
        if (line.length() < 300) line += ch;
      }
    }
    http.end();
  }

  if (eventCount == 0) {
    events[0] = { "Nessun evento", "prossimo mese", "", -1 };
    eventCount = 1;
  }
}


void drawBoot(const char* msg, int pct) {
  display.clearDisplay();
  display.setTextColor(WHITE);

  // Bordo
  display.drawRect(0, 0, 128, 64, WHITE);

  // Titolo
  display.setTextSize(1);
  display.setCursor(14, 4);
  display.print(F("COSPLAY RPG v1.0"));
  sep(14);

  // Messaggio stato
  display.setTextSize(1);
  display.setCursor(4, 20);
  display.print(msg);

  // Barra progress
  display.drawRect(4, 38, 120, 12, WHITE);
  int fill = map(pct, 0, 100, 0, 118);
  if (fill > 0) display.fillRect(5, 39, fill, 10, WHITE);

  // Percentuale
  display.setCursor(54, 53);
  display.print(pct);
  display.print(F("%"));

  display.display();
}

// ══════════════════════════════════════════════════════
//  SCHERMATA: AP setup
// ══════════════════════════════════════════════════════
void drawAPScreen() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.drawRect(0, 0, 128, 64, WHITE);

  display.setTextSize(1);

  // Titolo
  display.setCursor(22, 3);
  display.print(F("WIFI SETUP"));
  sep(13);

  // Istruzioni
  display.setCursor(4, 17);
  display.print(F("1. Connettiti a:"));

  display.setCursor(4, 28);
  display.print(AP_SSID);

  display.setCursor(4, 39);
  display.print(F("2. Apri il browser"));

  display.setCursor(4, 50);
  display.print(F("   192.168.4.1"));

  display.display();
}

// ══════════════════════════════════════════════════════
//  SCHERMATA: connessione in corso
// ══════════════════════════════════════════════════════
void drawConnecting(int tick) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.drawRect(0, 0, 128, 64, WHITE);

  display.setTextSize(1);
  display.setCursor(16, 4);
  display.print(F("CONNESSIONE..."));
  sep(14);

  display.setCursor(4, 28);
  // Animazione puntini
  int dots = (tick % 4) + 1;
  for (int i = 0; i < dots; i++) display.print(F(". "));

  display.display();
}

// ══════════════════════════════════════════════════════
//  SCHERMATA 1: RPG CARD
// ══════════════════════════════════════════════════════
void drawCard() {
  display.setTextColor(WHITE);

  // Bordo esterno
  display.drawRect(0, 0, 128, 64, WHITE);

  // Riga 1: classe (sx) e livello (dx) — y=4
  display.setTextSize(1);
  display.setCursor(3, 4);
  display.print(charClass.substring(0, 9));

  // Livello allineato a destra — sprintf evita conversione float
  char lvlBuf[6];
  sprintf(lvlBuf, "LV%d", (int)charLevel);
  display.setCursor(128 - (int)strlen(lvlBuf) * 6 - 3, 4);
  display.print(lvlBuf);

  sep(14);

  // Riga 2: nome grande centrato — y=18
  display.setTextSize(2);
  int nameW = strlen(CHAR_NAME) * 12;
  display.setCursor(max(2, (128 - nameW) / 2), 18);
  display.print(CHAR_NAME);

  sep(36);

  // Riga 3: barra XP — y=40
  display.setTextSize(1);
  display.setCursor(3, 40);
  display.print(F("XP"));
  drawBar(18, 39, 106, 9, map(xpCurrent, 0, xpMax, 0, 100));

  sep(51);

  // Riga 4: due stat in fondo — y=55
  display.setCursor(3, 55);
  display.print(F("STM"));
  drawBar(24, 54, 44, 8, statStamina);

  display.setCursor(74, 55);
  display.print(F("STL"));
  drawBar(95, 54, 30, 8, statStyle);

  // Dot WiFi angolo in alto a destra
  if (WiFi.status() == WL_CONNECTED) display.fillCircle(123, 4, 2, WHITE);
  else                                display.drawCircle(123, 4, 2, WHITE);
}

// ══════════════════════════════════════════════════════
//  SCHERMATA 2: STATS dettaglio
// ══════════════════════════════════════════════════════
void drawStats() {
  display.setTextColor(WHITE);
  display.drawRect(0, 0, 128, 64, WHITE);

  display.setTextSize(1);
  display.setCursor(36, 4);
  display.print(F("[ STATS ]"));
  sep(14);

  // 4 righe, ciascuna con: label | barra | valore
  // Spazio: da y=18 a y=58, 4 righe da 10px
  struct { const char* label; int val; } rows[] = {
    { "STAMINA", statStamina },
    { "CRAFT  ", statCraft   },
    { "STYLE  ", statStyle   },
    { "HYPE   ", statHype    },
  };

  // 4 righe in 50px (y 14 → 64): ogni riga alta 12px
  // barY: 15, 27, 39, 51  — stando tutti dentro lo schermo
  for (int i = 0; i < 4; i++) {
    int barY  = 15 + i * 12;
    int textY = barY + 1;

    display.setCursor(3, textY);
    display.print(rows[i].label);  // 7 char * 6px = 42px

    drawBar(46, barY, 60, 9, rows[i].val);

    char valBuf[4];
    sprintf(valBuf, "%3d", rows[i].val);
    display.setCursor(109, textY);
    display.print(valBuf);
  }
}

// ══════════════════════════════════════════════════════
//  SCHERMATA 3: CAROUSEL EVENTI
//  Un evento alla volta, cambia ogni 5s
// ══════════════════════════════════════════════════════
void drawEventCard() {
  display.setTextColor(WHITE);
  display.drawRect(0, 0, 128, 64, WHITE);

  // Header con indice evento e lampeggio se urgente
  Event& ev = events[currentEvent];
  bool urgent = (ev.daysTo >= 0 && ev.daysTo <= 7);

  if (urgent && blinkState) {
    display.fillRect(0, 0, 128, 13, WHITE);
    display.setTextColor(BLACK);
  }

  display.setTextSize(1);
  // "EVENTO 2/5" allineato
  String hdr = "EVENTO " + String(currentEvent + 1) + "/" + String(eventCount);
  display.setCursor(3, 3);
  display.print(hdr);

  // Indicatore urgenza a destra
  if (urgent) {
    display.setCursor(98, 3);
    display.print(F("!! HOT"));
  }

  display.setTextColor(WHITE);
  sep(13);

  // Nome evento su 2 righe se lungo (max 21 car per riga @ size 1)
  display.setTextSize(1);
  String name = ev.name;
  if (name.length() <= 21) {
    // Riga singola, centrata verticalmente un po' più in alto
    display.setCursor(3, 17);
    display.print(name.substring(0, 21));
    display.setCursor(3, 27);
    display.print(name.substring(21, 42)); // seconda riga se c'è
  } else {
    display.setCursor(3, 17);
    display.print(name.substring(0, 21));
    display.setCursor(3, 27);
    display.print(name.substring(21, 42));
  }

  sep(38);

  // Città
  display.setCursor(3, 41);
  display.print(F("LUOGO: "));
  display.print(ev.city.substring(0, 14));

  // Data
  display.setCursor(3, 52);
  if (ev.dateFmt.length()) {
    display.print(ev.dateFmt);
    display.print(F("  "));
    // Countdown
    if      (ev.daysTo == 0) { if (blinkState) display.print(F("OGGI!")); }
    else if (ev.daysTo == 1) display.print(F("domani!"));
    else if (ev.daysTo > 1)  { display.print(F("tra ")); display.print(ev.daysTo); display.print(F("gg")); }
  } else {
    display.print(F("nessun evento"));
  }
}

// ══════════════════════════════════════════════════════
//  SCHERMATA 4: OROLOGIO
// ══════════════════════════════════════════════════════
void drawClock() {
  display.setTextColor(WHITE);

  if (WiFi.status() != WL_CONNECTED) {
    display.setTextSize(1);
    display.drawRect(0, 0, 128, 64, WHITE);
    display.setCursor(22, 28);
    display.print(F("No WiFi  :("));
    return;
  }

  time_t rawtime = (time_t)timeClient.getEpochTime();
  struct tm* ti  = gmtime(&rawtime);

  // Ora grande con blink dei due punti
  char timeBuf[6];
  sprintf(timeBuf, blinkState ? "%02d:%02d" : "%02d %02d", ti->tm_hour, ti->tm_min);

  display.setTextSize(3);
  // Centra: ogni char è 18px, 5 char = 90px
  display.setCursor((128 - 90) / 2, 8);
  display.print(timeBuf);

  sep(42);

  // Data e giorno
  char dateBuf[11];
  sprintf(dateBuf, "%02d/%02d/%04d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);
  const char* days[] = { "DOM","LUN","MAR","MER","GIO","VEN","SAB" };

  display.setTextSize(1);
  display.setCursor(3, 47);
  display.print(days[ti->tm_wday]);
  display.print(F("  "));
  display.print(dateBuf);

  // Puntino sync wifi
  display.fillCircle(124, 60, 2, WHITE);
}

// ══════════════════════════════════════════════════════
//  AP CAPTIVE PORTAL
// ══════════════════════════════════════════════════════
String buildPage(const String& msg = "") {
  return R"(<!DOCTYPE html><html><head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>CosplayRPG Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0a0a0f;color:#00ff88;font-family:monospace;
     display:flex;align-items:center;justify-content:center;min-height:100vh}
.box{background:#111118;border:1px solid #1e3d2a;border-radius:8px;
     padding:32px 24px;width:92%;max-width:360px}
.name{text-align:center;font-size:26px;letter-spacing:4px;color:#00ff88;margin-bottom:4px}
.sub{text-align:center;font-size:11px;color:#445544;margin-bottom:28px;letter-spacing:1px}
h2{color:#ffaa00;letter-spacing:3px;font-size:12px;margin-bottom:16px}
label{display:block;font-size:11px;color:#445544;letter-spacing:1px;margin-bottom:5px}
input{width:100%;background:#1a1a24;border:1px solid #1e3d2a;border-radius:4px;
      color:#00ff88;font-family:monospace;font-size:15px;padding:10px 12px;margin-bottom:16px}
button{width:100%;background:transparent;border:1px solid #00ff88;color:#00ff88;
       font-family:monospace;font-size:13px;letter-spacing:2px;padding:11px;border-radius:4px;cursor:pointer}
.err{color:#ff4455;font-size:12px;margin-bottom:14px;text-align:center;padding:8px;
     border:1px solid #ff445533;border-radius:4px;background:#ff44550a}
</style></head><body><div class='box'>
<div class='name'>)" + String(CHAR_NAME) + R"(</div>
<div class='sub'>CosplayRPG &mdash; HexLions</div>
<h2>CONFIGURAZIONE WIFI</h2>)"
  + (msg.length() ? "<div class='err'>" + msg + "</div>" : "")
  + R"(<form method='POST' action='/save'>
<label>NOME RETE (SSID)</label>
<input type='text' name='ssid' placeholder='NomeRete' autocomplete='off' autocapitalize='none'>
<label>PASSWORD</label>
<input type='password' name='pass' placeholder='Password'>
<button type='submit'>&#9654; CONNETTI</button>
</form></div></body></html>)";
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  auto sendPage = [](){ server.send(200, "text/html", buildPage()); };
  server.on("/",                    HTTP_GET, sendPage);
  server.on("/generate_204",        HTTP_GET, sendPage);
  server.on("/hotspot-detect.html", HTTP_GET, sendPage);
  server.onNotFound(sendPage);

  server.on("/save", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (!ssid.length()) {
      server.send(200, "text/html", buildPage("Inserisci il nome della rete."));
      return;
    }
    server.send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
      "<style>body{background:#0a0a0f;color:#00ff88;font-family:monospace;"
      "display:flex;align-items:center;justify-content:center;min-height:100vh;"
      "text-align:center;padding:20px}</style></head><body>"
      "<div><div style='font-size:22px;color:#ffaa00;letter-spacing:3px;"
      "margin-bottom:16px'>CONNESSIONE...</div>"
      "<div style='color:#445544;font-size:13px'>Guarda il display!</div>"
      "</div></body></html>");

    delay(200);
    server.stop();
    WiFi.softAPdisconnect(true);
    delay(300);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 24 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500); drawConnecting(i);
    }

    if (WiFi.status() == WL_CONNECTED) {
      saveWifi(ssid, pass);
      appState = ST_RUNNING;
      drawBoot("NTP...", 60);
      timeClient.begin(); timeClient.update();
      generateStats();
      drawBoot("FETCH EVENTI...", 80);
      fetchEvents();
      drawBoot("PRONTO!", 100);
      delay(700);
      screenTimer     = millis();
      eventSlideTimer = millis();
    } else {
      clearWifi();
      WiFi.mode(WIFI_AP);
      WiFi.softAP(AP_SSID, AP_PASSWORD);
      server.begin();
      drawAPScreen();
    }
  });

  server.begin();
  appState = ST_AP;
  drawAPScreen();
}

// ══════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════
void setup() {
  delay(100);
  EEPROM.begin(128);
  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.setRotation(2);
  display.clearDisplay();
  display.display();

  drawBoot("INIT...", 10);

  if (hasSavedWifi()) {
    String ssid = eeReadStr(EE_SSID_ADDR, 32);
    String pass = eeReadStr(EE_PASS_ADDR, 64);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 24 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500); drawConnecting(i);
    }
    if (WiFi.status() == WL_CONNECTED) {
      appState = ST_RUNNING;
      drawBoot("NTP...", 60);
      timeClient.begin(); timeClient.update();
      generateStats();
      drawBoot("FETCH EVENTI...", 80);
      fetchEvents();
      drawBoot("PRONTO!", 100);
      delay(700);
    } else {
      clearWifi();
      startAP();
    }
  } else {
    startAP();
  }

  screenTimer     = millis();
  eventSlideTimer = millis();
}

// ══════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════
void loop() {
  if (appState == ST_AP) {
    server.handleClient();
    return;
  }

  unsigned long now = millis();

  if (now - blinkTimer > 500) { blinkState = !blinkState; blinkTimer = now; }

  timeClient.update();

  unsigned long sDur = (currentScreen == SCR_EVENTS)
    ? max(5000UL, (unsigned long)eventCount * 5000UL)
    : 5000UL;
  if (now - screenTimer > sDur) {
    currentScreen   = (Screen)((currentScreen + 1) % SCR_COUNT);
    screenTimer     = now;
    currentEvent    = 0;
    eventSlideTimer = now;
  }

  if (currentScreen == SCR_EVENTS && now - eventSlideTimer > 5000) {
    currentEvent    = (currentEvent + 1) % eventCount;
    eventSlideTimer = now;
  }

  if (WiFi.status() == WL_CONNECTED &&
      now - screenTimer > (unsigned long)FETCH_INTERVAL * 60000UL) {
    fetchEvents();
  }

  display.clearDisplay();
  switch (currentScreen) {
    case SCR_CARD:   drawCard();      break;
    case SCR_STATS:  drawStats();     break;
    case SCR_EVENTS: drawEventCard(); break;
    case SCR_CLOCK:  drawClock();     break;
    default: break;
  }
  display.display();
  delay(40);
}
